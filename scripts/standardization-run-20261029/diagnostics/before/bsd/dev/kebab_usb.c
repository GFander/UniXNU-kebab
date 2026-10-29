/*
 * KEBAB USB: DWC3 device mode with a CDC-ACM interface, and a kernel log pipe.
 *
 * The bootloader leaves the Synopsys DWC3 powered, clocked, in device mode and
 * halted, with the PHY configured, so only the device controller is restarted:
 * reset, event buffer, endpoint 0, then run with device-event enables. Events
 * are polled from a kernel thread - no interrupt - like the UFS driver. DMA is
 * not cache coherent, so buffers are handed over with explicit cache
 * maintenance.
 *
 * The class is CDC-ACM so the host just sees a serial port: kernel log text is
 * streamed out of bulk IN; bulk OUT feeds the console/debugger input ring.
 *
 * Physical endpoint numbering follows DWC3: ep = 2 * usb_number + direction,
 * even is OUT, odd is IN. Endpoint 0 is physical 0 (OUT) and 1 (IN).
 *
 * Off unless boot-arg kebabusb=1 is given.
 */
#if defined(KEBAB)

#include <kern/clock.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>
#include <os/atomic_private.h>
#include <pexpert/pexpert.h>
#include <mach/vm_param.h>
#include <sys/systm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>

#include "kebab_install.h"
#include "kebab_vol.h"

/*
 * The installer and the volume writer share one 64 KiB bulk buffer and one
 * ACK line, so the two chunk sizes must stay identical.
 */
_Static_assert(KEBAB_VOL_CHUNK == KEBAB_INST_CHUNK,
    "installer and volume writer must use the same chunk size");

/* PEHaltRestart() and kPERestartCPU: the platform restart path, not a
 * driver-private reset register. */
#include <IOKit/IOPlatformExpert.h>

#include <arm/machine_routines.h>
#include <arm/cpu_number.h>
#include <arm/dbgwrap.h>

static void ud_bus_error_report(const char *when);

/* Declared in arm/machine_routines.h, behind a guard this build does not
 * satisfy; the negroni port spells it out for the same reason. */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/* arm/pmap.h declares this behind a guard this build does not satisfy either. */
extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);

void kebab_usb_log(const char *buf, size_t len);
void kebab_usb_log_flush(void);
void kebab_usb_start(void);
int kebab_usb_getc(bool wait);
bool kebab_usb_console_ready(void);
bool kebab_usb_restart_armed(void);
void kebab_usb_panic_spin(void) __dead2;
static void ud_thread(void *, wait_result_t) __dead2;
bool kebab_usb_kdp_enabled(void);
void kebab_usb_kdp_start(void);
bool kebab_usb_kdp_pending(void);
void kebab_usb_kdp_tx_frame(uint32_t len);
static void ud_pump_log(void);
static uint32_t ud_log_take(char *out, uint32_t max);

/*
 * The firmware reset interface lives in KebabPlatformExpert.cpp, next to the
 * SYSTEM_RESET backend that PEHaltRestart() already uses.  FASTBOOT2 needs
 * SYSTEM_RESET2 directly, and the probe needs PSCI_FEATURES; neither belongs
 * in a USB driver, so the platform expert exports them.
 */
extern uint64_t kebab_psci_call(uint64_t function, uint64_t arg1, uint64_t arg2);
extern uint32_t kebab_psci_version(void);
extern uint32_t kebab_psci_features(uint32_t function);
extern uint64_t kebab_psci_system_reset2(uint32_t reset_type, uint32_t cookie);

/*
 * Read-only SPMI arbiter probe, plus the gated PMIC PON write path.  The
 * driver lives in bsd/dev/kebab_spmi.c and owns every PMIC-facing register;
 * this file only matches the console commands and calls it, so the USB path
 * never touches the arbiter or the PMIC directly.
 */
extern void kebab_spmi_init(void);
extern void kebab_spmi_report(void);
extern void kebab_spmi_pon_report(void);

/* bsd/dev/kebab_touch.c: read-only probe and explicit bring-up. */
extern void kebab_touch_probe(void);
extern void kebab_touch_init_now(void);
extern void kebab_touch_clock_enable(void);
extern void kebab_touch_se(void);
extern void kebab_touch_ping(void);

/* bsd/dev/kebab_spmi.c: read-only PMIC rail probe. */
extern void kebab_spmi_power_report(void);

/* bsd/dev/kebab_spmi.c: read-only RPMh discovery. */
extern void kebab_rpmh_report(void);

/*
 * bsd/dev/kebab_spmi.c: the RPMh VRM rail enable.  One rail per command, gated
 * by kebab_vrm_write; the encoder, the whitelist and the TCS state machine all
 * live in that file, so this one only names the rail.
 */
extern int kebab_spmi_vrm_enable(const char *rail);

/* bsd/dev/kebab_spmi.c: read-only TCS dump, the evidence behind VROMON. */
extern void kebab_tcs_report(void);

/* bsd/dev/kebab_fwlog.c: read-only reader of the firmware's ring. */
extern void kebab_fwlog_report(uint32_t max_bytes);
/*
 * Map the firmware ring once, early.  kebab_fwlog_attach() is idempotent and
 * caches the mapping, but it cannot run after a panic because ml_io_map needs
 * to allocate; doing it here means a later crash can still dump the pre-fault
 * log instead of reporting "no firmware log attached".
 */
extern void kebab_fwlog_attach_at_init(void);

extern bool kebab_spmi_pon_writes_enabled(void);
extern int kebab_spmi_pon_reason_write(void);
extern int kebab_spmi_pon_reason_write_normal(void);
extern void kebab_spmi_test_panic_lock(void);

/* ---------------------------------------------------------------- registers */

#define GCTL            0xc110
#define GHWPARAMS0      0xc140
#define GHWPARAMS7      0xc15c
#define GUSB2PHYCFG0    0xc200
#define GEVTEN          0xc114
#define GEVNTADRLO      0xc400
#define GEVNTADRHI      0xc404
#define GEVNTSIZ        0xc408
#define GEVNTCOUNT      0xc40c
#define DCFG            0xc700
#define DCTL            0xc704
#define DEVTEN          0xc708
#define DSTS            0xc70c
#define DALEPENA        0xc720
#define DEPCMDPAR2(n)   (0xc800 + 0x10 * (n))
#define DEPCMDPAR1(n)   (0xc804 + 0x10 * (n))
#define DEPCMDPAR0(n)   (0xc808 + 0x10 * (n))
#define DEPCMD(n)       (0xc80c + 0x10 * (n))

#define DCTL_RUN_STOP   (1u << 31)
#define DCTL_CSFTRST    (1u << 30)
#define DSTS_DEVCTRLHLT (1u << 22)

/* DCFG: bits 2:0 device speed (0 = high speed), bits 9:3 device address */
#define DCFG_SPEED_MASK (0x7u << 0)
#define DCFG_ADDR_MASK  (0x7fu << 3)
#define DCFG_ADDR(a)    (((uint32_t)(a) << 3) & DCFG_ADDR_MASK)
#define DCFG_NUMP_SHIFT 17
#define DCFG_NUMP_MASK  (0x1fu << DCFG_NUMP_SHIFT)

/* DEVTEN: disconnect, connect done, reset */
#define DEVTEN_DEVICE   (0x7u | (1u << 9) | (1u << 11))

#define DEPCMD_SETEPCONFIG       1
#define DEPCMD_SETTRANSFRESOURCE 2
#define DEPCMD_SETSTALL          4
#define DEPCMD_CLEARSTALL        5
#define DEPCMD_STARTTRANSFER     6
#define DEPCMD_ENDTRANSFER       8
#define DEPCMD_DEPSTARTCFG       9
#define DEPCMD_CMDIOC            (1u << 8)
#define DEPCMD_FORCERM           (1u << 11)
#define DEPCMD_CMDACT            (1u << 10)

/* SetEndpointConfig parameters: type in 2:1, packet size in 13:3, FIFO in 21:17;
 * transfer-complete in 8, transfer-not-ready in 10, endpoint number in 29:26 and
 * direction in 25 of the second parameter. */
#define DEPCFG_PARAM0(type, maxpkt, fifo) \
	(((uint32_t)(type) << 1) | ((uint32_t)(maxpkt) << 3) | ((uint32_t)(fifo) << 17))
#define DEPCFG_PARAM1(ep) \
	((1u << 8) | (1u << 10) | (((uint32_t)(ep)) << 25))

#define TRB_HWO         (1u << 0)
#define TRB_LST         (1u << 1)
#define TRB_CHN         (1u << 2)
#define TRB_ISP_IMI     (1u << 10)
#define TRB_IOC         (1u << 11)
#define TRBCTL_NORMAL   (1u << 4)
#define TRBCTL_SETUP    (2u << 4)
#define TRBCTL_STATUS2  (3u << 4)
#define TRBCTL_STATUS3  (4u << 4)
#define TRBCTL_DATA     (5u << 4)
#define TRBSTS_SETUP_PENDING 2u

#define EP_TYPE_CONTROL 0
#define EP_TYPE_BULK    2
#define EP_TYPE_INTR    3

/* Port facts: DWC3 at 0x0A600000, Qualcomm QSCRATCH above it. */
#define DWC3_PA        0x0a600000
#define DWC3_SIZE      0x0000d000
#define QSCRATCH_PA    0x0a6f8800
#define QSCRATCH_HS    0x10    /* HS_PHY_CTRL */
#define QSCRATCH_SS    0x30    /* SS_PHY_CTRL */
#define UTMI_OTG_VBUS_VALID  (1u << 20)
#define SW_SESSVLD_SEL       (1u << 28)
#define LANE0_PWR_PRESENT    (1u << 24)

/* ------------------------------------------------------------- DMA layout */

/*
 * One physically contiguous arena.  It used to be a single 4 KiB page because
 * every transfer was one maximum-packet-size buffer; the userland installer
 * hands the controller a whole 64 KiB chunk per TRB, and a TRB names exactly
 * one physical buffer, so the arena has to be contiguous and big enough for
 * the chunk.  Every object the controller writes on its own still gets its own
 * cache line, so a clean of one never writes back stale bytes over another.
 */
#define UD_SIZE        0x11000
#define UD_EVT         0x0000      /* 1 KiB event buffer */
#define UD_EVT_SIZE    1024
#define UD_TRB_OUT     0x0400
#define UD_TRB_IN      0x0440
#define UD_TRB_NOTIFY  0x0480
#define UD_TRB_BULKIN  0x04c0
#define UD_TRB_BULKOUT 0x0500
#define UD_TRB_OUT_DATA 0x0540   /* ep0 OUT data stage, own slot */
#define UD_SETUP       0x0580
#define UD_DATA        0x0600      /* ep0 data stage, 512 bytes */
#define UD_NOTIFY      0x0800
/* Bulk buffers must be aligned to the endpoint maximum packet size, which is 512,
 * so they live at 512-aligned offsets; 0x0b00 is only 64-aligned, which is why the
 * endpoints worked at 64-byte packets and died at 512. */
#define UD_BULKIN      0x0c00      /* 512 bytes, 512-aligned */
#define UD_BULKOUT     0x0e00      /* 512 bytes, 512-aligned */
#define UD_INST        0x1000      /* KEBAB_INST_CHUNK bytes, chunk-aligned */

/* physical endpoints: ep = 2 * usb_number + direction */
#define EP0_OUT        0
#define EP0_IN         1
#define EP_NOTIFY      5           /* usb 2, IN  -> descriptor 0x82 */
#define EP_BULK_OUT    2           /* usb 1, OUT -> descriptor 0x01 */
#define EP_BULK_IN     3           /* usb 1, IN  -> descriptor 0x81 */
#define EP_MASK        ((1u << EP0_OUT) | (1u << EP0_IN) | (1u << EP_NOTIFY) | \
    (1u << EP_BULK_OUT) | (1u << EP_BULK_IN))

/* --------------------------------------------------------------- state */

#define DATA_EP_MASK    ((1u << EP_BULK_OUT) | (1u << EP_BULK_IN) | (1u << EP_NOTIFY))
#define LOG_RING_SIZE  16384

/*
 * The SM8250 IMEM page that ABL reads on the next boot.  The address and the
 * field offset come from the device's own DTB:
 *   /soc/qcom,msm-imem@146bf000  reg = <0x146bf000 0x1000>
 *   /soc/.../restart_reason@65c  reg = <0x65c 0x04>
 *
 * 0x77665500 is not a guess: the vendor kernel writes exactly this value for
 * "bootloader" (drivers/power/reset/msm-poweroff.c, msm_restart_prepare with
 * cmd="bootloader"), together with PON_RESTART_REASON_BOOTLOADER.  0x77665501
 * is the "anything else" value in that same table, so writing it would ask
 * for a normal boot.
 */
#define KEBAB_IMEM_PA           0x146bf000u
#define KEBAB_IMEM_SIZE         0x1000u
#define KEBAB_IMEM_RESTART      0x65cu
#define KEBAB_FASTBOOT_COOKIE   0x77665500u

/* PSCI function IDs: SYSTEM_RESET is SMC32, SYSTEM_RESET2 is SMC64-only. */
#define KEBAB_PSCI_VERSION      0x84000000u
#define KEBAB_PSCI_FEATURES     0x8400000au
#define KEBAB_PSCI_SYSTEM_RESET 0x84000009u
#define KEBAB_PSCI_RESET2       0xc4000012u

/*
 * A PSCI function ID that cannot exist, used as a negative control for
 * PSCI_FEATURES.  The firmware must answer PSCI_RET_NOT_SUPPORTED; if it
 * answers zero, every "supported" answer from the same probe is meaningless.
 */
#define KEBAB_PSCI_NEGATIVE_CONTROL 0x8400ffffu
#define KEBAB_PSCI_RET_NOT_SUPPORTED 0xffffffffu

#define UD_REBOOT_PLAIN         1u
#define UD_REBOOT_FASTBOOT      2u
#define UD_REBOOT_FASTBOOT2     3u
#define UD_REBOOT_PSCI_REPORT   4u
#define UD_REBOOT_IMEM_REPORT   5u
#define UD_REBOOT_PSCI_NEGATIVE 6u
#define UD_REBOOT_SPMI_REPORT   7u
#define UD_REBOOT_PON_REPORT    8u
#define UD_REBOOT_TOUCH_PROBE   9u
#define UD_REBOOT_TOUCH_INIT    10u
#define UD_REBOOT_TOUCH_CLOCK   11u
#define UD_REBOOT_TOUCH_SE      12u
#define UD_REBOOT_TOUCH_PING    13u
#define UD_REBOOT_PWR_REPORT    14u
#define UD_REBOOT_RPMH_REPORT   15u
#define UD_REBOOT_FWLOG_REPORT  16u
#define UD_REBOOT_FWLOG_ALL     17u
/* During a panic keep the dump small: the console pump is the only writer. */
#define KEBAB_FWLOG_PANIC_MAX   0x8000u
#define UD_REBOOT_VRMON_LDOA13  18u
#define UD_REBOOT_VRMON_LDOC1   19u
#define UD_REBOOT_VRMON_OTHER   20u
#define UD_REBOOT_TCS_REPORT    21u
#define UD_REBOOT_INST_BEGIN    22u
#define UD_REBOOT_INST_DRY      23u
#define UD_REBOOT_INST_STATUS   24u
#define UD_REBOOT_INST_CHECK    25u
#define UD_REBOOT_VOL_BEGIN     26u
#define UD_REBOOT_VOL_DRY       27u
#define UD_REBOOT_VOL_STATUS    28u
#define UD_REBOOT_SYNC          29u

struct ud_endpoint {
	uint32_t trb, length, ctl, resource;
	unsigned end_polls;
	bool busy, ending, halted, zlp;
};
#define EP0_HISTORY_SIZE 32

static struct {
	vm_offset_t dwc;
	vm_offset_t qs;
	vm_offset_t imem;              /* KEBAB_IMEM_PA, mapped once at start */
	vm_offset_t dma;
	uint64_t    dma_pa;
	uint32_t    evt_pos;
	uint32_t    events;
	uint32_t    event_acked, event_discarded, resets, disconnects, fences;
	uint32_t    takeover_active;
	int         state;
	bool        data_in;
	bool        configured;
	bool        running;
	bool        start_attempted;
	struct ud_endpoint ep[6];
	/* Transmit gate: who may drive the log pump and the flush loops. */
	uint32_t    owner;             /* CPU number + 1; zero means unowned */
	uint32_t    owner_depth;       /* re-entrancy count while owner holds it */
	/*
	 * Receive lock: who may drain the event buffer and (re)arm bulk OUT.
	 * Deliberately separate from the transmit gate.  Reading a byte from the
	 * host is a different right from owning transmission, and a debugger must
	 * be able to receive while the panic path owns transmission.
	 */
	uint32_t    rx_owner;          /* CPU number + 1; zero means unowned */
	uint32_t    rx_depth;          /* re-entrancy count while rx_owner holds it */
	boolean_t   rx_irq;            /* interrupt state saved by the outer hold */
	uint32_t    panic_cpu;         /* terminal takeover, never released */
	bool        failed, dead, connected, connect_pending;
	bool        status_ready, setup_pending, setup_received, restart;
	bool        set_line, line_valid, controls_valid, notify_pending;
	unsigned    mps, control_lines, next_controls;
	uint8_t     line[7], next_line[7];
	int         config_pending, halt_pending, bus_pending;
	bool        halt_set;
	uint32_t    tx_pending, tx_completed;
	unsigned    probe_match;
	bool        probe_requested;
	bool        ep0_status3;
	bool        console_input;
	bool        panic_hook;
	bool        want_panic;
	/*
	 * Recovery commands are matched while bulk OUT is serviced, but they are
	 * executed only after the transport gate has been released: the restart
	 * path prints and must be able to flush, and the panic loop must not call
	 * it with ud.owner held.
	 */
	volatile uint32_t reboot_command;   /* UD_REBOOT_* or zero */
	volatile bool     restart_armed;    /* console asked for the restart */
	/*
	 * While an INST stream runs, bulk OUT stops feeding the input ring and
	 * the command matcher: every completion is stream payload for the
	 * installer.  inst_pending means a chunk is sitting in the arena and the
	 * endpoint must stay unarmed until it has been written or discarded --
	 * that, and the ACK the host waits for, is the whole flow control.
	 */
	volatile bool     inst_stream;
	volatile bool     inst_pending;
	uint32_t          inst_len;
	/*
	 * The volume writer uses the same buffer and the same pending/ACK
	 * discipline, with its own mode flag so a chunk is never handed to the
	 * wrong parser.  out_stream records which stream the arm was issued for,
	 * because the mode can change while a transfer is in flight.
	 */
	volatile bool     vol_stream;
	volatile bool     vol_pending;
	uint32_t          vol_len;
	uint32_t          out_stream;       /* 0 console, 1 INST, 2 VOL */
	uint64_t          vol_wire;         /* VOL BEGIN / VOL DRY argument */
	uint64_t          vol_size;         /* requested volume size */
	bool              vol_confirm;
	uint32_t          out_off;          /* buffer the pending OUT arm uses */
	uint64_t          inst_wire;        /* the INST BEGIN argument */
	char              inst_path[KEBAB_INST_PATH_MAX];
	/* Raw PSCI answers, kept exactly as the firmware returned them. */
	uint32_t    psci_reset2;       /* FEATURES(SYSTEM_RESET2) */
	bool        psci_reset2_valid; /* the value above was actually read */
	bool        psci_probe_sane;   /* the negative control came back 0xffffffff */
	uint32_t    tx_bytes;
	uint32_t    rx_bytes;          /* bulk-OUT bytes accepted into in_ring */
	uint32_t    rx_dropped;        /* bulk-OUT bytes lost to a full in_ring */
	uint32_t    kdp_rx_bytes;      /* bytes handed to the byte-stream reader */
	uint32_t    kdp_tx_frames;     /* debugger frames queued for bulk IN */
	uint32_t    kdp_tx_bytes;      /* wire bytes in those frames */
	uint32_t    ep0_progress;  /* packed state, see docs/EP0-PROBE.md */
	uint32_t    ep0_seq;       /* transitions recorded; low byte packs in */
	uint32_t    ep0_req;       /* request code of the transfer in flight */
	volatile uint32_t ep0_history[EP0_HISTORY_SIZE];
	uint32_t    in_head;
	uint32_t    in_tail;
	/*
	 * A serialized KDP frame is up to about 3 KB (a 1518-byte inner packet
	 * plus escaping and the CRC prefix), and the host's bulk-OUT writes can
	 * arrive in 512-byte transfers.  256 bytes was enough for keystrokes but
	 * would drop the middle of a debugger request; keep a whole frame here.
	 */
	char        in_ring[4096];
	uint32_t    log_head;
	uint32_t    log_tail;
	uint32_t    log_busy;
	char        log_ring[LOG_RING_SIZE];
} ud;

enum { EP0_SETUP, EP0_DATA, EP0_STATUS, EP0_WAIT_STATUS, EP0_ABORT };

/* Existing phases 1..7/16/17 are unchanged. New phases use the same hook. */
enum {
	NOTE_WAIT_STATUS = 8, NOTE_ABORT = 9, NOTE_STALL = 10,
	NOTE_END = 11, NOTE_ENDED = 12, NOTE_CONFIG = 13, NOTE_HALT = 14,
	NOTE_DISCONNECT = 18, NOTE_CONNECT = 19, NOTE_TAKEOVER = 20,
	NOTE_FAULT = 21, NOTE_CONTROLS = 22, NOTE_LINE = 23, NOTE_FENCE = 24,
	NOTE_DEFER_SETUP = 25, NOTE_NOTIFY = 26, NOTE_LOG_TAKEOVER = 27
};

static void ud_event(uint32_t ev);
static void ud_service(void);
static void ud_poll_locked(void);
static bool ud_controller_restart(void);
static void ud_start_setup(void);

/* ------------------------------------------------------------- EP0 probe */

/*
 * One word per transition, packed sequence/phase/request/status (see
 * docs/EP0-PROBE.md), plus the last 32 of them.  Plain stores only: this is
 * called from the panic path, where an allocation, a lock or a printf could
 * deadlock or re-enter the console path being measured.
 */
static void
kebab_usb_ep0_note(uint32_t req, uint32_t phase, uint32_t status)
{
	uint32_t seq = ud.ep0_seq + 1;
	uint32_t word = ((seq & 0xff) << 24) |
	    ((phase & 0xff) << 16) | ((req & 0xff) << 8) | (status & 0xff);

	ud.ep0_history[seq & (EP0_HISTORY_SIZE - 1)] = word;
	ud.ep0_progress = word;
	ud.ep0_seq = seq;
}

/* bmRequestType/bRequest reduced to the request numbers in the spec. */
static uint32_t
kebab_usb_ep0_request(uint8_t type, uint8_t req)
{
	if ((type & 0x60) == 0x20) {            /* class */
		switch (req) {
		case 0x20: return 5;            /* SET_LINE_CODING */
		case 0x21: return 6;            /* GET_LINE_CODING */
		case 0x22: return 7;            /* SET_CONTROL_LINE_STATE */
		default:   return 255;
		}
	}
	if ((type & 0x60) != 0) {               /* vendor */
		return 255;
	}
	switch (req) {
	case 5:  return 2;                      /* SET_ADDRESS */
	case 6:  return 1;                      /* GET_DESCRIPTOR */
	case 8:  return 3;                      /* GET_CONFIGURATION */
	case 9:  return 4;                      /* SET_CONFIGURATION */
	case 0:                                 /* GET_STATUS */
	case 1:  return 8;                      /* CLEAR_FEATURE */
	default: return 255;
	}
}

/* --------------------------------------------------------------- helpers */

static inline void
dwc_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(ud.dwc + off) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
dwc_rd(uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(ud.dwc + off);
	__asm__ volatile ("dsb sy" ::: "memory");
	return v;
}

static inline vm_size_t
ud_cache_line(void)
{
	uint64_t ctr;
	__asm__ volatile ("mrs %0, ctr_el0" : "=r"(ctr));
	return (vm_size_t)4 << ((ctr >> 16) & 0xf);
}

/* hand memory to the device: clean + invalidate */
static void
ud_to_dev(uint32_t off, uint32_t len)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	vm_size_t line = ud_cache_line();
	for (vm_offset_t va = (ud.dma + off) & ~(line - 1);
	    va < ud.dma + off + len; va += line) {
		__asm__ volatile ("dc civac, %0" :: "r"(va) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* take memory back from the device: invalidate */
static void
ud_from_dev(uint32_t off, uint32_t len)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	vm_size_t line = ud_cache_line();
	for (vm_offset_t va = (ud.dma + off) & ~(line - 1);
	    va < ud.dma + off + len; va += line) {
		__asm__ volatile ("dc ivac, %0" :: "r"(va) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
ud_sleep_ms(unsigned ms)
{
	delay(ms * 1000);
}

static int
ud_epcmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2)
{
	if (ud.failed || ud.dead) return -3;
	dwc_wr(DEPCMDPAR0(ep), p0);
	dwc_wr(DEPCMDPAR1(ep), p1);
	dwc_wr(DEPCMDPAR2(ep), p2);
	dwc_wr(DEPCMD(ep), cmd | DEPCMD_CMDACT);
	for (unsigned i = 0; i < 1000; i++) {
		uint32_t v = dwc_rd(DEPCMD(ep));
		if (!(v & DEPCMD_CMDACT)) {
			uint32_t status = (v >> 12) & 15;
			return status ? -(int)(16 + status) : 0;
		}
		delay(1);
	}
	/* Command outcome is unknown: retain DMA ownership until a core fence. */
	ud.failed = true;
	kebab_usb_ep0_note(ud.ep0_req, NOTE_FAULT, 1);
	return -1;
}

static void
ud_fault(unsigned status)
{
	kebab_usb_ep0_note(ud.ep0_req, NOTE_FAULT, status);
	ud.failed = true;
}

/*
 * Transmit gate.  No sleeping locks and no recursive acquisition from another
 * CPU.  IRQ masking also prevents a normal scheduler preemption inside a
 * transport transaction.  A debugger stop/FIQ can still strand it; terminal
 * takeover fences that case below.
 *
 * The CPU that already holds the gate may take it again.  That is what makes
 * the panic owner re-entrant: a console write, a probe dump or a debugger
 * hand-back that runs on top of the panic pump's own critical section must not
 * be denied the port it already owns.  Every acquisition is still IRQ-masked,
 * so a nested region cannot be preempted or halted any more than the outer one.
 */
static bool
ud_enter(boolean_t *irq)
{
	*irq = ml_set_interrupts_enabled(FALSE);
	uint32_t cpu = (uint32_t)cpu_number() + 1;
	uint32_t terminal = os_atomic_load(&ud.panic_cpu, acquire);
	if (terminal && terminal != cpu) {
		ml_set_interrupts_enabled(*irq);
		return false;
	}
	if (os_atomic_load(&ud.owner, acquire) == cpu) {
		ud.owner_depth++;
		return true;            /* IRQs stay masked by the outer hold */
	}
	if (!os_atomic_cmpxchg(&ud.owner, 0u, cpu, acquire)) {
		ml_set_interrupts_enabled(*irq);
		return false;
	}
	/* Close the race with a terminal takeover on another CPU. */
	terminal = os_atomic_load(&ud.panic_cpu, acquire);
	if (terminal && terminal != cpu) {
		os_atomic_store(&ud.owner, 0u, release);
		ml_set_interrupts_enabled(*irq);
		return false;
	}
	ud.owner_depth = 1;
	return true;
}

static void
ud_leave(boolean_t irq)
{
	uint32_t cpu = (uint32_t)cpu_number() + 1;
	if (os_atomic_load(&ud.owner, acquire) != cpu) return;
	if (ud.owner_depth > 1) {
		ud.owner_depth--;
		return;
	}
	ud.owner_depth = 0;
	os_atomic_store(&ud.owner, 0u, release);
	ml_set_interrupts_enabled(irq);
}

/*
 * Receive lock.  Serializes event-buffer dispatch and the bulk-OUT re-arm, and
 * nothing else.  It is not the transmit gate, so:
 *
 *   - a debugger reading through kebab_usb_getc() never waits for the panic
 *     pump, the console flusher or the event thread to release transmission;
 *   - it may be taken from whichever CPU is running the reader;
 *   - it never fences a CPU out because of ud.panic_cpu, because receiving is
 *     safe from any context that can run at all: it only consumes events and
 *     re-arms the OUT endpoint, and the lock makes that one at a time.
 *
 * It is re-entrant for the CPU that holds it, so a receive path entered from
 * inside a transmit-critical section (the panic pump polling, then a debugger
 * hand-back on the same stack) cannot deadlock against itself.  The ordering
 * rule is one-way: receive may be taken while holding transmit, never the
 * reverse, so the two locks cannot form a cycle.
 */
static bool
ud_rx_enter(void)
{
	boolean_t irq = ml_set_interrupts_enabled(FALSE);
	uint32_t cpu = (uint32_t)cpu_number() + 1;

	if (os_atomic_load(&ud.rx_owner, acquire) == cpu) {
		ud.rx_depth++;
		return true;            /* IRQs stay masked by the outer hold */
	}
	if (!os_atomic_cmpxchg(&ud.rx_owner, 0u, cpu, acquire)) {
		ml_set_interrupts_enabled(irq);
		return false;
	}
	ud.rx_irq = irq;
	ud.rx_depth = 1;
	return true;
}

static void
ud_rx_leave(void)
{
	boolean_t irq;
	uint32_t cpu = (uint32_t)cpu_number() + 1;

	if (os_atomic_load(&ud.rx_owner, acquire) != cpu) return;
	if (ud.rx_depth > 1) {
		ud.rx_depth--;
		return;
	}
	irq = ud.rx_irq;
	ud.rx_depth = 0;
	os_atomic_store(&ud.rx_owner, 0u, release);
	ml_set_interrupts_enabled(irq);
}

/* submit one TRB; returns the endpoint command result */
static int
ud_start(uint32_t trb_off, unsigned ep, uint32_t buf_off, uint32_t len, uint32_t ctl)
{
	struct ud_endpoint *e = &ud.ep[ep];
	if (ud.failed || ud.dead || e->busy || e->ending || e->halted) return -3;
	if (buf_off > UD_SIZE || len > UD_SIZE - buf_off || trb_off + 32 > UD_SIZE)
		return -4;
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + trb_off);
	uint64_t bpa = ud.dma_pa + buf_off, tpa = ud.dma_pa + trb_off;
	bool zlp = ep == EP0_IN && ctl == TRBCTL_DATA && e->zlp;
	t[0] = (uint32_t)bpa;
	t[1] = (uint32_t)(bpa >> 32);
	t[2] = len;
	t[3] = ctl | TRB_HWO | TRB_ISP_IMI |
	    (zlp ? TRB_CHN : (TRB_LST | TRB_IOC));
	if (zlp) {
		/* The second TRB fits within this endpoint's private cache line. */
		t[4] = (uint32_t)bpa; t[5] = (uint32_t)(bpa >> 32); t[6] = 0;
		t[7] = ctl | TRB_HWO | TRB_ISP_IMI | TRB_LST | TRB_IOC;
	}
	if (len) ud_to_dev(buf_off, len);
	ud_to_dev(trb_off, zlp ? 32 : 16);
	e->trb = trb_off; e->length = len; e->ctl = ctl; e->zlp = zlp;
	e->busy = true; /* Covers a STARTTRANSFER whose command times out. */
	int rc = ud_epcmd(ep, DEPCMD_STARTTRANSFER,
	    (uint32_t)(tpa >> 32), (uint32_t)tpa, 0);
	if (!rc) e->resource = (dwc_rd(DEPCMD(ep)) >> 16) & 0x7f;
	else if (!ud.failed) e->busy = false; /* definitive rejection, no DMA */
	return rc;
}

static void
ud_end(unsigned ep)
{
	struct ud_endpoint *e = &ud.ep[ep];
	if (!e->busy || e->ending || ud.failed) return;
	e->ending = true;
	e->end_polls = 0;
	/* Resource zero is valid for EP0 OUT. Never infer idle from its index. */
	/* Do not poll CMDACT here: a SETUP waiting in the RX FIFO can hold
	 * ENDTRANSFER pending until we arm EP0. Keep consuming events meanwhile.
	 * No other command or transfer may use this endpoint until EPCMDCMPLT.
	 * Unlike non-control endpoints, EP0 is ended without ForceRM (ep0.c). */
	dwc_wr(DEPCMDPAR0(ep), 0);
	dwc_wr(DEPCMDPAR1(ep), 0);
	dwc_wr(DEPCMDPAR2(ep), 0);
	dwc_wr(DEPCMD(ep), DEPCMD_ENDTRANSFER | DEPCMD_CMDIOC | DEPCMD_CMDACT |
	    (ep > EP0_IN ? DEPCMD_FORCERM : 0) | (e->resource << 16));
	kebab_usb_ep0_note(ud.ep0_req, NOTE_END, 0);
}


static void
ud_start_setup(void)
{
	ud.state = EP0_SETUP;
	ud.status_ready = ud.setup_pending = ud.set_line = false;
	ud.line_valid = ud.controls_valid = false;
	int rc = ud_start(UD_TRB_OUT, EP0_OUT, UD_SETUP, 8, TRBCTL_SETUP);
	kebab_usb_ep0_note(ud.ep0_req, 7, (uint32_t)-rc);
	if (rc) ud_fault((unsigned)-rc);
}

static void
ud_abort(unsigned status)
{
	kebab_usb_ep0_note(ud.ep0_req, NOTE_ABORT, status);
	ud_end(EP0_OUT);
	ud_end(EP0_IN);
	ud.state = EP0_ABORT;
	ud.restart = true;
	ud.status_ready = ud.set_line = ud.line_valid = ud.controls_valid = false;
	if (ud.config_pending >= 0) ud.config_pending = 0;
	ud.halt_pending = -1;
}

/* ----------------------------------------------------------- descriptors */

/* VID 0x1209 / PID 0x0001 is the pid.codes test pool, not a product. */
static const uint8_t ud_dev_desc[] = {
	18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
	0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 3, 1,
};

/*
 * One configuration, two interfaces, 67 bytes:
 *   CDC control: interrupt IN 0x82 (physical 5)
 *   CDC data:    bulk OUT 0x01 (physical 2), bulk IN 0x81 (physical 3)
 */
static const uint8_t ud_cfg_desc[] = {
	/* configuration: 2 interfaces, 500 mA, bus powered */
	9, 2, 67, 0, 2, 1, 0, 0x80, 250,
	/* interface 0: CDC control */
	9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,
	/* header functional: CDC 1.10 */
	5, 0x24, 0x00, 0x10, 0x01,
	/* call management: no data class interface, no device call mgmt */
	5, 0x24, 0x01, 0x00, 0x01,
	/* abstract control management: line coding + control line state */
	4, 0x24, 0x02, 0x02,
	/* union: control interface 0, data interface 1 */
	5, 0x24, 0x06, 0x00, 0x01,
	/* SERIAL_STATE: 8-byte header + 2-byte UART state, one packet. */
	7, 5, 0x82, 0x03, 10, 0, 9,
	/* interface 1: CDC data */
	9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
	/* bulk OUT */
	7, 5, 0x01, 0x02, 0x00, 0x02, 0,
	/* bulk IN */
	7, 5, 0x81, 0x02, 0x00, 0x02, 0,
};

static const char *const ud_strings[] = {
	NULL, "OnePlus 8T", "UniXNU kebab console", "0001",
};

static uint32_t
ud_string(unsigned idx, uint8_t *out)
{
	if (idx == 0) {
		out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04;
		return 4;
	}
	if (idx >= sizeof(ud_strings) / sizeof(ud_strings[0]) || ud_strings[idx] == NULL) {
		return 0;
	}
	const char *str = ud_strings[idx];
	uint32_t n = 2;
	for (; *str && n < 126; str++) {
		out[n++] = (uint8_t)*str;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n;
	out[1] = 3;
	return n;
}

/* --------------------------------------------------------- endpoint setup */

static int
ud_ep_config(unsigned ep, uint32_t type, uint32_t maxpkt)
{
	uint32_t fifo = (ep & 1) ? ep >> 1 : 0;
	uint32_t p1 = DEPCFG_PARAM1(ep);
	/* bInterval minus one, clamped to 13 as in DWC3 gadget.c. */
	if (ep == EP_NOTIFY) p1 |= (ud.mps == 512 ? 8u : 13u) << 16;
	int rc = ud_epcmd(ep, DEPCMD_SETEPCONFIG,
	    DEPCFG_PARAM0(type, maxpkt, fifo), p1, 0);
	return rc ? rc : ud_epcmd(ep, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0);
}


static void
ud_reconfigure(unsigned config)
{
	ud.configured = false;
	ud.config_pending = (int)config;
	for (unsigned ep = 2; ep < 6; ep++)
		if (DATA_EP_MASK & (1u << ep)) ud_end(ep);
	kebab_usb_ep0_note(ud.ep0_req, NOTE_CONFIG, 3); /* pending DMA fences */
}

/* A USB reset does not erase endpoint resources. Preserve a live SETUP TRB;
 * end other active phases by resource index, then restore EP0 with MODIFY.
 * DEPSTARTCFG(0) is reserved for initialization after a device-core reset. */
static void
ud_bus_transition(bool reset)
{
	kebab_usb_ep0_note(0, reset ? 16 : NOTE_DISCONNECT, 0);
	if (reset) ud.resets++;
	else ud.disconnects++;
	ud.connected = reset;
	ud.bus_pending = reset ? 1 : 2;
	ud.configured = false;
	ud.config_pending = ud.halt_pending = -1;
	ud.setup_received = false;
	ud.status_ready = ud.line_valid = ud.controls_valid = ud.set_line = false;
	ud.setup_pending = false;
	ud.control_lines = 0;
	ud.notify_pending = false;
	if (ud.state != EP0_SETUP) {
		ud_abort(0);
	}
	/* ud_service first restores SETUP, then ends the data endpoints.
	 * A pending SETUP must be allowed to DMA before their ENDTRANSFERs. */
	dwc_wr(DCFG, dwc_rd(DCFG) & ~DCFG_ADDR_MASK);
}

/* ------------------------------------------------------------- log ring */

/*
 * The console hook can run from interrupt context, where taking a mutex would
 * assert. This is a bounded, non-blocking handoff: whoever cannot take the flag
 * drops its bytes instead of waiting.
 */
/*
 * Push the ring out synchronously, waiting for each transfer to complete.
 *
 * A panic stops the event thread, so nothing else will re-arm the endpoint, and a
 * bare submit only moves one chunk - not enough for a multi-kilobyte panic. This
 * waits for the completion event after every chunk so the whole ring drains.
 */
static void
ud_flush_from_panic(void)
{
	/* Caller owns transmission. Keep every event, including EP0 and resets.
	 * A host that stops reading leaves its original bulk TRB owned by DWC3.
	 * The drain itself runs under the receive lock, which is separate, so a
	 * debugger can still read while this loop holds transmission. */
	if (!ud_rx_enter()) return;
	for (unsigned i = 0; i < 300; i++) {
		ud_poll_locked();
		if (ud.dead || (!ud.ep[EP_BULK_IN].busy &&
		    !ud.tx_pending && ud.log_head == ud.log_tail)) break;
		delay(100);
	}
	ud_rx_leave();
}

void
kebab_usb_log(const char *buf, size_t len)
{
	/* Capture into the kernel ring so a later crash can still be read. */
	extern void kebab_klog_put(const char *buf, size_t len);

	kebab_klog_put(buf, len);
	if (!ud.running || !buf || !len) return;
	/* Serialize producers without waiting on a preempted console writer. */
	boolean_t irq = ml_set_interrupts_enabled(FALSE);
	uint32_t cpu = (uint32_t)cpu_number() + 1;
	if (!os_atomic_cmpxchg(&ud.log_busy, 0u, cpu, acquire)) {
		ml_set_interrupts_enabled(irq);
		return;
	}
	for (size_t i = 0; i < len; i++) {
		uint32_t head = os_atomic_load(&ud.log_head, relaxed);
		uint32_t next = (head + 1) % LOG_RING_SIZE;
		if (next == os_atomic_load(&ud.log_tail, acquire)) break;
		ud.log_ring[head] = buf[i];
		os_atomic_store(&ud.log_head, next, release);
	}
	os_atomic_store(&ud.log_busy, 0u, release);
	ml_set_interrupts_enabled(irq);
	if (panic_active()) kebab_usb_log_flush();
}

/*
 * Drain the ring now instead of leaving it to the event thread.
 *
 * KDP runs with every other CPU stopped: a debugger entry parks them, and a
 * panic stops the event thread outright.  A reply queued by kebab_usb_log()
 * would therefore never leave the device.  The KDP path buffers a whole frame
 * and calls this once per frame, so the cost is one synchronous drain per
 * packet instead of one per byte; normal console output is untouched.
 */
void
kebab_usb_log_flush(void)
{
	boolean_t irq;
	if (!ud.running || !ud_enter(&irq)) return;
	ud_flush_from_panic();
	ud_leave(irq);
}

static uint32_t
ud_log_take(char *out, uint32_t max)
{
	uint32_t n = 0, tail = os_atomic_load(&ud.log_tail, relaxed);
	while (n < max && tail != os_atomic_load(&ud.log_head, acquire)) {
		out[n++] = ud.log_ring[tail];
		tail = (tail + 1) % LOG_RING_SIZE;
	}
	os_atomic_store(&ud.log_tail, tail, release);
	return n;
}

/* ------------------------------------------------------- control requests */


static uint16_t
ud_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static int
ud_endpoint(uint16_t index)
{
	if (index == 0 || index == 0x80) return 0;
	if (!ud.configured) return -1;
	if (index == 0x01) return EP_BULK_OUT;
	if (index == 0x81) return EP_BULK_IN;
	if (index == 0x82) return EP_NOTIFY;
	return -1;
}

static void
ud_status_stage(bool three)
{
	ud.ep0_status3 = three;
	ud.state = EP0_WAIT_STATUS;
	ud.status_ready = false;
	kebab_usb_ep0_note(ud.ep0_req, NOTE_WAIT_STATUS, 0);
}

static void
ud_setup(void)
{
	const uint8_t *st = (const uint8_t *)(ud.dma + UD_SETUP);
	uint8_t *data = (uint8_t *)(ud.dma + UD_DATA);
	uint8_t type = st[0], req = st[1];
	uint16_t value = ud_le16(st + 2), index = ud_le16(st + 4);
	uint16_t length = ud_le16(st + 6);
	uint32_t n = 0;
	int ep = ud_endpoint(index);
	bool ok = true;

	ud.ep0_req = kebab_usb_ep0_request(type, req);
	kebab_usb_ep0_note(ud.ep0_req, 1, 0);
	ud.data_in = (type & 0x80) != 0;
	ud.ep0_status3 = length != 0;
	ud.status_ready = ud.setup_pending = ud.set_line = false;
	ud.line_valid = ud.controls_valid = false;

	if (type == 0x21 && req == 0x20 && ud.configured &&
	    !index && !value && length == 7) {
		ud.set_line = true;
	} else if (type == 0xa1 && req == 0x21 && ud.configured && !index && !value) {
		memcpy(data, ud.line, 7); n = 7;
	} else if (type == 0x21 && req == 0x22 && ud.configured &&
	    !index && !length && !(value & ~3u)) {
		ud.next_controls = value;
		ud.controls_valid = true;
	} else if (type == 0 && req == 5 && !index && !length && value < 128 &&
	    !ud.configured) {
		/* DWC3 switches to the new address after the status handshake. */
		dwc_wr(DCFG, (dwc_rd(DCFG) & ~DCFG_ADDR_MASK) | DCFG_ADDR(value));
	} else if (type == 0x80 && req == 6) {
		unsigned dtype = value >> 8, idx = value & 255;
		if (dtype == 1 && !idx && !index) {
			memcpy(data, ud_dev_desc, sizeof(ud_dev_desc)); n = sizeof(ud_dev_desc);
		} else if ((dtype == 2 || dtype == 7) && !idx && !index) {
			unsigned mps = dtype == 7 ? (ud.mps == 512 ? 64 : 512) : ud.mps;
			memcpy(data, ud_cfg_desc, sizeof(ud_cfg_desc)); n = sizeof(ud_cfg_desc);
			data[1] = (uint8_t)dtype;
			data[43] = mps == 512 ? 9 : 32;
			data[57] = data[64] = (uint8_t)mps;
			data[58] = data[65] = (uint8_t)(mps >> 8);
		} else if (dtype == 6 && !idx && !index) {
			const uint8_t qual[] = { 10, 6, 0, 2, 2, 0, 0, 64, 1, 0 };
			memcpy(data, qual, sizeof(qual)); n = sizeof(qual);
		} else if (dtype == 3 && ((!idx && !index) || index == 0x0409)) {
			n = ud_string(idx, data);
		}
		ok = n != 0;
	} else if (type == 0x80 && req == 8 && !value && !index && length == 1) {
		data[0] = ud.configured ? 1 : 0; n = 1;
	} else if (type == 0 && req == 9 && !index && !length && value <= 1) {
		ud_reconfigure(value);
	} else if (type == 0x81 && req == 10 && ud.configured &&
	    index < 2 && !value && length == 1) {
		data[0] = 0; n = 1;
	} else if (type == 1 && req == 11 && ud.configured &&
	    index < 2 && !value && !length) {
		ud_reconfigure(1); /* only alternate setting zero exists */
	} else if (req == 0 && !value && length == 2 &&
	    ((type == 0x80 && !index) || (type == 0x81 && ud.configured && index < 2) ||
	    (type == 0x82 && ep >= 0))) {
		data[0] = (type == 0x82 && ep > 1 && ud.ep[ep].halted) ? 1 : 0;
		data[1] = 0; n = 2;
	} else if (type == 2 && (req == 1 || req == 3) &&
	    !value && !length && ep > 1) {
		ud.halt_pending = ep;
		ud.halt_set = req == 3;
		ud_end((unsigned)ep);
		kebab_usb_ep0_note(ud.ep0_req, NOTE_HALT, 3);
	} else {
		ok = false;
	}
	if (!ok) { ud_abort(0); return; }
	if (!length) { ud_status_stage(false); return; }
	ud.state = EP0_DATA;
	unsigned dir = ud.data_in ? EP0_IN : EP0_OUT;
	unsigned count = ud.data_in ? MIN(n, length) : 64;
	/* Terminate a short IN reply with a ZLP only at an exact MPS boundary. */
	ud.ep[dir].zlp = ud.data_in && count && count < length && !(count & 63);
	int rc = ud_start(ud.data_in ? UD_TRB_IN : UD_TRB_OUT_DATA,
	    dir, UD_DATA, count, TRBCTL_DATA);
	kebab_usb_ep0_note(ud.ep0_req, 2, (uint32_t)-rc);
	if (rc) ud_abort((unsigned)-rc);
}

static bool
ud_data_idle(void)
{
	for (unsigned ep = 2; ep < 6; ep++)
		if ((DATA_EP_MASK & (1u << ep)) && (ud.ep[ep].busy || ud.ep[ep].ending))
			return false;
	return true;
}

static int
ud_clear_data_stalls(void)
{
	for (unsigned ep = 2; ep < 6; ep++) {
		if (!(DATA_EP_MASK & (1u << ep)) || !ud.ep[ep].halted) continue;
		int rc = ud_epcmd(ep, DEPCMD_CLEARSTALL, 0, 0, 0);
		kebab_usb_ep0_note(ud.ep0_req, NOTE_HALT, (uint32_t)-rc);
		if (rc) return rc;
		ud.ep[ep].halted = false;
	}
	return 0;
}

static int
ud_ep0_modify(void)
{
	for (unsigned ep = 0; ep < 2; ep++) {
		int rc = ud_epcmd(ep, DEPCMD_SETEPCONFIG,
		    DEPCFG_PARAM0(EP_TYPE_CONTROL, 64, 0) | (2u << 30),
		    DEPCFG_PARAM1(ep), 0);
		if (rc) return rc;
	}
	return 0;
}

static void
ud_service(void)
{
	if (ud.failed || ud.dead) return;
	if (ud.restart) {
		if (ud.ep[0].busy || ud.ep[1].busy) return;
		int rc = ud_epcmd(EP0_OUT, DEPCMD_SETSTALL, 0, 0, 0);
		kebab_usb_ep0_note(ud.ep0_req, NOTE_STALL, (uint32_t)-rc);
		if (rc) { ud_fault((unsigned)-rc); return; }
		ud.restart = false;
		/* EP0 STALL clears in hardware on the next SETUP token. Do not
		 * issue CLEARSTALL to control endpoints (Linux gadget.c). */
		ud_start_setup();
	}
	if (ud.connect_pending) {
		if (ud.ep[0].ending || ud.ep[1].ending) return;
		int rc = ud_ep0_modify();
		kebab_usb_ep0_note(0, NOTE_CONNECT, (uint32_t)-rc);
		if (rc) { ud_fault((unsigned)-rc); return; }
		ud.connect_pending = false;
	}
	if (ud.bus_pending) {
		if (!ud.ep[EP0_OUT].busy && !ud.setup_received) ud_start_setup();
		if (ud.failed) return;
		for (unsigned ep = 2; ep < 6; ep++)
			if (DATA_EP_MASK & (1u << ep)) ud_end(ep);
		if (!ud_data_idle()) return;
		int rc = ud_clear_data_stalls();
		if (!rc) rc = ud_ep0_modify();
		kebab_usb_ep0_note(0, 17, (uint32_t)-rc);
		if (rc) { ud_fault((unsigned)-rc); return; }
		dwc_wr(DALEPENA, 3);
		ud.bus_pending = 0;
		/* A SETUP completing during the data-endpoint fences is retained. */
		if (ud.setup_received) {
			ud.setup_received = false;
			ud_setup();
		} else if (!ud.ep[EP0_OUT].busy) ud_start_setup();
	}
	if (ud.config_pending >= 0) {
		if (!ud_data_idle()) return;
		unsigned config = (unsigned)ud.config_pending;
		int rc = ud_clear_data_stalls();
		if (rc) { ud_fault((unsigned)-rc); return; }
		dwc_wr(DALEPENA, 3); /* EP0 stays enabled even for configuration zero */
		if (config) {
			rc = ud_epcmd(EP0_OUT, DEPCMD_DEPSTARTCFG | (2u << 16), 0, 0, 0);
			if (!rc) rc = ud_ep_config(EP_BULK_OUT, EP_TYPE_BULK, ud.mps);
			if (!rc) rc = ud_ep_config(EP_BULK_IN, EP_TYPE_BULK, ud.mps);
			if (!rc) rc = ud_ep_config(EP_NOTIFY, EP_TYPE_INTR, 10);
		}
		kebab_usb_ep0_note(ud.ep0_req, NOTE_CONFIG, (uint32_t)-rc);
		if (rc) { ud_fault((unsigned)-rc); return; }
		if (config) dwc_wr(DALEPENA, EP_MASK);
		ud.config_pending = -1;
		ud.configured = config != 0;
		ud.control_lines = 0;
		ud.notify_pending = config != 0;
	}
	if (ud.halt_pending >= 0) {
		unsigned ep = (unsigned)ud.halt_pending;
		if (ud.ep[ep].busy || ud.ep[ep].ending) return;
		int rc = ud_epcmd(ep, ud.halt_set ? DEPCMD_SETSTALL : DEPCMD_CLEARSTALL, 0, 0, 0);
		kebab_usb_ep0_note(ud.ep0_req, NOTE_HALT, (uint32_t)-rc);
		if (rc) { ud_abort((unsigned)-rc); return; }
		ud.ep[ep].halted = ud.halt_set;
		ud.halt_pending = -1;
		if (ep == EP_NOTIFY && !ud.halt_set) ud.notify_pending = true;
	}
	if (ud.status_ready && ud.state == EP0_WAIT_STATUS) {
		unsigned ep = ud.ep0_status3 && ud.data_in ? EP0_OUT : EP0_IN;
		int rc = ud_start(ep ? UD_TRB_IN : UD_TRB_OUT, ep, UD_DATA, 0,
		    ud.ep0_status3 ? TRBCTL_STATUS3 : TRBCTL_STATUS2);
		kebab_usb_ep0_note(ud.ep0_req, 5, (uint32_t)-rc);
		if (rc) { ud_abort((unsigned)-rc); return; }
		ud.state = EP0_STATUS;
		ud.status_ready = false;
	}
	if (!ud.configured || !ud.connected || ud.failed) return;
	/*
	 * One arm at a time.  The endpoint is left unarmed while a chunk is being
	 * written (so a host that ignores the protocol is NAKed instead of
	 * overwriting the buffer) and while a command that may change the mode is
	 * still waiting for the event thread (so the next arm already has the
	 * right buffer and length).
	 */
	if (!ud.ep[EP_BULK_OUT].busy && !ud.ep[EP_BULK_OUT].halted &&
	    !ud.inst_pending && !ud.vol_pending && ud.reboot_command == 0) {
		bool stream = ud.inst_stream || ud.vol_stream;
		uint32_t off = stream ? UD_INST : UD_BULKOUT;
		uint32_t len = stream ? KEBAB_INST_CHUNK : ud.mps;
		int rc = ud_start(UD_TRB_BULKOUT, EP_BULK_OUT, off, len, TRBCTL_NORMAL);
		if (rc) { ud_fault((unsigned)-rc); return; }
		ud.out_off = off;
		ud.out_stream = ud.vol_stream ? 2u : (ud.inst_stream ? 1u : 0u);
	}
	if (ud.notify_pending && !ud.ep[EP_NOTIFY].busy && !ud.ep[EP_NOTIFY].halted) {
		/* Device-side virtual UART is online: DCD|DSR, independent of host DTR. */
		const uint8_t serial_state[10] = { 0xa1, 0x20, 0, 0, 0, 0, 2, 0, 3, 0 };
		memcpy((void *)(ud.dma + UD_NOTIFY), serial_state, sizeof(serial_state));
		int rc = ud_start(UD_TRB_NOTIFY, EP_NOTIFY, UD_NOTIFY, sizeof(serial_state), TRBCTL_NORMAL);
		kebab_usb_ep0_note(ud.ep0_req, NOTE_NOTIFY, (uint32_t)-rc);
		if (rc) { ud_fault((unsigned)-rc); return; }
		ud.notify_pending = false;
	}
}

/* ------------------------------------------------------ recovery commands */

/*
 * A command only fires when all of its bytes arrive in one bulk-OUT transfer:
 * the caller passes exactly the bytes of this completion, and the match is
 * made inside that window.
 */
static bool
ud_command_seen(const char *buf, uint32_t received, const char *command,
    uint32_t length)
{
	if (received < length) return false;
	for (uint32_t i = 0; i + length <= received; i++) {
		if (!memcmp(buf + i, command, length)) return true;
	}
	return false;
}

/*
 * INST commands carry an argument, so ud_command_seen() is not enough: the
 * number or the path has to be read out of the same completion the command
 * arrived in, before the buffer is re-armed.  Everything is copied into bounded
 * storage here; the strings never reach the filesystem directly.
 */
static bool
ud_inst_u64(const char *p, uint32_t n, uint64_t *out)
{
	uint64_t v = 0;
	uint32_t i = 0;

	if (n == 0 || p[0] < '0' || p[0] > '9') return false;
	while (i < n && p[i] >= '0' && p[i] <= '9') {
		if (v > (0xffffffffffffffffull - 9) / 10) return false;
		v = v * 10 + (uint64_t)(p[i] - '0');
		i++;
	}
	if (i >= n || p[i] != '\n') return false;
	*out = v;
	return true;
}

static uint32_t
ud_inst_command(const char *buf, uint32_t received)
{
	for (uint32_t i = 0; i + 4 <= received; i++) {
		const char *p = buf + i;
		uint32_t n = received - i;
		uint64_t value = 0;

		if (memcmp(p, "INST", 4) != 0) continue;
		if (n >= sizeof("INST?\n") - 1 &&
		    !memcmp(p, "INST?\n", sizeof("INST?\n") - 1)) {
			return UD_REBOOT_INST_STATUS;
		}
		if (n > sizeof("INST BEGIN ") - 1 &&
		    !memcmp(p, "INST BEGIN ", sizeof("INST BEGIN ") - 1) &&
		    ud_inst_u64(p + sizeof("INST BEGIN ") - 1,
		    n - (sizeof("INST BEGIN ") - 1), &value)) {
			ud.inst_wire = value;
			return UD_REBOOT_INST_BEGIN;
		}
		if (n > sizeof("INST DRY ") - 1 &&
		    !memcmp(p, "INST DRY ", sizeof("INST DRY ") - 1) &&
		    ud_inst_u64(p + sizeof("INST DRY ") - 1,
		    n - (sizeof("INST DRY ") - 1), &value)) {
			ud.inst_wire = value;
			return UD_REBOOT_INST_DRY;
		}
		if (n > sizeof("INST CHECK ") - 1 &&
		    !memcmp(p, "INST CHECK ", sizeof("INST CHECK ") - 1)) {
			const char *path = p + sizeof("INST CHECK ") - 1;
			uint32_t len = n - (sizeof("INST CHECK ") - 1);
			for (uint32_t k = 0; k < len; k++) {
				if (path[k] == '\n') { len = k; break; }
			}
			if (len == 0 || len >= KEBAB_INST_PATH_MAX) return 0;
			memcpy(ud.inst_path, path, len);
			ud.inst_path[len] = 0;
			return UD_REBOOT_INST_CHECK;
		}
	}
	return 0;
}

/*
 * VOL commands carry two numbers and a literal confirmation:
 *
 *   VOL BEGIN <wire_bytes> <volume_bytes> CONFIRM
 *   VOL DRY <wire_bytes>
 *   VOL?
 *
 * The confirmation is not decoration: a write replaces the filesystem the
 * kernel is running from, so the operator has to spell out the size and the
 * word.  Everything is parsed out of the same completion, into bounded fields.
 */
static bool
ud_u64_span(const char *p, uint32_t n, uint64_t *out, uint32_t *used)
{
	uint64_t v = 0;
	uint32_t i = 0;

	if (n == 0 || p[0] < '0' || p[0] > '9') {
		return false;
	}
	while (i < n && p[i] >= '0' && p[i] <= '9') {
		if (v > (0xffffffffffffffffull - 9) / 10) {
			return false;
		}
		v = v * 10 + (uint64_t)(p[i] - '0');
		i++;
	}
	*out = v;
	*used = i;
	return true;
}

static uint32_t
ud_vol_command(const char *buf, uint32_t received)
{
	for (uint32_t i = 0; i + 4 <= received; i++) {
		const char *p = buf + i;
		uint32_t n = received - i;
		uint64_t wire = 0, size = 0;
		uint32_t used = 0, off;

		if (memcmp(p, "VOL", 3) != 0) {
			continue;
		}
		if (n >= sizeof("VOL?\n") - 1 &&
		    !memcmp(p, "VOL?\n", sizeof("VOL?\n") - 1)) {
			return UD_REBOOT_VOL_STATUS;
		}
		if (n > sizeof("VOL BEGIN ") - 1 &&
		    !memcmp(p, "VOL BEGIN ", sizeof("VOL BEGIN ") - 1)) {
			off = sizeof("VOL BEGIN ") - 1;
			if (!ud_u64_span(p + off, n - off, &wire, &used)) {
				continue;
			}
			off += used;
			if (off >= n || p[off] != ' ') {
				continue;
			}
			off++;
			if (!ud_u64_span(p + off, n - off, &size, &used)) {
				continue;
			}
			off += used;
			if (off >= n || p[off] != ' ') {
				continue;
			}
			off++;
			if (n - off < sizeof("CONFIRM\n") - 1 ||
			    memcmp(p + off, "CONFIRM\n",
			    sizeof("CONFIRM\n") - 1) != 0) {
				continue;
			}
			ud.vol_wire = wire;
			ud.vol_size = size;
			ud.vol_confirm = true;
			return UD_REBOOT_VOL_BEGIN;
		}
		if (n > sizeof("VOL DRY ") - 1 &&
		    !memcmp(p, "VOL DRY ", sizeof("VOL DRY ") - 1)) {
			off = sizeof("VOL DRY ") - 1;
			if (!ud_u64_span(p + off, n - off, &wire, &used)) {
				continue;
			}
			off += used;
			if (off >= n || p[off] != '\n') {
				continue;
			}
			ud.vol_wire = wire;
			ud.vol_size = 0;
			ud.vol_confirm = false;
			return UD_REBOOT_VOL_DRY;
		}
	}
	return 0;
}

/*
 * While serial KDP owns the byte stream, a KDP frame always starts with 0xFA,
 * so a command anchored at the first byte cannot be confused with debugger
 * traffic.  Without KDP the console is a normal serial port and the bytes may
 * follow earlier typing in the same transfer, so scan the whole completion
 * the way the PANIC! probe does.
 */
static uint32_t
ud_reboot_command(const char *buf, uint32_t received)
{
	if (!kebab_usb_kdp_enabled()) {
		/*
		 * FASTBOOT2 and FASTBOOT share a prefix; the longer command has
		 * to be tested first or its bytes would never reach it.
		 */
		if (ud_command_seen(buf, received, "REBOOT\n", sizeof("REBOOT\n") - 1)) {
			return UD_REBOOT_PLAIN;
		}
		if (ud_command_seen(buf, received, "SYNC\n", sizeof("SYNC\n") - 1)) {
			return UD_REBOOT_SYNC;
		}
		if (ud_command_seen(buf, received, "FASTBOOT2\n", sizeof("FASTBOOT2\n") - 1)) {
			return UD_REBOOT_FASTBOOT2;
		}
		if (ud_command_seen(buf, received, "FASTBOOT\n", sizeof("FASTBOOT\n") - 1)) {
			return UD_REBOOT_FASTBOOT;
		}
		if (ud_command_seen(buf, received, "PSCI?\n", sizeof("PSCI?\n") - 1)) {
			return UD_REBOOT_PSCI_REPORT;
		}
		if (ud_command_seen(buf, received, "PSCINEG?\n", sizeof("PSCINEG?\n") - 1)) {
			return UD_REBOOT_PSCI_NEGATIVE;
		}
		if (ud_command_seen(buf, received, "IMEM?\n", sizeof("IMEM?\n") - 1)) {
			return UD_REBOOT_IMEM_REPORT;
		}
		if (ud_command_seen(buf, received, "SPMI?\n", sizeof("SPMI?\n") - 1)) {
			return UD_REBOOT_SPMI_REPORT;
		}
		if (ud_command_seen(buf, received, "PON?\n", sizeof("PON?\n") - 1)) {
			return UD_REBOOT_PON_REPORT;
		}
		/*
		 * VROMON powers one rail per command: "VROMON ldoc1" must be
		 * tried before the bare form, and anything else that starts
		 * with VROMON is answered with the whitelist instead of being
		 * silently ignored.
		 */
		if (ud_command_seen(buf, received, "VRMON ldoc1\n",
		    sizeof("VRMON ldoc1\n") - 1)) {
			return UD_REBOOT_VRMON_LDOC1;
		}
		if (ud_command_seen(buf, received, "VRMON\n",
		    sizeof("VRMON\n") - 1)) {
			return UD_REBOOT_VRMON_LDOA13;
		}
		if (ud_command_seen(buf, received, "VRMON",
		    sizeof("VRMON") - 1)) {
			return UD_REBOOT_VRMON_OTHER;
		}
		if (ud_command_seen(buf, received, "TCS?\n",
		    sizeof("TCS?\n") - 1)) {
			return UD_REBOOT_TCS_REPORT;
		}
		/* "FWLOG? all\n" must be tried before the bare form. */
		if (ud_command_seen(buf, received, "FWLOG? all\n",
		    sizeof("FWLOG? all\n") - 1)) {
			return UD_REBOOT_FWLOG_ALL;
		}
		if (ud_command_seen(buf, received, "FWLOG?\n",
		    sizeof("FWLOG?\n") - 1)) {
			return UD_REBOOT_FWLOG_REPORT;
		}
		{
			uint32_t inst = ud_inst_command(buf, received);
			if (inst != 0) return inst;
		}
		{
			uint32_t vol = ud_vol_command(buf, received);
			if (vol != 0) return vol;
		}
		return 0;
	}
	if (received >= sizeof("REBOOT\n") - 1 &&
	    !memcmp(buf, "REBOOT\n", sizeof("REBOOT\n") - 1)) {
		return UD_REBOOT_PLAIN;
	}
	if (received >= sizeof("SYNC\n") - 1 &&
	    !memcmp(buf, "SYNC\n", sizeof("SYNC\n") - 1)) {
		return UD_REBOOT_SYNC;
	}
	if (received >= sizeof("FASTBOOT2\n") - 1 &&
	    !memcmp(buf, "FASTBOOT2\n", sizeof("FASTBOOT2\n") - 1)) {
		return UD_REBOOT_FASTBOOT2;
	}
	if (received >= sizeof("FASTBOOT\n") - 1 &&
	    !memcmp(buf, "FASTBOOT\n", sizeof("FASTBOOT\n") - 1)) {
		return UD_REBOOT_FASTBOOT;
	}
	if (received >= sizeof("PSCI?\n") - 1 &&
	    !memcmp(buf, "PSCI?\n", sizeof("PSCI?\n") - 1)) {
		return UD_REBOOT_PSCI_REPORT;
	}
	if (received >= sizeof("PSCINEG?\n") - 1 &&
	    !memcmp(buf, "PSCINEG?\n", sizeof("PSCINEG?\n") - 1)) {
		return UD_REBOOT_PSCI_NEGATIVE;
	}
	if (received >= sizeof("IMEM?\n") - 1 &&
	    !memcmp(buf, "IMEM?\n", sizeof("IMEM?\n") - 1)) {
		return UD_REBOOT_IMEM_REPORT;
	}
	if (received >= sizeof("SPMI?\n") - 1 &&
	    !memcmp(buf, "SPMI?\n", sizeof("SPMI?\n") - 1)) {
		return UD_REBOOT_SPMI_REPORT;
	}
	if (received >= sizeof("PON?\n") - 1 &&
	    !memcmp(buf, "PON?\n", sizeof("PON?\n") - 1)) {
		return UD_REBOOT_PON_REPORT;
	}
	if (received >= sizeof("TOUCH?\n") - 1 &&
	    !memcmp(buf, "TOUCH?\n", sizeof("TOUCH?\n") - 1)) {
		return UD_REBOOT_TOUCH_PROBE;
	}
	if (received >= sizeof("TOUCHINIT\n") - 1 &&
	    !memcmp(buf, "TOUCHINIT\n", sizeof("TOUCHINIT\n") - 1)) {
		return UD_REBOOT_TOUCH_INIT;
	}
	if (received >= sizeof("TOUCHCLK\n") - 1 &&
	    !memcmp(buf, "TOUCHCLK\n", sizeof("TOUCHCLK\n") - 1)) {
		return UD_REBOOT_TOUCH_CLOCK;
	}
	if (received >= sizeof("TOUCHSE\n") - 1 &&
	    !memcmp(buf, "TOUCHSE\n", sizeof("TOUCHSE\n") - 1)) {
		return UD_REBOOT_TOUCH_SE;
	}
	if (received >= sizeof("TOUCHPING\n") - 1 &&
	    !memcmp(buf, "TOUCHPING\n", sizeof("TOUCHPING\n") - 1)) {
		return UD_REBOOT_TOUCH_PING;
	}
	if (received >= sizeof("PWR?\n") - 1 &&
	    !memcmp(buf, "PWR?\n", sizeof("PWR?\n") - 1)) {
		return UD_REBOOT_PWR_REPORT;
	}
	if (received >= sizeof("VRMON ldoc1\n") - 1 &&
	    !memcmp(buf, "VRMON ldoc1\n", sizeof("VRMON ldoc1\n") - 1)) {
		return UD_REBOOT_VRMON_LDOC1;
	}
	if (received >= sizeof("VRMON\n") - 1 &&
	    !memcmp(buf, "VRMON\n", sizeof("VRMON\n") - 1)) {
		return UD_REBOOT_VRMON_LDOA13;
	}
	if (received >= sizeof("VRMON") - 1 &&
	    !memcmp(buf, "VRMON", sizeof("VRMON") - 1)) {
		return UD_REBOOT_VRMON_OTHER;
	}
	if (received >= sizeof("TCS?\n") - 1 &&
	    !memcmp(buf, "TCS?\n", sizeof("TCS?\n") - 1)) {
		return UD_REBOOT_TCS_REPORT;
	}
	if (received >= sizeof("RPMH?\n") - 1 &&
	    !memcmp(buf, "RPMH?\n", sizeof("RPMH?\n") - 1)) {
		return UD_REBOOT_RPMH_REPORT;
	}
	if (received >= sizeof("FWLOG? all\n") - 1 &&
	    !memcmp(buf, "FWLOG? all\n", sizeof("FWLOG? all\n") - 1)) {
		return UD_REBOOT_FWLOG_ALL;
	}
	if (received >= sizeof("FWLOG?\n") - 1 &&
	    !memcmp(buf, "FWLOG?\n", sizeof("FWLOG?\n") - 1)) {
		return UD_REBOOT_FWLOG_REPORT;
	}
	{
		/* The INST family carries arguments, so it has its own scanner.
		 * It has to run in this branch too: kebabkdp=1 only selects the
		 * matching style, and the installer must not depend on it. */
		uint32_t inst = ud_inst_command(buf, received);
		if (inst != 0) return inst;
	}
	{
		uint32_t vol = ud_vol_command(buf, received);
		if (vol != 0) return vol;
	}
	return 0;
}

static uint32_t
ud_fastboot_cookie(void)
{
	uint32_t cookie = KEBAB_FASTBOOT_COOKIE;

	if (PE_parse_boot_argn("kebab_fastboot_cookie", &cookie, sizeof(cookie))) {
		printf("KEBAB FASTBOOT: cookie 0x%08x from kebab_fastboot_cookie\n", cookie);
	} else {
		printf("KEBAB FASTBOOT: cookie 0x%08x (default)\n", cookie);
	}
	return cookie;
}

/*
 * PEHaltRestart() is the platform restart entry point.  KebabPlatformExpert
 * implements kPERestartCPU with the firmware's PSCI SYSTEM_RESET call; this
 * port never installed PE_halt_restart, so the stock IODTPlatformExpert path
 * would otherwise return -1 without resetting anything.
 *
 * The restart is requested only after the transport gate has been released.
 * If the call returns, the machine did not reset: report that instead of
 * claiming success, and disarm the panic-spin bypass.
 */
static int
ud_platform_restart(void)
{
	int rc;

	ud.restart_armed = true;
	bool panicking = panic_active() != 0;
	printf("KEBAB REBOOT: %s\n", panicking ?
	    "panic PSCI SYSTEM_RESET (no platform driver required)" :
	    "PEHaltRestart(kPERestartCPU)");
	/*
	 * The reset only returns on failure, so the lines above have to leave the
	 * device first.  Without this the host sees the port vanish with the
	 * diagnosis still queued in the log ring.
	 */
	kebab_usb_log_flush();
	delay(300000);
	/* Early panics can precede the IOPlatformExpert instance. The SMC
	 * backend needs no service lookup, allocation, scheduler or driver lock. */
	rc = panicking ? (int32_t)kebab_psci_call(KEBAB_PSCI_SYSTEM_RESET, 0, 0) :
	    PEHaltRestart(kPERestartCPU);
	ud.restart_armed = false;
	printf("KEBAB REBOOT: reset returned %d, the machine did not reset\n", rc);
	return rc;
}

/*
 * SYSTEM_RESET2 with reset_type 0 is the architectural warm reset.  It is
 * called only when the corrected PSCI_FEATURES rule says it exists and the
 * negative control proved the probe itself is trustworthy; this is not a
 * vendor-specific reset type.  A return means the firmware refused the call,
 * so the IMEM cookie was never delivered and the caller must not reset again.
 */
static uint64_t
ud_platform_restart2(void)
{
	uint64_t rc;

	printf("KEBAB REBOOT: PSCI SYSTEM_RESET2(reset_type=0, cookie=0)\n");
	/* The reset only returns on failure, so let every line reach the host. */
	kebab_usb_log_flush();
	delay(300000);
	rc = kebab_psci_system_reset2(0, 0);
	printf("KEBAB REBOOT: SYSTEM_RESET2 returned 0x%08x%08x, the machine did not reset\n",
	    (uint32_t)(rc >> 32), (uint32_t)rc);
	return rc;
}

/*
 * Write the PON restart reason and the IMEM cookie, then reset through PSCI
 * SYSTEM_RESET (or SYSTEM_RESET2 when FASTBOOT2 asks for it).  The reset
 * mechanism is deliberately unchanged from the earlier experiments: a
 * different boot outcome can then only come from the newly written reason.
 */
static void
ud_fastboot_restart(bool use_reset2)
{
	volatile uint32_t *reason;
	uint32_t previous, cookie, readback;

	/*
	 * FASTBOOT is the single supported fastboot entry point, and it writes
	 * two things that only make sense together: the PMIC PON reason and the
	 * IMEM cookie.  Both are gated by kebab_pon_write; a boot that did not
	 * ask for fastboot must not have a PMIC write path at all.  REBOOT stays
	 * available for a plain PSCI reset.
	 */
	if (!kebab_spmi_pon_writes_enabled()) {
		printf("KEBAB FASTBOOT: disabled because kebab_pon_write=0; use REBOOT for a plain restart\n");
		return;
	}
	if (ud.imem == 0) {
		printf("KEBAB FASTBOOT: cannot map IMEM at 0x%x; not resetting\n",
		    KEBAB_IMEM_PA);
		return;
	}
	if (use_reset2) {
		if (!ud.psci_reset2_valid) {
			printf("KEBAB FASTBOOT: SYSTEM_RESET2 feature was never probed; not resetting\n");
			return;
		}
		if (!ud.psci_probe_sane) {
			printf("KEBAB FASTBOOT: PSCI probe failed its negative control; not resetting\n");
			return;
		}
		if (ud.psci_reset2 == KEBAB_PSCI_RET_NOT_SUPPORTED) {
			printf("KEBAB FASTBOOT: SYSTEM_RESET2 is not supported (0x%08x); not resetting\n",
			    ud.psci_reset2);
			return;
		}
	}
	/*
	 * Writing the reason without verifying it would make the reset an
	 * uncontrolled experiment: if the masked write fails, the boot that
	 * follows would not be attributable to the PON reason.
	 */
	if ((panic_active() ? kebab_spmi_pon_reason_write() :
	    kebab_spmi_pon_reason_write_normal()) != 0) {
		printf("KEBAB FASTBOOT: PON restart reason was not verified; not resetting\n");
		return;
	}
	reason = (volatile uint32_t *)(ud.imem + KEBAB_IMEM_RESTART);
	previous = *reason;
	cookie = ud_fastboot_cookie();
	printf("KEBAB FASTBOOT: writing IMEM restart_reason 0x%08x -> 0x%08x\n",
	    previous, cookie);
	*reason = cookie;
	readback = *reason;
	printf("KEBAB FASTBOOT: IMEM read back 0x%08x (%s)\n", readback,
	    readback == cookie ? "MATCH" : "MISMATCH");
	if (readback != cookie) {
		*reason = previous;
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("KEBAB FASTBOOT: IMEM cookie was not verified; not resetting\n");
		return;
	}
	/* The cookie has to be in memory before the firmware can read it. */
	__asm__ volatile ("dsb sy" ::: "memory");
	if (use_reset2) {
		(void)ud_platform_restart2();
	} else {
		(void)ud_platform_restart();
	}
	/* PSCI reset must never return, even with zero. Any return is failure. */
	/* A later reset must not inherit an undelivered fastboot request. */
	*reason = previous;
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("KEBAB FASTBOOT: restored restart_reason to 0x%08x\n", previous);
}

/*
 * Read-only probes.  The first answer is the negative control: PSCI_FEATURES
 * must report an impossible function ID as PSCI_RET_NOT_SUPPORTED.  Only if
 * that holds does the rest of the report mean anything.  The two real IDs are
 * printed raw; per the PSCI specification zero means "supported, no feature
 * flags", so SYSTEM_RESET2 is supported unless the answer is 0xffffffff.
 */
static void
ud_psci_report(void)
{
	uint32_t version = kebab_psci_version();
	uint32_t negative = kebab_psci_features(KEBAB_PSCI_NEGATIVE_CONTROL);
	uint32_t reset = kebab_psci_features(KEBAB_PSCI_SYSTEM_RESET);
	uint32_t reset2 = kebab_psci_features(KEBAB_PSCI_RESET2);

	ud.psci_reset2 = reset2;
	ud.psci_reset2_valid = true;
	ud.psci_probe_sane = (negative == KEBAB_PSCI_RET_NOT_SUPPORTED);

	printf("KEBAB PSCI: version 0x%08x\n", version);
	printf("KEBAB PSCI: FEATURES(0x%08x)=0x%08x negative control %s\n",
	    KEBAB_PSCI_NEGATIVE_CONTROL, negative,
	    ud.psci_probe_sane ? "OK" : "BROKEN");
	printf("KEBAB PSCI: FEATURES(0x%08x)=0x%08x SYSTEM_RESET %s\n",
	    KEBAB_PSCI_SYSTEM_RESET, reset,
	    reset != KEBAB_PSCI_RET_NOT_SUPPORTED ? "supported" : "NOT_SUPPORTED");
	printf("KEBAB PSCI: FEATURES(0x%08x)=0x%08x SYSTEM_RESET2 %s\n",
	    KEBAB_PSCI_RESET2, reset2,
	    reset2 != KEBAB_PSCI_RET_NOT_SUPPORTED ? "supported" : "NOT_SUPPORTED");
	if (!ud.psci_probe_sane) {
		printf("KEBAB PSCI: negative control did not return 0xffffffff; "
		    "the feature answers are not trustworthy and FASTBOOT2 is disabled\n");
	}
}

/*
 * The negative control is also available on its own, so a damaged feature table
 * can be separated from a damaged probe without re-reading the other two.
 */
static void
ud_psci_negative_report(void)
{
	uint32_t negative = kebab_psci_features(KEBAB_PSCI_NEGATIVE_CONTROL);

	printf("KEBAB PSCI: FEATURES(0x%08x)=0x%08x negative control %s\n",
	    KEBAB_PSCI_NEGATIVE_CONTROL, negative,
	    negative == KEBAB_PSCI_RET_NOT_SUPPORTED ? "OK" : "BROKEN");
}

static void
ud_imem_report(void)
{
	volatile uint32_t *reason;

	if (ud.imem == 0) {
		printf("KEBAB IMEM: not mapped\n");
		return;
	}
	reason = (volatile uint32_t *)(ud.imem + KEBAB_IMEM_RESTART);
	printf("KEBAB IMEM: restart_reason at 0x%x+0x%x is 0x%08x\n",
	    KEBAB_IMEM_PA, KEBAB_IMEM_RESTART, *reason);
}

/*
 * Take the stream.  kebab_install_begin() prints INST BEGIN only when it really
 * took it, so the endpoint switches to the chunk buffer only after that: a
 * refused command (gate off, or a stream already running) leaves the console in
 * command mode.
 */
static void
ud_inst_arm(bool dry)
{
	kebab_install_begin(dry, ud.inst_wire);
	ud.inst_stream = kebab_install_active();
}

/* Same contract as ud_inst_arm(): the mode switch happens only when the writer
 * really took the stream. */
static void
ud_vol_arm(bool dry)
{
	kebab_vol_begin(dry, ud.vol_wire, ud.vol_size, ud.vol_confirm);
	ud.vol_stream = kebab_vol_active();
}

/*
 * Console command dispatcher.
 *
 * "panicking" is not a hint: the panic console pump (kebab_usb_panic_spin())
 * calls this with preemption already disabled, inside a panic, on the CPU that
 * panicked.  Nothing reachable from here in that mode may sleep, take a mutex
 * or allocate, because any of the three turns the panic into a nested panic --
 * which is exactly what parked this board with a wedged controller after a
 * userspace panic.
 *
 * The reset commands are the only ones that matter in a panic, and they are
 * degraded to a plain PSCI SYSTEM_RESET: the durable-install flush is skipped
 * on purpose.  VFS_SYNC(MNT_WAIT) sleeps on HFS+/UBC/mount locks and
 * kebab_ufs_sync() takes kebab_ufs_lock before SYNCHRONIZE CACHE, so the flush
 * is the one thing that must not run here; losing a write that was still queued
 * is preferable to never resetting.  Every other command is a diagnostic that
 * reaches a driver lock or allocates, so it is refused with one line and the
 * pump keeps running.
 */
static void
ud_run_reboot_command(uint32_t command, boolean_t panicking)
{
	if (panicking) {
		if (command == UD_REBOOT_FASTBOOT || command == UD_REBOOT_FASTBOOT2 ||
		    command == UD_REBOOT_PLAIN) {
			printf("KEBAB PANIC: reset requested (%s); skipping filesystem sync\n",
			    command == UD_REBOOT_FASTBOOT ? "FASTBOOT" :
			    command == UD_REBOOT_FASTBOOT2 ? "FASTBOOT2" : "REBOOT");
			/* PON and IMEM were mapped during normal USB startup. Their
			 * bounded MMIO paths need no allocation or driver/VFS locks.
			 * Preserve the requested boot target instead of degrading
			 * FASTBOOT to an ordinary restart of the panicking kernel. */
			if (command == UD_REBOOT_PLAIN) {
				(void)ud_platform_restart();
			} else {
				ud_fastboot_restart(command == UD_REBOOT_FASTBOOT2);
			}
			return;
		}
		/*
		 * Reading the firmware ring is the one diagnostic that is safe
		 * here.  kebab_fwlog_attach() already ran at boot, so it is a
		 * no-op, and the report path only copies from the mapped window
		 * and writes to the console - no locks, no allocation, no sleep.
		 * Panicking would otherwise destroy the only record of what the
		 * kernel printed before the fault, because the ring is small and
		 * the panic dump overwrites it.
		 */
		if (command == UD_REBOOT_FWLOG_REPORT || command == UD_REBOOT_FWLOG_ALL) {
			kebab_fwlog_report(command == UD_REBOOT_FWLOG_ALL ? 0 :
			    KEBAB_FWLOG_PANIC_MAX);
			return;
		}
		printf("KEBAB PANIC: command 0x%x refused while panicking; it can "
		    "sleep or take a mutex, and only a reset is safe here\n", command);
		return;
	}
	/*
	 * A console-requested reset is the transition from the install phase to
	 * the next boot.  Make every written byte durable before the PSCI call:
	 * VFS_SYNC flushes the HFS+ metadata/data still in the UBC and
	 * SYNCHRONIZE CACHE flushes the drive's write cache.  This is the
	 * non-panic arm only; the panic pump takes the branch above and never
	 * waits for a filesystem sync.
	 */
	if (command == UD_REBOOT_FASTBOOT || command == UD_REBOOT_FASTBOOT2 ||
	    command == UD_REBOOT_PLAIN) {
		const char *why = command == UD_REBOOT_FASTBOOT ? "FASTBOOT" :
		    command == UD_REBOOT_FASTBOOT2 ? "FASTBOOT2" : "REBOOT";

		if (!kebab_install_sync_for_reset(why)) {
			/* Do not turn a failed flush into an unattributable boot. */
			return;
		}
	}
	if (command == UD_REBOOT_FASTBOOT) {
		ud_fastboot_restart(false);
	} else if (command == UD_REBOOT_FASTBOOT2) {
		ud_fastboot_restart(true);
	} else if (command == UD_REBOOT_PLAIN) {
		(void)ud_platform_restart();
	} else if (command == UD_REBOOT_PSCI_REPORT) {
		ud_psci_report();
	} else if (command == UD_REBOOT_PSCI_NEGATIVE) {
		ud_psci_negative_report();
	} else if (command == UD_REBOOT_IMEM_REPORT) {
		ud_imem_report();
	} else if (command == UD_REBOOT_SPMI_REPORT) {
		kebab_spmi_report();
	} else if (command == UD_REBOOT_PON_REPORT) {
		kebab_spmi_pon_report();
	} else if (command == UD_REBOOT_TOUCH_PROBE) {
		kebab_touch_probe();
	} else if (command == UD_REBOOT_TOUCH_INIT) {
		kebab_touch_init_now();
	} else if (command == UD_REBOOT_TOUCH_CLOCK) {
		kebab_touch_clock_enable();
	} else if (command == UD_REBOOT_TOUCH_SE) {
		kebab_touch_se();
	} else if (command == UD_REBOOT_TOUCH_PING) {
		kebab_touch_ping();
	} else if (command == UD_REBOOT_PWR_REPORT) {
		kebab_spmi_power_report();
	} else if (command == UD_REBOOT_RPMH_REPORT) {
		kebab_rpmh_report();
	} else if (command == UD_REBOOT_VRMON_LDOA13) {
		kebab_spmi_vrm_enable("ldoa13");
	} else if (command == UD_REBOOT_VRMON_LDOC1) {
		kebab_spmi_vrm_enable("ldoc1");
	} else if (command == UD_REBOOT_VRMON_OTHER) {
		/* Outside the whitelist: name the two forms, send nothing. */
		kebab_spmi_vrm_enable(NULL);
	} else if (command == UD_REBOOT_TCS_REPORT) {
		kebab_tcs_report();
	} else if (command == UD_REBOOT_FWLOG_REPORT) {
		kebab_fwlog_report(4096);
	} else if (command == UD_REBOOT_FWLOG_ALL) {
		kebab_fwlog_report(0);
	} else if (command == UD_REBOOT_SYNC) {
		kebab_install_sync_report();
	} else if (command == UD_REBOOT_INST_BEGIN) {
		ud_inst_arm(false);
	} else if (command == UD_REBOOT_INST_DRY) {
		ud_inst_arm(true);
	} else if (command == UD_REBOOT_INST_STATUS) {
		kebab_install_status();
	} else if (command == UD_REBOOT_INST_CHECK) {
		kebab_install_check(ud.inst_path);
	} else if (command == UD_REBOOT_VOL_BEGIN) {
		ud_vol_arm(false);
	} else if (command == UD_REBOOT_VOL_DRY) {
		ud_vol_arm(true);
	} else if (command == UD_REBOOT_VOL_STATUS) {
		kebab_vol_status();
	}
}

/* ---------------------------------------------------------------- events */

static void
ud_bulk_out_done(uint32_t received)
{
	const char *buf = (const char *)(ud.dma + ud.out_off);
	uint32_t command;

	/*
	 * The buffer this completion was armed with decides what the bytes are,
	 * not the current mode: the mode can change while a transfer is in
	 * flight, and reading a command out of the chunk buffer (or the other way
	 * round) would hand the parser rubbish.  Stream bytes bypass the input
	 * ring and the command matcher completely.
	 */
	if (ud.out_off == UD_INST) {
		if (received > KEBAB_INST_CHUNK) { ud_fault(4); return; }
		ud_from_dev(UD_INST, received);
		/*
		 * Which stream this arm belonged to is recorded when the arm is
		 * issued.  Reading the current mode instead would misroute a
		 * chunk that completes after the stream was aborted.
		 */
		if (ud.out_stream == 2u) {
			ud.vol_len = received;
			ud.vol_pending = true;
		} else {
			ud.inst_len = received;
			ud.inst_pending = true;
		}
		return;
	}
	ud_from_dev(UD_BULKOUT, ud.mps);
	/*
	 * The pre-rewrite driver echoed host bytes to printf() here.  This state
	 * machine gives the input ring one owner (kebab_usb_getc()/KDP) and keeps
	 * printf off transport-owned paths, so the bytes are delivered, not echoed:
	 * an echo would re-enter the console log ring while the event poll owns it.
	 */
	for (uint32_t i = 0; i < received; i++) {
		uint32_t next = (ud.in_head + 1) % sizeof(ud.in_ring);
		if (next != ud.in_tail) {
			ud.in_ring[ud.in_head] = buf[i];
			__asm__ volatile ("dmb ishst" ::: "memory");
			ud.in_head = next;
			ud.rx_bytes++;
		} else {
			ud.rx_dropped++;
		}
	}
	if (ud.panic_hook && !panic_active()) {
		for (uint32_t i = 0; i + 6 <= received; i++)
			if (!memcmp(buf + i, "PANIC!", 6)) ud.want_panic = true;
	}
	/* Refresh the 32-entry trace after reopening, without a new kernel build.
	 * Text commands are disabled when serial KDP owns the byte stream. */
	if (panic_active() && !kebab_usb_kdp_enabled()) {
		static const char query[] = "EP0?\n";
		for (uint32_t i = 0; i < received; i++) {
			ud.probe_match = buf[i] == query[ud.probe_match] ? ud.probe_match + 1 :
			    (buf[i] == query[0] ? 1u : 0u);
			if (ud.probe_match == sizeof(query) - 1) {
				ud.probe_match = 0;
				ud.probe_requested = true;
			}
		}
	}
	if (ud.reboot_command == 0) {
		command = ud_reboot_command(buf, received);
		if (command != 0) ud.reboot_command = command;
	}
}

static void
ud_bulk_in_done(void)
{
	ud.tx_bytes += ud.tx_pending;
	ud.tx_pending = 0;
	ud.tx_completed++;
}

static void
ud_event(uint32_t ev)
{
	if (ev & 1) {
		if (ev & 0xfe) { ud_fault(4); return; }
		unsigned type = (ev >> 8) & 15;
		if (type == 0 || type == 1) ud_bus_transition(type == 1);
		else if (type == 2) {
			unsigned speed = dwc_rd(DSTS) & 7;
			if (speed != 0 && speed != 1 && speed != 3) { ud_fault(3); return; }
			ud.mps = speed == 0 ? 512 : 64;
			ud.connected = true;
			ud.connect_pending = true;
			kebab_usb_ep0_note(0, NOTE_CONNECT, 3);
		} else if (type == 9 || type == 11) ud_fault(4);
		return;
	}
	unsigned ep = (ev >> 1) & 31, type = (ev >> 6) & 15, status = (ev >> 12) & 15;
	if (ep >= 6 || !(EP_MASK & (1u << ep))) return;
	struct ud_endpoint *e = &ud.ep[ep];
	if (type == 7 && ((ev >> 24) & 15) == DEPCMD_ENDTRANSFER) {
		/* One outstanding command per endpoint; do not recycle that endpoint
		 * until its ENDTRANSFER completion, even if XferComplete arrived first. */
		if (!e->ending) return;
		if (status) { ud_fault(16 + status); return; }
		ud_from_dev(e->trb, e->zlp ? 32 : 16);
		e->busy = e->ending = false;
		e->resource = 0;
		if (ep == EP_BULK_IN) ud.tx_pending = 0; /* cancelled bytes are ambiguous */
		kebab_usb_ep0_note(ud.ep0_req, NOTE_ENDED, 0);
		return;
	}
	if (e->ending) return;
	if (type == 3 && ep < 2) {
		unsigned phase = status & 3;
		if (phase == 1 && ud.state == EP0_DATA && ep != (unsigned)ud.data_in) {
			ud_abort(5);
		} else if (phase == 2) {
			unsigned expected = ud.ep0_status3 && ud.data_in ? EP0_OUT : EP0_IN;
			bool ready = ud.state == EP0_WAIT_STATUS && ep == expected;
			kebab_usb_ep0_note(ud.ep0_req, 4, ready ? 0 : 5);
			if (ud.state != EP0_WAIT_STATUS) return;
			/* Linux dwc3_ep0_xfernotready: SETUP_PENDING restarts on NRDY,
			 * without committing the aborted data or submitting its status. */
			if (ud.setup_pending || !ready) ud_abort(ready ? 0 : 5);
			else ud.status_ready = true;
		}
		return;
	}
	if (type != 1 || !e->busy) return;
	ud_from_dev(e->trb, e->zlp ? 32 : 16);
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + e->trb);
	unsigned left = t[2] & 0xffffff, trbsts = t[2] >> 28;
	unsigned ctl = e->ctl, length = e->length;
	if ((status & 1) || left > length ||
	    (trbsts != TRBSTS_SETUP_PENDING && (t[3] & TRB_HWO))) {
		ud_fault(4); return;
	}
	/* XferComplete is the resource fence. A chained ZLP can remain HWO on
	 * an aborted control transfer; ownership still ends with this event. */
	e->busy = false;
	e->resource = 0;
	if (ep > 1) {
		if (trbsts) { ud_fault(4); return; }
		if (ep == EP_BULK_IN) {
			if (left) { ud_fault(4); return; }
			ud_bulk_in_done();
		} else if (ep == EP_BULK_OUT && ud.configured) ud_bulk_out_done(length - left);
		else if (ep == EP_NOTIFY) kebab_usb_ep0_note(ud.ep0_req, NOTE_NOTIFY, 0);
		return;
	}
	if (ctl == TRBCTL_SETUP && ep == EP0_OUT) {
		if (trbsts || left) { ud_abort(4); return; }
		ud_from_dev(UD_SETUP, 8);
		/* This also accepts a fresh setup after an aborted request. Never
		 * interpret an old data/status completion as a new SETUP. */
		if (ud.bus_pending) {
			ud.setup_received = true;
			kebab_usb_ep0_note(0, NOTE_DEFER_SETUP, 0);
		}
		else ud_setup();
		return;
	}
	if (ud.bus_pending || ud.restart) return;
	if (ctl == TRBCTL_DATA && ud.state == EP0_DATA && ep == (unsigned)ud.data_in) {
		kebab_usb_ep0_note(ud.ep0_req, 3,
		    trbsts && trbsts != TRBSTS_SETUP_PENDING ? 4 : 0);
		ud_status_stage(true);
		if (trbsts == TRBSTS_SETUP_PENDING) {
			ud.setup_pending = true;
			ud.line_valid = false;
			kebab_usb_ep0_note(ud.ep0_req, NOTE_ABORT, 0);
			return;
		}
		if (trbsts || (ud.data_in && left)) { ud_abort(4); return; }
		if (ud.set_line) {
			if (length - left != 7) { ud_abort(4); return; }
			ud_from_dev(UD_DATA, 64);
			memcpy(ud.next_line, (const void *)(ud.dma + UD_DATA), 7);
			ud.line_valid = true;
		}
	} else if ((ctl == TRBCTL_STATUS2 || ctl == TRBCTL_STATUS3) && ud.state == EP0_STATUS) {
		kebab_usb_ep0_note(ud.ep0_req, 6,
		    trbsts && trbsts != TRBSTS_SETUP_PENDING ? 4 : 0);
		if (trbsts && trbsts != TRBSTS_SETUP_PENDING) { ud_abort(4); return; }
		if (trbsts == TRBSTS_SETUP_PENDING)
			kebab_usb_ep0_note(ud.ep0_req, NOTE_ABORT, 0);
		else {
			if (ud.line_valid) {
				memcpy(ud.line, ud.next_line, 7);
				kebab_usb_ep0_note(ud.ep0_req, NOTE_LINE, 0);
			}
			if (ud.controls_valid) {
				ud.control_lines = ud.next_controls;
				ud.notify_pending = true;
				kebab_usb_ep0_note(ud.ep0_req, NOTE_CONTROLS, 0);
			}
		}
		ud_start_setup();
	} else ud_abort(5);
}

/* ----------------------------------------------------------------- thread */

static void
ud_pump_log(void)
{
	if (!ud.configured || !ud.connected || ud.failed || ud.dead ||
	    ud.ep[EP_BULK_IN].busy || ud.ep[EP_BULK_IN].ending ||
	    ud.ep[EP_BULK_IN].halted || ud.halt_pending == EP_BULK_IN) return;
	if (!ud.tx_pending)
		ud.tx_pending = ud_log_take((char *)(ud.dma + UD_BULKIN), ud.mps - 1);
	if (!ud.tx_pending) return;
	int rc = ud_start(UD_TRB_BULKIN, EP_BULK_IN, UD_BULKIN, ud.tx_pending, TRBCTL_NORMAL);
	if (rc) ud_fault((unsigned)-rc);
}

/*
 * Dispatch the event buffer and service the endpoints.
 *
 * The caller must hold the receive lock (ud_rx_enter).  That is what makes the
 * accounting exact: one consumer at a time, each dispatched four-byte event
 * advances the cursor and acknowledges exactly four bytes, and the bulk OUT
 * endpoint is re-armed by the same context that consumed its completion.
 */
static void
ud_poll_locked(void)
{
	if (ud.dead) return;
	if (ud.failed) { (void)ud_controller_restart(); return; }
	uint32_t count = dwc_rd(GEVNTCOUNT) & 0xffff;
	if (count > UD_EVT_SIZE || (count & 3)) {
		/* Do not acknowledge bytes for which there is no valid cursor. */
		ud_fault(4); return;
	}
	for (uint32_t done = 0; done < count && !ud.failed; done += 4) {
		/* Invalidate each event's line. The device may append events into a
		 * line already read earlier; never clean the event buffer here. */
		ud_from_dev(UD_EVT + ud.evt_pos, 4);
		uint32_t ev = *(volatile uint32_t *)(ud.dma + UD_EVT + ud.evt_pos);
		ud_event(ev);
		ud.evt_pos = (ud.evt_pos + 4) % UD_EVT_SIZE;
		dwc_wr(GEVNTCOUNT, 4); /* exactly this dispatched event, even on error */
		ud.event_acked += 4;
		ud.events++;
	}
	/* Starts/rebuilds are deferred until the whole snapshot is consumed, so
	 * old completions in this batch cannot complete newly allocated data TRBs. */
	ud_service();
	ud_pump_log();
	for (unsigned ep = 0; ep < 6; ep++)
		if (ud.ep[ep].ending && ++ud.ep[ep].end_polls >= 1000) ud_fault(1);
}

static void
ud_thread(void *arg __unused, wait_result_t wr __unused)
{
	for (;;) {
		boolean_t irq;
		bool trigger = false;
		uint32_t command = 0;
		if (ud_enter(&irq)) {
			if (ud_rx_enter()) {
				ud_poll_locked();
				ud_rx_leave();
			}
			trigger = ud.want_panic;
			ud.want_panic = false;
			command = ud.reboot_command;
			ud.reboot_command = 0;
			ud_leave(irq);
		}
		/* Panic/restart only after releasing the transport and acknowledging events. */
		if (trigger) {
			kebab_spmi_test_panic_lock();
			panic("kebab usb console test");
		}
		if (command != 0) ud_run_reboot_command(command, FALSE);
		/*
		 * The installer writes to the root filesystem, which can sleep, so
		 * it runs here with no transport gate held.  The chunk is still in
		 * the arena: the endpoint is not re-armed until inst_pending is
		 * cleared, so nothing can overwrite it under the parser.
		 */
		if (ud.inst_pending) {
			uint32_t len = ud.inst_len;
			ud.inst_pending = false;
			kebab_install_bytes((const void *)(ud.dma + UD_INST), len);
			ud.inst_stream = kebab_install_active();
		}
		/*
		 * The volume writer runs here for the same reason as the
		 * installer: raw UFS commands sleep/poll and must not hold the
		 * transport gate.  vol_pending keeps the endpoint unarmed until
		 * the chunk has been written and acknowledged.
		 */
		if (ud.vol_pending) {
			uint32_t len = ud.vol_len;
			ud.vol_pending = false;
			kebab_vol_bytes((const void *)(ud.dma + UD_INST), len);
			ud.vol_stream = kebab_vol_active();
		}
		kebab_install_poll();
		ud.inst_stream = kebab_install_active();
		kebab_vol_poll();
		ud.vol_stream = kebab_vol_active();
		ud_sleep_ms(1);
	}
}


/* Last resort for an interrupted CPU transaction, lost events, or a command
 * whose outcome is unknown. No TRB, payload, or event memory is reused before
 * BOTH DEVCTRLHLT and CSFTRST completion. This creates a new USB connection;
 * the normal panic handoff and ordinary reopen do not enter this path. */
static bool
ud_controller_restart(void)
{
	ud.fences++;
	uint32_t dcfg = dwc_rd(DCFG) & ~(DCFG_ADDR_MASK | DCFG_SPEED_MASK);
	kebab_usb_ep0_note(0, NOTE_FENCE, 3);
	dwc_wr(DEVTEN, 0);
	dwc_wr(DCTL, dwc_rd(DCTL) & ~DCTL_RUN_STOP);
	unsigned i;
	for (i = 0; i < 1000 && !(dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) delay(100);
	if (!(dwc_rd(DSTS) & DSTS_DEVCTRLHLT)) goto dead;
	dwc_wr(DCTL, DCTL_CSFTRST);
	for (i = 0; i < 1000 && (dwc_rd(DCTL) & DCTL_CSFTRST); i++) delay(100);
	if (dwc_rd(DCTL) & DCTL_CSFTRST) goto dead;
	/* Controller reset, not a software flag, returned all DMA ownership. */
	memset(ud.ep, 0, sizeof(ud.ep));
	ud.failed = false;
	ud.configured = ud.connected = ud.connect_pending = false;
	ud.restart = ud.setup_received = ud.setup_pending = ud.status_ready = false;
	ud.line_valid = ud.controls_valid = ud.notify_pending = false;
	ud.config_pending = ud.halt_pending = -1;
	ud.bus_pending = 0;
	ud.tx_pending = 0;
	ud.control_lines = 0;
	ud.mps = 512;
	dwc_wr(GCTL, (dwc_rd(GCTL) & ~(3u << 12)) | (2u << 12));
	dwc_wr(GUSB2PHYCFG0, dwc_rd(GUSB2PHYCFG0) & ~((1u << 6) | (1u << 8)));
	dwc_wr(DCFG, dcfg);
	dwc_wr(GEVNTADRLO, (uint32_t)(ud.dma_pa + UD_EVT));
	dwc_wr(GEVNTADRHI, (uint32_t)((ud.dma_pa + UD_EVT) >> 32));
	dwc_wr(GEVNTSIZ, (1u << 31) | UD_EVT_SIZE);
	uint32_t discarded = dwc_rd(GEVNTCOUNT) & 0xffff;
	dwc_wr(GEVNTCOUNT, discarded);
	ud.event_discarded += discarded;
	ud.evt_pos = 0; /* only a core reset establishes a new producer cursor */
	int rc = ud_epcmd(EP0_OUT, DEPCMD_DEPSTARTCFG, 0, 0, 0);
	if (!rc) rc = ud_ep_config(EP0_OUT, EP_TYPE_CONTROL, 64);
	if (!rc) rc = ud_ep_config(EP0_IN, EP_TYPE_CONTROL, 64);
	kebab_usb_ep0_note(0, 17, (uint32_t)-rc);
	if (rc) goto dead;
	dwc_wr(DALEPENA, 3);
	dwc_wr(DEVTEN, DEVTEN_DEVICE);
	dwc_wr(DCTL, dwc_rd(DCTL) | DCTL_RUN_STOP);
	for (i = 0; i < 1000 && (dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) delay(100);
	if (dwc_rd(DSTS) & DSTS_DEVCTRLHLT) goto dead;
	ud_start_setup();
	if (ud.failed) goto dead;
	kebab_usb_ep0_note(0, NOTE_FENCE, 0);
	return true;
dead:
	kebab_usb_ep0_note(0, NOTE_FENCE, 1);
	ud.dead = true; /* Retain the DMA arena permanently; never guess ownership. */
	return false;
}

/*
 * Force the transport into a state the panic loop can own.
 *
 * Only called from the non-returning panic loop, after ud.panic_cpu is set.
 * A stopped scheduler is not proof that a DMA transfer or CPU transaction
 * finished, so a foreign owner normally has to be proven stopped before the
 * controller is reset and the gate is taken.
 *
 * That proof does not exist in a panic.  DebuggerHaltOtherCores() parks the
 * other CPUs with a SIGPdebug IPI, not with a DBGWRAP halt, and
 * ml_dbgwrap_cpu_is_halted() only reads the DBGACK bit of the Apple CoreSight
 * UTT window, which this board does not map.  The recorded owner can
 * therefore never be reported halted even though its stack cannot resume.
 * Waiting for that report stranded the gate forever: the panic loop retried
 * every millisecond and never touched the controller again, which is exactly
 * the dead CDC-ACM port.  Once panic_active() is true, fence the controller
 * and take both locks unconditionally; no other CPU is going to release them.
 *
 * The non-panic arm is unreachable from the panic loop and is kept only as a
 * defensive check for a future caller that might run next to live CPUs.
 */
static bool
ud_panic_takeover(void)
{
	uint32_t cpu;
	bool panic;
	uint32_t old;
	uint32_t rx;
	uint32_t writer;
	boolean_t irq;

	cpu = (uint32_t)cpu_number() + 1;
	panic = panic_active() != 0;
	old = os_atomic_load(&ud.owner, acquire);
	rx = os_atomic_load(&ud.rx_owner, acquire);

	if (old || rx) {
		if (!panic) {
			bool stopped;

			stopped = true;
			if (old && old != cpu &&
			    (old > MAX_CPUS ||
			    !ml_dbgwrap_cpu_is_halted((int)old - 1))) {
				stopped = false;
			}
			if (rx && rx != cpu &&
			    (rx > MAX_CPUS ||
			    !ml_dbgwrap_cpu_is_halted((int)rx - 1))) {
				stopped = false;
			}
			if (!stopped) {
				printf("KEBAB USB: console takeover refused: owner %u/%u is not halted\n",
				    old, rx);
				return false;
			}
		}
		/*
		 * A stopped CPU may have left an endpoint command or DMA in flight.
		 * The core reset is the ownership fence: it returns every transfer
		 * before any event memory or TRB is reused.
		 */
		(void)ud_controller_restart();
		os_atomic_store(&ud.owner, 0u, release);
		ud.owner_depth = 0;
		os_atomic_store(&ud.rx_owner, 0u, release);
		ud.rx_depth = 0;
	}
	if (!ud_enter(&irq)) return false;
	for (unsigned ep = 0; ep < 6; ep++)
		if (ud.ep[ep].busy) ud.takeover_active |= 1u << ep;
	/* A producer publishes head after each byte. If its stack was abandoned,
	 * all published bytes are valid; the unpublished slot can be overwritten.
	 * In a panic only this CPU is running, so any recorded producer is dead. */
	writer = os_atomic_load(&ud.log_busy, acquire);
	if (writer && (writer == cpu || panic ||
	    (writer <= MAX_CPUS && ml_dbgwrap_cpu_is_halted((int)writer - 1)))) {
		os_atomic_store(&ud.log_busy, 0u, release);
		kebab_usb_ep0_note(0, NOTE_LOG_TAKEOVER, 0);
	}
	kebab_usb_ep0_note(0, NOTE_TAKEOVER, ud.dead ? 1 : 0);
	/* Keep all active resources on a clean handoff. ud_event completes
	 * them normally, even if the host leaves its IN read pending. */
	ud.probe_requested = true;
	ud_leave(irq);
	return true;
}

/*
 * The USB controller sits behind apps_smmu (stream 0, per kona-usb.dtsi) while
 * the UFS controller does not, which is why UFS DMA works and the first USB
 * transfer kills the board. Read the SMMU out before touching anything: whether
 * it is enabled, what the global bypass says, and whether stream 0 has a
 * mapping. Read-only on purpose.
 */
#define SMMU_PA        0x15000000
#define SMMU_CR0       0x0000
#define SMMU_GBPA      0x0004
#define SMMU_IDR0      0x0020
#define SMMU_IDR1      0x0024
#define SMMU_GFSR      0x0048
#define SMMU_GFSYNR0   0x0050
#define SMMU_GFSYNR1   0x0054
#define SMMU_SMR(n)    (0x0800 + 4 * (n))     /* VALID[31] MASK[30:16] ID[14:0] */
#define SMMU_S2CR(n)   (0x0c00 + 4 * (n))     /* TYPE[17:16]: 0=trans 1=bypass 2=fault */

/* DWC3 records whether the last failure was a bus error, and where. */
#define GSTS            0xc118
#define GBUSERRADDR0    0xc130
#define GBUSERRADDR1    0xc134

static void
ud_bus_error_report(const char *when)
{
	uint32_t gsts = dwc_rd(GSTS);
	if (gsts & (1u << 4)) {
		printf("KEBAB USB: bus error address valid %s: 0x%08x%08x GSTS 0x%08x\n",
		    when, dwc_rd(GBUSERRADDR1), dwc_rd(GBUSERRADDR0), gsts);
	} else {
		printf("KEBAB USB: no bus error %s (GSTS 0x%08x)\n", when, gsts);
	}
}

static void
ud_smmu_report(void)
{
	vm_offset_t smmu = ml_io_map(SMMU_PA, 0x10000);
	if (smmu == 0) {
		printf("KEBAB USB: cannot map the SMMU\n");
		return;
	}
	uint32_t cr0 = *(volatile uint32_t *)(smmu + SMMU_CR0);
	uint32_t idr0 = *(volatile uint32_t *)(smmu + SMMU_IDR0);
	uint32_t idr1 = *(volatile uint32_t *)(smmu + SMMU_IDR1);
	printf("KEBAB USB: SMMU CR0 0x%08x IDR0 0x%08x IDR1 0x%08x\n", cr0, idr0, idr1);
	printf("KEBAB USB: SMMU clientpd %u usfcfg %u numsmrg %u\n",
	    cr0 & 1u, (cr0 >> 10) & 1u, idr0 & 0xffu);
	/* only the first handful: enough to see whether stream 0 is covered */
	for (unsigned i = 0; i < 8; i++) {
		uint32_t smr = *(volatile uint32_t *)(smmu + SMMU_SMR(i));
		uint32_t s2cr = *(volatile uint32_t *)(smmu + SMMU_S2CR(i));
		if (smr == 0 && s2cr == 0) {
			continue;
		}
		printf("KEBAB USB: SMMU[%u] SMR 0x%08x (valid %u id 0x%x mask 0x%x) S2CR 0x%08x (type %u)\n",
		    i, smr, smr >> 31, smr & 0x7fffu, (smr >> 16) & 0x7fffu,
		    s2cr, (s2cr >> 16) & 3u);
	}
}

/* ------------------------------------------------------------------ start */

void
kebab_usb_start(void)
{
	/* Map the firmware log before any later panic could need it. */
	kebab_fwlog_attach_at_init();
	uint32_t enabled = 0;
	thread_t thread;

	/*
	 * Called early from kernel_bootstrap when KDP over this port is requested,
	 * and again from bsd_init for the ordinary console.  Only the first caller
	 * that gets past the boot argument may touch the controller; a second full
	 * bring-up would leak the DMA page and start a second event thread.
	 */
	if (ud.running || ud.start_attempted) {
		return;
	}
	if (!PE_parse_boot_argn("kebabusb", &enabled, sizeof(enabled)) || enabled == 0) {
		return;
	}
	ud.start_attempted = true;
	{
		uint32_t hook = 0;
		ud.panic_hook = PE_parse_boot_argn("kebabpanic", &hook, sizeof(hook)) && hook != 0;
	}
	ud.dwc = ml_io_map(DWC3_PA, DWC3_SIZE);
	if (ud.dwc == 0) {
		printf("KEBAB USB: cannot map the controller\n");
		return;
	}
	ud.qs = ml_io_map(QSCRATCH_PA, 0x100);
	/*
	 * Map IMEM now, in normal context.  The panic loop cannot safely allocate
	 * or take VM locks, and FASTBOOT has to read and write this page there.
	 * A failed mapping is not fatal: the command falls back to REBOOT.
	 */
	ud.imem = ml_io_map(KEBAB_IMEM_PA, KEBAB_IMEM_SIZE);
	if (ud.imem == 0) {
		printf("KEBAB USB: cannot map IMEM at 0x%x; FASTBOOT will fall back\n",
		    KEBAB_IMEM_PA);
	} else {
		/*
		 * This is the earliest point in this kernel at which the boot
		 * reason can be observed.  ABL may have consumed or cleared the
		 * cell already, so a zero here does not prove the value was lost
		 * during reset; it only says nobody can see it any more.
		 */
		printf("KEBAB USB: IMEM mapped at 0x%x (restart_reason +0x%x); boot read 0x%08x\n",
		    KEBAB_IMEM_PA, KEBAB_IMEM_RESTART,
		    *(volatile uint32_t *)(ud.imem + KEBAB_IMEM_RESTART));
	}
	/* Record the firmware's PSCI answers once, early, in normal context. */
	ud.psci_reset2 = KEBAB_PSCI_RET_NOT_SUPPORTED;
	ud.psci_reset2_valid = false;
	ud.psci_probe_sane = false;
	ud_psci_report();
	/*
	 * Map the SPMI arbiter here, in normal context: the console can run from
	 * the panic loop, where ml_io_map cannot allocate.  The probe itself is
	 * read-only and its result is reported by SPMI? / PON?.
	 */
	kebab_spmi_init();
	/* wired pages need a real tag: VM_KERN_MEMORY_NONE trips an assertion */
	if (kmem_alloc_contig(kernel_map, &ud.dma, UD_SIZE, PAGE_MASK,
	    (vm_offset_t)-1, 0, KMA_DATA | KMA_ZERO | KMA_NOFAIL,
	    VM_KERN_MEMORY_BSD) != KERN_SUCCESS) {
		printf("KEBAB USB: no contiguous DMA memory\n");
		return;
	}
	ud.dma_pa = (uint64_t)ml_vtophys(ud.dma);
	if (ud.dma_pa == 0) {
		printf("KEBAB USB: DMA page has no physical backing\n");
		return;
	}
	/*
	 * The chunk path names the whole buffer in one TRB, so the arena has to
	 * be physically contiguous and not merely virtually.  Verify it instead
	 * of assuming it: a split would have the controller write through one
	 * address into pages that belong to something else.
	 */
	for (vm_size_t off = 0; off < UD_SIZE; off += PAGE_SIZE) {
		ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)(ud.dma + off));
		if (pn == 0 || ptoa_64(pn) != ud.dma_pa + off) {
			printf("KEBAB USB: DMA arena is not contiguous at +0x%lx\n",
			    (unsigned long)off);
			return;
		}
	}
	ud.out_off = UD_BULKOUT;
	bzero((void *)ud.dma, UD_SIZE);
	/* hand the whole arena to the device once, before anything else uses it */
	ud_to_dev(0, UD_SIZE);

	/*
	 * The USB2 PHY must not be suspended and LPM must be off before endpoint
	 * commands; dwc3 clears SUSPHY and ENBLSLPM around HS/FS operation and the
	 * SM8250 quirk set keeps both clear.
	 */
	dwc_wr(GUSB2PHYCFG0, dwc_rd(GUSB2PHYCFG0) & ~((1u << 6) | (1u << 8)));

	dwc_wr(DEVTEN, DEVTEN_DEVICE);
	dwc_wr(DCTL, DCTL_CSFTRST);
	int i;
	for (i = 0; i < 500 && (dwc_rd(DCTL) & DCTL_CSFTRST); i++) {
		delay(1000);
	}
	printf("KEBAB USB: reset %s after %d ms, GCTL 0x%08x DCFG 0x%08x GSNPSID 0x%08x\n",
	    (dwc_rd(DCTL) & DCTL_CSFTRST) ? "STUCK" : "done", i,
	    dwc_rd(GCTL), dwc_rd(DCFG), dwc_rd(0xc120));
	if (dwc_rd(DCTL) & DCTL_CSFTRST) {
		return;
	}

	/* device mode, high speed, address 0 */
	dwc_wr(GCTL, (dwc_rd(GCTL) & ~(3u << 12)) | (2u << 12));
	dwc_wr(DCFG, dwc_rd(DCFG) & ~(DCFG_SPEED_MASK | DCFG_ADDR_MASK));

	/*
	 * Number of receive packets, as dwc3_gadget_setup_nump() computes it:
	 * RxFIFO comes from RAM2 depth and the datapath width, and NumP is that
	 * size in KiB, clamped to 16. With NumP left at the reset value the
	 * controller has nothing to receive with.
	 */
	{
		uint32_t hw7 = dwc_rd(GHWPARAMS7);
		uint32_t ram2 = (hw7 >> 16) & 0xffff;
		uint32_t hw0 = dwc_rd(GHWPARAMS0);
		uint32_t mdwidth = (hw0 >> 4) & 0x3f;   /* MDWIDTH in GHWPARAMS0[9:4] */
		uint32_t fifo;
		uint32_t nump;
		if (mdwidth == 0) {
			mdwidth = 32;
		}
		fifo = (ram2 * mdwidth / 8);
		fifo = (fifo > 40) ? (fifo - 40) : 0;
		nump = fifo / 1024;
		if (nump > 16) {
			nump = 16;
		}
		dwc_wr(DCFG, (dwc_rd(DCFG) & ~DCFG_NUMP_MASK) | (nump << DCFG_NUMP_SHIFT));
		printf("KEBAB USB: GHWPARAMS0 0x%08x GHWPARAMS7 0x%08x -> NumP %u\n",
		    hw0, hw7, nump);
	}
	dwc_wr(DEVTEN, DEVTEN_DEVICE);

	dwc_wr(GEVNTADRLO, (uint32_t)(ud.dma_pa + UD_EVT));
	dwc_wr(GEVNTADRHI, (uint32_t)((ud.dma_pa + UD_EVT) >> 32));
	dwc_wr(GEVNTSIZ, (1u << 31) | UD_EVT_SIZE);      /* bit 31 masks the interrupt */
	dwc_wr(GEVNTCOUNT, dwc_rd(GEVNTCOUNT) & 0xffff);
	ud.evt_pos = 0;
	printf("KEBAB USB: event buffer at 0x%llx\n", (unsigned long long)(ud.dma_pa + UD_EVT));

	ud.mps = 512;
	ud.config_pending = ud.halt_pending = -1;
	const uint8_t default_line[7] = { 0x00, 0xc2, 0x01, 0, 0, 0, 8 };
	memcpy(ud.line, default_line, sizeof(default_line));

	/* The controller needs its endpoint resource configuration started once
	 * before any endpoint is configured; without it every STARTTRANSFER comes
	 * back with status 1 (invalid parameter). */
	printf("KEBAB USB: step 1 startcfg\n");
	if (ud_epcmd(EP0_OUT, DEPCMD_DEPSTARTCFG, 0, 0, 0)) {
		printf("KEBAB USB: DEPSTARTCFG failed\n");
		return;
	}
	printf("KEBAB USB: step 2 ep0 out cfg\n");
	if (ud_epcmd(EP0_OUT, DEPCMD_SETEPCONFIG, DEPCFG_PARAM0(EP_TYPE_CONTROL, 64, 0),
	    DEPCFG_PARAM1(EP0_OUT), 0)) {
		printf("KEBAB USB: step 2 failed\n");
		return;
	}
	printf("KEBAB USB: step 3 ep0 out resources\n");
	if (ud_epcmd(EP0_OUT, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		printf("KEBAB USB: step 3 failed\n");
		return;
	}
	printf("KEBAB USB: step 4 ep0 in cfg\n");
	if (ud_epcmd(EP0_IN, DEPCMD_SETEPCONFIG, DEPCFG_PARAM0(EP_TYPE_CONTROL, 64, 0),
	    DEPCFG_PARAM1(EP0_IN), 0)) {
		printf("KEBAB USB: step 4 failed\n");
		return;
	}
	ud_smmu_report();
	printf("KEBAB USB: step 5 ep0 in resources\n");
	if (ud_epcmd(EP0_IN, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		printf("KEBAB USB: step 5 failed\n");
		return;
	}
	dwc_wr(DALEPENA, (1u << EP0_OUT) | (1u << EP0_IN));
	printf("KEBAB USB: step 6 endpoints enabled\n");

	/* tell the controller VBUS is present; the board does not wire OTG here */
	if (ud.qs != 0) {
		uint32_t hs = *(volatile uint32_t *)(ud.qs + QSCRATCH_HS);
		uint32_t ss = *(volatile uint32_t *)(ud.qs + QSCRATCH_SS);
		*(volatile uint32_t *)(ud.qs + QSCRATCH_HS) =
		    hs | UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL;
		*(volatile uint32_t *)(ud.qs + QSCRATCH_SS) = ss | LANE0_PWR_PRESENT;
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("KEBAB USB: VBUS override, HS 0x%08x -> 0x%08x, SS 0x%08x -> 0x%08x\n",
		    hs, *(volatile uint32_t *)(ud.qs + QSCRATCH_HS),
		    ss, *(volatile uint32_t *)(ud.qs + QSCRATCH_SS));
	} else {
		printf("KEBAB USB: QSCRATCH not mapped, no VBUS override\n");
	}

	dwc_wr(DCTL, dwc_rd(DCTL) | DCTL_RUN_STOP);
	for (i = 0; i < 500 && (dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) {
		delay(1000);
	}
	if (dwc_rd(DSTS) & DSTS_DEVCTRLHLT) {
		printf("KEBAB USB: controller did not start (DSTS 0x%08x)\n", dwc_rd(DSTS));
		return;
	}
	printf("KEBAB USB: running (DSTS 0x%08x after %d ms), DMA at 0x%llx\n",
	    dwc_rd(DSTS), i, (unsigned long long)ud.dma_pa);

	/*
	 * Only now, with the controller running, arm the first setup transfer:
	 * dwc3_gadget_start() also sets RUN_STOP before dwc3_ep0_out_start(), and
	 * submitting a transfer to a halted controller is not legal.
	 */
	ud_bus_error_report("before setup");
	printf("KEBAB USB: step 7 arming setup, first DMA at 0x%llx\n",
	    (unsigned long long)(ud.dma_pa + UD_SETUP));
	ud_start_setup();
	printf("KEBAB USB: step 7 done\n");

	if (ud.failed) return;
	ud.running = true;
	if (kernel_thread_start((thread_continue_t)ud_thread, NULL, &thread) != KERN_SUCCESS) {
		ud.running = false;
		printf("KEBAB USB: no event thread, halting\n");
		dwc_wr(DCTL, dwc_rd(DCTL) & ~DCTL_RUN_STOP);
		return;
	}
	thread_deallocate(thread);
	printf("KEBAB USB: event thread started, waiting for the host\n");
}


/*
 * Console input for the kernel debugger. A panic stops the event thread, so this
 * polls the controller itself instead of waiting for it: it drains whatever events
 * are pending and returns the next typed byte, or -1 after a short wait.
 *
 * Receive only: this takes the receive lock and never the transmit gate.  A
 * debugger has to be able to read a request while the panic pump (or the console
 * flusher, or the event thread) owns transmission, and it has to work from
 * whichever CPU is running the debugger, so this path cannot fence itself out
 * the way a transmit-gate attempt does.
 */
int
kebab_usb_getc(bool wait)
{
	if (!ud.running) return -1;
	for (unsigned tries = 0; tries < (wait ? 4000u : 1u); tries++) {
		int c = -1;
		if (ud_rx_enter()) {
			ud.console_input = true;
			ud_poll_locked();
			if (ud.in_tail != ud.in_head) {
				c = (unsigned char)ud.in_ring[ud.in_tail];
				ud.in_tail = (ud.in_tail + 1) % sizeof(ud.in_ring);
				ud.kdp_rx_bytes++;
			}
			ud_rx_leave();
		}
		if (c >= 0) return c;
		if (wait) delay(1000);
	}
	return -1;
}

bool
kebab_usb_console_ready(void)
{
	/* panic_spin_forever's gate must allow an unconfigured/disconnected host
	 * to enumerate later. Configuration is a USB state, not driver readiness. */
	return ud.running && !ud.dead;
}

/*
 * True while the console itself is calling PEHaltRestart().  The platform
 * expert uses this to distinguish a host-requested restart from the panic
 * path's final stop: without it, haltRestart() would call
 * kebab_usb_panic_spin() again and the machine would never reset.
 */
bool
kebab_usb_restart_armed(void)
{
	return ud.restart_armed;
}

/*
 * KDP over this port is opt-in: kebabusb=1 gives a console, kebabkdp=1 says
 * the debugger owns its input and output as well.  Parsed once; this is
 * reachable from the panic path, where PE_parse_boot_argn() is still safe but
 * not free.
 */
bool
kebab_usb_kdp_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0) {
		uint32_t value = 0;

		enabled = (PE_parse_boot_argn("kebabkdp", &value,
		    sizeof(value)) && value != 0);
	}
	return enabled != 0;
}

/*
 * Bring the port up before kdp_init() can trap into the debugger, and stop
 * host input from being echoed into the visible log: once KDP is selected
 * those bytes are frames, not keystrokes.
 */
void
kebab_usb_kdp_start(void)
{
	uint32_t early_console = 0;
	bool kdp = kebab_usb_kdp_enabled();

	if (!kdp && !(PE_parse_boot_argn("kebabusb_early", &early_console,
	    sizeof(early_console)) && early_console != 0)) {
		return;
	}
	kebab_usb_start();
	if (ud.running && kdp) {
		ud.console_input = true;
	}
}

/*
 * True when a host byte is waiting and it looks like the start of a serial KDP
 * frame.  The panic pump uses this to decide whether to hand the stream back
 * to KDP; console input cannot start with 0xfa because the serializer escapes
 * that byte everywhere inside a frame.
 *
 * Scan the whole ring, do not just peek at its head: one stale console byte or
 * an earlier fragment in front of a fresh frame used to keep the pump from ever
 * handing the stream back, which wedged the debugger for the rest of the boot.
 * KDP's own deserializer discards everything before the 0xfa, so hand-back is
 * safe as soon as any frame start is queued.
 */
bool
kebab_usb_kdp_pending(void)
{
	uint32_t head = os_atomic_load(&ud.in_head, acquire);

	if (!ud.running || !ud.configured || head == ud.in_tail) return false;
	for (uint32_t i = ud.in_tail; i != head; i = (i + 1) % sizeof(ud.in_ring)) {
		if ((unsigned char)ud.in_ring[i] == 0xfa) return true;
	}
	return false;
}

/*
 * Count one serialized debugger frame that the PAL layer handed to the log ring.
 * The frame boundary is only known there, so the PAL calls this once per frame:
 * "frames queued" is then an exact number and does not have to be guessed from
 * the byte stream.  It is a plain increment, safe in the panic context.
 */
void
kebab_usb_kdp_tx_frame(uint32_t len)
{
	ud.kdp_tx_frames++;
	ud.kdp_tx_bytes += len;
}

/* ---------------------------------------------------------- EP0 probe I/O */

#define EP0_REPORT_SIZE 2048

static char ep0_report[EP0_REPORT_SIZE];

/* Format the current word and the whole history ring, four entries per line. */
static uint32_t
kebab_usb_ep0_format(void)
{
	uint32_t v = ud.ep0_progress;
	uint32_t seq = ud.ep0_seq;
	uint32_t used;
	int n;

	n = snprintf(ep0_report, sizeof(ep0_report),
	    "KEBAB EP0 PROBE: progress 0x%08x seq %u req %u phase %u status %u\n",
	    v, (v >> 24) & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v & 0xff);
	if (n <= 0) {
		return 0;
	}
	used = (uint32_t)n;
	if (used >= EP0_REPORT_SIZE) {
		used = EP0_REPORT_SIZE - 1;
	}
	n = snprintf(ep0_report + used, EP0_REPORT_SIZE - used,
	    "KEBAB EP0 STATE: resets=%u disconnects=%u fences=%u events=%u "
	    "acked=%u discarded=%u cursor=%u takeover=%u tx_complete=%u line="
	    "%02x%02x%02x%02x%02x%02x%02x controls=%u\n",
	    ud.resets, ud.disconnects, ud.fences, ud.events, ud.event_acked,
	    ud.event_discarded, ud.evt_pos, ud.takeover_active, ud.tx_completed,
	    ud.line[0], ud.line[1], ud.line[2], ud.line[3], ud.line[4],
	    ud.line[5], ud.line[6], ud.control_lines);
	if (n <= 0 || (uint32_t)n >= EP0_REPORT_SIZE - used) return used;
	used += (uint32_t)n;

	/*
	 * One line that answers the only question a hardware session can ask about
	 * the debugger link: did bytes arrive, and did the kernel ever answer?
	 *   host_in   bulk-OUT bytes accepted from the host
	 *   in_drop   bulk-OUT bytes lost because the input ring was full
	 *   kdp_in    bytes handed to the byte-stream reader (pal_serial_getc)
	 *   kdp_frames/kdp_out  frames and wire bytes queued for the debugger
	 *   tx_bytes  bulk-IN bytes the controller actually completed
	 * If host_in stays 0 nothing reached the device.  If host_in grows while
	 * kdp_in stays 0 the request arrived and the reader never ran.  If kdp_in
	 * grows but kdp_frames stays 0 the kernel consumed the request and chose
	 * not to answer it.  Owners are CPU+1 or 0, and identify a stranded lock.
	 */
	n = snprintf(ep0_report + used, EP0_REPORT_SIZE - used,
	    "KEBAB KDP STATE: host_in=%u in_drop=%u kdp_in=%u kdp_frames=%u "
	    "kdp_out=%u tx_bytes=%u owners=%u/%u/%u depths=%u/%u\n",
	    ud.rx_bytes, ud.rx_dropped, ud.kdp_rx_bytes, ud.kdp_tx_frames,
	    ud.kdp_tx_bytes, ud.tx_bytes, ud.owner, ud.rx_owner, ud.panic_cpu,
	    ud.owner_depth, ud.rx_depth);
	if (n > 0 && (uint32_t)n < EP0_REPORT_SIZE - used) used += (uint32_t)n;

	for (uint32_t i = 0; i < EP0_HISTORY_SIZE; i += 4) {
		uint32_t slot[4];
		uint32_t word[4];

		for (uint32_t k = 0; k < 4; k++) {
			slot[k] = (seq + 1 + i + k) & (EP0_HISTORY_SIZE - 1);
			word[k] = ud.ep0_history[slot[k]];
		}
		n = snprintf(ep0_report + used, EP0_REPORT_SIZE - used,
		    "KEBAB EP0 PROBE: [%02u]0x%08x [%02u]0x%08x [%02u]0x%08x [%02u]0x%08x\n",
		    slot[0], word[0], slot[1], word[1], slot[2], word[2],
		    slot[3], word[3]);
		if (n <= 0) {
			break;
		}
		if ((uint32_t)n >= (uint32_t)(EP0_REPORT_SIZE - used)) {
			used = EP0_REPORT_SIZE - 1;
			break;
		}
		used += (uint32_t)n;
	}
	n = snprintf(ep0_report + used, EP0_REPORT_SIZE - used, "KEBAB EP0 END\n");
	if (n > 0 && (uint32_t)n < EP0_REPORT_SIZE - used) used += (uint32_t)n;
	return used;
}

/*
 * Queue a fresh snapshot without recursively entering the console.
 *
 * EP0-PROBE.md specified a direct bulk-IN submit from the panic loop.  This
 * rewrite makes the log pump the single owner of bulk IN (ep[].busy and
 * tx_pending), so a second submit would race it and could drop log bytes.  The
 * caller already owns the transport; appending the report to the log ring
 * keeps the probe contract (the report follows any pending log DMA) and the
 * next ud_poll_locked() drains it in order.  No printf and no second poller.
 */
static void
kebab_usb_ep0_probe_send(void)
{
	/* Caller holds the transport, and the report follows all pending log DMA.
	 * Append without calling the console hook or starting a second poller. */
	if (ud.ep[EP_BULK_IN].busy || ud.tx_pending ||
	    os_atomic_load(&ud.log_head, acquire) != os_atomic_load(&ud.log_tail, acquire)) return;
	uint32_t cpu = (uint32_t)cpu_number() + 1;
	if (!os_atomic_cmpxchg(&ud.log_busy, 0u, cpu, acquire)) return;
	uint32_t len = kebab_usb_ep0_format();
	uint32_t head = os_atomic_load(&ud.log_head, relaxed);
	uint32_t tail = os_atomic_load(&ud.log_tail, acquire);
	uint32_t free = (tail + LOG_RING_SIZE - head - 1) % LOG_RING_SIZE;
	if (len <= free) {
		for (uint32_t i = 0; i < len; i++) {
			ud.log_ring[head] = ep0_report[i];
			head = (head + 1) % LOG_RING_SIZE;
		}
		os_atomic_store(&ud.log_head, head, release);
		ud.probe_requested = false;
	}
	os_atomic_store(&ud.log_busy, 0u, release);
}


/*
 * A panic whose console cannot be serviced must not park: this board has no
 * watchdog that would reset it, so an unattended boot would need a physical
 * power cycle.  Print the one line that tells the next session which case
 * happened, try to flush it, and reset through the same
 * panic-safe PSCI path the REBOOT command uses.  A return means
 * the firmware refused the reset; stay in the loop with the diagnosis on the
 * ring rather than claiming success.
 */
static void __dead2
ud_panic_reboot(const char *why)
{
	printf("KEBAB USB: panic console cannot be serviced (%s); rebooting instead of parking\n",
	    why);
	kebab_usb_log_flush();
	(void)ud_platform_restart();
	printf("KEBAB USB: panic reboot returned; the machine did not reset\n");
	for (;;) delay(1000);
}

/*
 * Keep the pipe alive after a panic instead of letting the machine halt with a dead
 * controller. Panic halt/restart paths call this terminal loop: the host
 * can then reconnect, keep reading later console output, and the device no longer
 * disappears the moment the panic report ends.
 */
void
kebab_usb_panic_spin(void)
{
	bool taken;
	unsigned refused;
	boolean_t irq;
	uint32_t cpu;

	(void)ml_set_interrupts_enabled(FALSE);
	cpu = (uint32_t)cpu_number() + 1;
	if (!os_atomic_cmpxchg(&ud.panic_cpu, 0u, cpu, acq_rel)) {
		/* A second/nested terminal spinner must not re-enter the transport. */
		for (;;) delay(1000);
	}
	taken = false;
	refused = 0;
	for (;;) {
		uint32_t command = 0;
		if (!taken) {
			taken = ud_panic_takeover();
			if (!taken) {
				/*
				 * A takeover that keeps failing means the port is
				 * unreachable.  One second of retries is enough for a
				 * live peer that is about to release the gate; in a
				 * panic nobody will.
				 */
				if (++refused >= 1000) {
					ud_panic_reboot("transport takeover refused");
				}
				delay(1000);
				continue;
			}
			if (ud.dead || !ud.running) {
				ud_panic_reboot(ud.dead ? "controller dead" :
				    "console not running");
			}
			printf("KEBAB PANIC: USB command loop ready (FASTBOOT/REBOOT/FWLOG?)\n");
		}
		if (ud.running && ud_enter(&irq)) {
			if (ud_rx_enter()) {
				ud_poll_locked();
				ud_rx_leave();
			}
			if (ud.probe_requested && ud.configured && !ud.dead)
				kebab_usb_ep0_probe_send();
			command = ud.reboot_command;
			ud.reboot_command = 0;
			ud_leave(irq);
		}
		/*
		 * A host command is answered outside the transport gate: the restart
		 * path prints through the same console, and it must not run with
		 * ud.owner held or with the panic-CPU fence still in the way.  This is
		 * the panic pump, so the dispatcher gets panicking=TRUE and may only
		 * reset: no filesystem sync, no driver locks, no allocation.
		 */
		if (command != 0) ud_run_reboot_command(command, TRUE);
		/*
		 * Keep the KDP hand-back from 0099: a debugger that reconnects after
		 * the panic parks here must not have its 0xFA CONNECT frame eaten as
		 * console input.  Invoke it only after ud_leave() so the debugger's
		 * own reads and replies start from a free transmit gate; they take the
		 * receive lock separately, which is why receiving no longer depends on
		 * this hand-back happening at all.
		 */
		if (kebab_usb_kdp_enabled() && kebab_usb_kdp_pending()) {
#if CONFIG_SERIAL_KDP
			extern boolean_t kdp_serial_panic_reenter(void);
			(void)kdp_serial_panic_reenter();
#endif
		}
		delay(1000);
	}
}

#endif /* KEBAB */
