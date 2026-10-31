/*
 * KEBAB TOUCH: Synaptics S3908 on QUPv3 SE13, I2C address 0x4B.
 *
 * The panel is 1080x2400; the sensor reports 1079x2399 with a 16x36
 * transmit/receive grid. Nothing is registered in the IORegistry: this is a
 * bring-up driver that polls instead of taking an interrupt, and it is off
 * unless boot-arg kebab_touch=1 is given. Decoded reports are readable from
 * /dev/kebab_touch (see below) as well as printed, but the kernel console
 * remains the output that is usable today.
 * It is started from bsd_init() next to the USB console, so the two can be
 * enabled independently.
 *
 * WHERE THE ADDRESSES COME FROM (read from this device, not guessed)
 *
 *   SE13            0x00a94000, 0x4000 bytes. The board device tree node
 *                   /soc/i2c@a94000 has reg = <0xa94000 0x4000>,
 *                   compatible = "qcom,i2c-geni", status = "ok", and it holds
 *                   two mutually exclusive touch nodes, told apart by panel:
 *
 *                     sec-s6sy761@48      reg 0x48, chip-name SY771,
 *                                         project-name 19821, panel =
 *                                         <generic Samsung, ANA6705,
 *                                          ANA6706, DD305>
 *                     synaptics-s3908@4B  reg 0x4B, chip-name S3908,
 *                                         project-name 19805, panel =
 *                                         <BOE, DD305, AMB655X>
 *
 *                   This unit's panel is the Samsung AMB655X, which appears
 *                   only in the S3908 list, so the touch part is the S3908 at
 *                   0x4B; 19805 is also the project_name in that DTB's own
 *                   bootargs.  The tree is the vendor DTB out of the kebab
 *                   image kept on the build host, decompiled with dtc.
 *   QUPv3_1 wrapper 0x00ac0000, 0x2000 bytes. The i2c node points at it with
 *                   qcom,wrapper-core = <phandle>; that phandle resolves to
 *                   /soc/qcom,qupv3_1_geni_se@ac0000, reg = <0xac0000 0x2000>.
 *   TLMM            0x00f000000, the qcom,kona-pinctrl node. Its
 *                   qupv3_se13_i2c_active group muxes "gpio36"/"gpio37" to
 *                   function "qup13"; the touch node's reset-gpio is pin 38
 *                   and its irq-gpio pin 39.
 *
 * The SE and wrapper bases stay boot-arg overridable anyway (kebab_touch_se=),
 * because a bring-up driver that cannot be pointed somewhere else is a driver
 * that has to be recompiled to test a suspicion.
 *
 * REGISTER PROGRAMMING
 *
 * The GENI/QUP register layout, the I2C sequencer opcodes and the SCL counter
 * tables are the ones in the Linux sources that drive this exact block:
 * include/linux/soc/qcom/geni-se.h and drivers/i2c/busses/i2c-qcom-geni.c
 * (v6.6). The TLMM per-pin layout (0x1000 per pin, mux at bit 2, pull at bit
 * 0, drive at bit 6, output-enable at bit 9) and the "qup13" function index
 * (1) are from drivers/pinctrl/qcom/pinctrl-msm.c and pinctrl-sm8250.c.
 *
 * The identity commands are the Synaptics TouchComm (TCM) ones implemented by
 * the synaptics_tcm driver: CMD_IDENTIFY 0x02, CMD_GET_BOOT_INFO 0x10,
 * CMD_GET_APPLICATION_INFO 0x20, sent as {command, length_lo, length_hi} and
 * answered with a 4-byte header {0xa5, code, length_lo, length_hi} followed by
 * the payload.
 *
 * TWO TRANSPORTS
 *
 * QUPv3/GENI is the default and uses the block the bootloader configured.
 * kebab_touch_bb=1 selects a second transport that bit-bangs I2C on the same
 * two pins as plain GPIO (mux 0, open drain, pull-up), with the same TCM
 * command layer above it.  The alternate path exists because the GENI setup is
 * the one part of this driver that cannot be checked without hardware; the
 * sibling OnePlus 9 port uses the bit-banged scheme on the same Synaptics
 * family at the same 0x48 address.  It is a diagnostic comparison, not a
 * replacement: QUPv3 stays the default.  With bit-bang selected, no SE or
 * wrapper register is read or written at all - only TLMM and the chip.
 *
 * BRING-UP SAFETY
 *
 * The USB console event thread shares the single CPU with this driver.  Every
 * bring-up phase prints and then flushes synchronously, the first MMIO access
 * is preceded by a 3 s blocking sleep so the host can open the CDC-ACM port,
 * and every bit-bang wait is bounded (1 ms per clock stretch, 20 TCM header
 * attempts).  A hang after a phase line therefore names the operation that
 * caused it instead of hiding the whole bring-up.
 *
 * WHAT IS NOT CONFIRMED (read this before trusting a silent console)
 *
 *   1. The bootloader is assumed to have left QUPv3_1/SE13 clocked and its
 *      protocol selected for I2C. Linux only *reads* that selection
 *      (GENI_FW_REVISION_RO bits 15:8) and refuses to drive an SE that is not
 *      an I2C one, which means the selection is made before the kernel runs.
 *      If the wrapper hardware version reads 0 or 0xffffffff the block is not
 *      clocked, and no amount of register poking here helps: the GCC branch
 *      clocks for QUPv3_1 are not programmed by this driver.
 *   2. That the part answers TCM at all. The vendor tree drives the S3908
 *      node with Synaptics' own driver (compatible "synaptics-s3908"), whose
 *      command framing is TCM, so the family matches; the exact identify
 *      opcodes still have to agree with the firmware revision, and
 *      kebab_touch_raw=1 keeps the raw bytes for the first hardware run.
 *   3. The reset pulse polarity (kebab_touch_reset=2 drives the opposite one)
 *      and whether the TLMM mux/pull/drive values taken from the device tree
 *      are what the chip needs (kebab_touch_pins=0 skips that step).
 *
 * TOUCH TO USER SPACE
 *
 * There is no HID stack in this kernel. IOHIDFamily is a separate Apple kext
 * and it is not here (no IOHIDSystem, IOHIDEventService, IOHIDEvent or
 * IOHIDDevice sources), there is no evdev, and the build has no KXLD so a kext
 * cannot be loaded either. What does exist is devfs and cdevsw: the decoded
 * reports go into a fixed-size queue and are readable from /dev/kebab_touch as
 * 48-byte records. The console dump remains the primary debug channel because
 * userland on this port cannot yet open the node. See docs/BRING-UP-STATE.md
 * section 十三 for the inventory and for what reaching IOHIDEvent would take.
 *
 * THREE COORDINATE SPACES
 *
 *   sensor   what the chip reports: 0..1079 and 0..2399. Those are maximum
 *            indices, not counts: 1080 x 2400 positions.
 *   panel    the physical panel, 1080 x 2400 pixels, the same space as the
 *            boot framebuffer and IOMobileFramebuffer's default surface. The
 *            sensor covers the panel exactly, so one step is one pixel.
 *   logical  what userland draws in: 1125 x 2436 at 3x. The display presents
 *            logical (22,18) at panel (0,0) and clips the margin, so
 *            logical = panel + (22,18). The 22 leftmost columns, 23 rightmost
 *            columns and 18 rows at the top and bottom of the logical display
 *            have no panel under them and cannot be touched.
 *
 * kt_transform() is the only place that converts between these three.
 *
 * Boot arguments:
 *   kebab_touch=1          enable this driver (required)
 *   kebab_touch_bb=1       bit-bang I2C on GPIO 36/37 instead of QUPv3 SE13
 *   kebab_touch_raw=1      dump SE registers, TLMM state and raw messages
 *   kebab_touch_dump=1     print the full per-object decode of every down
 *   kebab_touch_move_ms=N  minimum interval between move lines (default 200,
 *                          0 turns move lines off; down and up still print)
 *   kebab_touch_se=0x..    override the SE13 physical base
 *   kebab_touch_pins=0     skip the TLMM pin muxing
 *   kebab_touch_reset=0    skip the reset pulse; =2 drive the opposite polarity
 *   kebab_touch_hz=400000  I2C bus speed (100000 or 400000)
 */
#if defined(KEBAB)

#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>

/*
 * The wire format and the report decoder.  This is the generated kernel copy of
 * package/KebabTouchTcm.h (scripts/sync-touch-tcm.py); the host tests drive the
 * same source, so a layout this driver sends is a layout something has already
 * decoded.
 */
#include "kebab_touch_tcm.h"
#include "kebab_geni.h"
#define KT_RAIL_SETTLE_MS 15

/* arm/machine_routines.h declares this behind a guard this build does not
 * satisfy; the kebab UFS and USB drivers spell it out for the same reason. */


/*
 * Bring-up tracing.  The USB console is the only readable output after a
 * hang, and its event thread cannot run while this thread spins, so each
 * phase line is pushed out synchronously.  Both symbols are in kebab_usb.c;
 * if the console never came up the calls are harmless.
 */
extern bool kebab_usb_console_ready(void);
extern void kebab_usb_log_flush(void);

/* bsd/dev/kebab_usb.c: console entry points, read-only probe first. */
void kebab_touch_probe(void);
void kebab_touch_init_now(void);
void kebab_touch_clock_enable(void);
void kebab_touch_se(void);
void kebab_touch_ping(void);

/*
 * bsd/dev/kebab_spmi.c: the boot-time rail sequence.  It lives in that file
 * because the encoder, the RPMH whitelist and the TCS state machine do, and
 * because it is the same path VROMON has already been measured through.
 */
extern int kebab_spmi_vrm_boot_power(void);

/*
 * Returns 0 when the driver is off, finished or deliberately idle, and -1 when
 * the bring-up failed and touch is unavailable for the rest of this boot.  It
 * never panics, never loops and never retries.
 */
int kebab_touch_start(void);

/* ----------------------------------------------------------- address facts */

/* ------------------------------------------------------------ TCM protocol */

/*
 * The protocol itself -- framing, command codes, report ids, boot modes, the
 * report program and the report decoder -- lives in bsd/dev/kebab_touch_tcm.h,
 * which is the host-tested copy of package/KebabTouchTcm.h.  The byte transport lives in kebab_geni.c; this file owns
 * the TCM transaction and bring-up policy, not register access.
 *
 * Boot status is the second byte of the get-boot-info payload and belongs to the
 * one command that is only answered from the bootloader, so it stays here.
 */
#define TCM_BOOT_STATUS_OK             0x00
#define TCM_BOOT_STATUS_BOOTING        0x01
#define TCM_BOOT_STATUS_APP_BAD_CRC    0xfc
#define TCM_BOOT_STATUS_BAD_DISPLAY    0xfd
#define TCM_BOOT_STATUS_BAD_APP_FW     0xfe
#define TCM_BOOT_STATUS_WARM_BOOT      0xff

/* --------------------------------------------------------- input plumbing */

/*
 * The record /dev/kebab_touch returns. This is a kernel ABI: the layout is
 * fixed and magic lets a reader reject a buffer that was never filled. All
 * three coordinate pairs are carried so a consumer can check the transform
 * rather than redo it, and timestamp_ns is nanoseconds since boot (converted
 * from mach_absolute_time) so userland needs no timebase.
 */
#define KT_EVENT_MAGIC          0x4b544556u     /* 'KTEV' */
#define KT_EVENT_DOWN           1
#define KT_EVENT_MOVE           2
#define KT_EVENT_UP             3
#define KT_EVENT_FLAG_CLAMPED   0x01            /* sample was past the panel */

struct kt_event {
	uint32_t magic;
	uint32_t seq;                   /* report number, shared by its events */
	uint64_t timestamp_ns;
	int32_t  raw_x, raw_y;          /* sensor space */
	int32_t  panel_x, panel_y;      /* 0..1079, 0..2399 */
	int32_t  logical_x, logical_y;  /* 22..1101, 18..2417 */
	uint16_t pressure;              /* report z, 0..255 */
	uint8_t  phase;                 /* KT_EVENT_* */
	uint8_t  slot;                  /* contact index from the report */
	uint8_t  flags;
	uint8_t  pad[3];
};
_Static_assert(sizeof(struct kt_event) == 48, "kebab touch event layout");

/* 64 records: 3 KiB of BSS, never allocated. */
#define KT_QUEUE_LEN            64

/*
 * The sensor, panel and logical geometry, the transform between them and the
 * ten-slot limit all live in kebab_touch_tcm.h: kt_transform() is the only
 * caller and the host tests own the arithmetic.
 */

/* ---------------------------------------------------------------- state */

static struct {
	bool        raw;
	bool        bb;                 /* kebab_touch_bb=1: GPIO bit-banged I2C */
	bool        tcm_seen;
	bool        decode;
	bool        dump;
	bool        queue_ok;
	bool        dev_ready;
	uint32_t    max_x;
	uint32_t    max_y;
	uint32_t    max_objects;
	uint32_t    raw_dumps;
	uint32_t    padding;
	uint32_t    unknown;
	uint32_t    move_ms;
	uint32_t    seq;
	kebab_tcm_tracker tracker;      /* previous report: slots, last x/y */
	uint64_t    last_move_ns;
	int         cmajor;
	bool        rails_on;           /* kebab_touch_power brought both up */
	bool        unavailable;        /* bring-up failed; no transfer this boot */
	bool        mode_known;         /* identify returned a mode byte */
	uint8_t     mode;               /* KEBAB_TCM_MODE_* from the identify payload */
	bool        app_valid;          /* application info parsed */
	kebab_tcm_app_info app;         /* geometry and the two report sizes */
	uint8_t     program[KEBAB_TCM_PROGRAM_MAX]; /* accepted report program */
	uint32_t    program_len;
	uint32_t    object_bits;        /* bits per contact in that program */
	uint32_t    truncated;          /* reports dropped mid-contact */
} kt;

/*
 * Print one bring-up phase and push it out now.  printf() only queues into the
 * USB log ring; the event thread that drains it cannot run while this thread
 * is spinning in a delay or stuck on a register, so a hang would otherwise
 * hide the phase that caused it.  kebab_usb_log_flush() polls the controller
 * synchronously and is the same path the panic/KDP code uses.
 */

/*
 * Single-producer/single-consumer ring. The poll thread is the only producer;
 * read(2) is the consumer. Both take the mutex, which also gives msleep() a
 * lock to drop while a reader waits. With no consumer the oldest record is
 * overwritten and counted: the storage is a static array, so an absent reader
 * costs history but never memory.
 */
static struct {
	struct kt_event ev[KT_QUEUE_LEN];
	uint32_t head;                  /* next slot to write */
	uint32_t tail;                  /* next slot to read */
	uint32_t count;
	uint32_t opens;
	uint64_t pushed;
	uint64_t dropped;
	lck_grp_t *lock_group;
	lck_mtx_t *lock;
} ktq;


static void
kt_hexdump(const char *what, const uint8_t *buf, uint32_t len)
{
	static const char hex[] = "0123456789abcdef";
	char line[3 * 16 + 1];
	uint32_t i;

	for (i = 0; i < len; i += 16) {
		uint32_t j;
		uint32_t n = 0;

		for (j = 0; j < 16 && i + j < len; j++) {
			uint8_t b = buf[i + j];

			line[n++] = hex[b >> 4];
			line[n++] = hex[b & 0xf];
			line[n++] = ' ';
		}
		line[n] = '\0';
		printf("KEBAB TOUCH: %s[%u] %s\n", what, i, line);
	}
}

/* ---------------------------------------------------------- input plumbing */

struct kt_point {
	int32_t panel_x, panel_y;
	int32_t logical_x, logical_y;
	bool    clamped;
};

/*
 * Convert one sensor sample into panel and logical coordinates.  The three
 * spaces and the arithmetic are documented once, in kebab_tcm_transform(), and
 * that is the function the host tests exercise; this wrapper only feeds it the
 * geometry the application info reported.
 *
 *   sensor   raw_x/raw_y, the 12-bit fields of the touch report. kt.max_x and
 *            kt.max_y are the largest values the chip advertises (its app
 *            info, and the device tree's touchpanel,panel-coords: 1079 and
 *            2399). They are maximum indices, not counts, so the range is
 *            0..1079 - 1080 positions - and 0..2399 - 2400 positions.
 *
 *   panel    the physical 1080x2400 panel, the same space as the boot
 *            framebuffer and IOMobileFramebuffer's default surface.
 *
 *   logical  the 1125x2436 display userland draws in, which presents logical
 *            (22,18) at panel (0,0).
 *
 * A value above the advertised maximum (0xfff from a finger that lifted
 * mid-transfer is the common case) is clamped to the panel edge and flagged,
 * never extrapolated past the panel.
 */
static struct kt_point
kt_transform(uint32_t raw_x, uint32_t raw_y)
{
	kebab_tcm_point in = kebab_tcm_transform(raw_x, raw_y, kt.max_x,
	    kt.max_y);
	struct kt_point p;

	p.panel_x = in.panel_x;
	p.panel_y = in.panel_y;
	p.logical_x = in.logical_x;
	p.logical_y = in.logical_y;
	p.clamped = in.clamped != 0;
	return p;
}

/* Producer side: drops history rather than wait for a reader; no allocation. */
static void
ktq_push(const struct kt_event *ev)
{
	lck_mtx_lock(ktq.lock);
	if (ktq.count == KT_QUEUE_LEN) {
		/* No reader: overwrite the oldest record and count the loss. */
		ktq.tail = (ktq.tail + 1) % KT_QUEUE_LEN;
		ktq.count--;
		ktq.dropped++;
	}
	ktq.ev[ktq.head] = *ev;
	ktq.head = (ktq.head + 1) % KT_QUEUE_LEN;
	ktq.count++;
	ktq.pushed++;
	wakeup(&ktq);
	lck_mtx_unlock(ktq.lock);
}

/* Consumer side; read(2) opens the lock itself because it also sleeps in it. */
static bool
ktq_pop(struct kt_event *ev)
{
	bool got = false;

	lck_mtx_lock(ktq.lock);
	if (ktq.count != 0) {
		*ev = ktq.ev[ktq.tail];
		ktq.tail = (ktq.tail + 1) % KT_QUEUE_LEN;
		ktq.count--;
		got = true;
	}
	lck_mtx_unlock(ktq.lock);
	return got;
}

/*
 * Fill the ring three times over with nobody reading, then drain it. Proves
 * the two properties the interface depends on: the queue never holds more
 * than KT_QUEUE_LEN records, and an absent consumer loses the oldest records
 * (counted) while the storage stays a fixed array. Runs before the device
 * node and the poll thread exist, so it cannot race.
 */
static bool
kt_queue_selftest(void)
{
	struct kt_event ev;
	struct kt_event out;
	uint32_t i;
	uint32_t drained = 0;
	bool ok = true;

	bzero(&ev, sizeof(ev));
	ev.magic = KT_EVENT_MAGIC;
	for (i = 0; i < KT_QUEUE_LEN * 3; i++) {
		ev.seq = i;
		ktq_push(&ev);
	}
	if (ktq.count != KT_QUEUE_LEN ||
	    ktq.dropped != 2 * (uint64_t)KT_QUEUE_LEN) {
		ok = false;
	}
	for (i = 0; i < KT_QUEUE_LEN; i++) {
		if (!ktq_pop(&out) || out.seq != 2 * KT_QUEUE_LEN + i) {
			ok = false;
			break;
		}
		drained++;
	}
	if (ktq_pop(&out)) {
		ok = false;
	}
	if (ktq.count != 0) {
		ok = false;
	}

	printf("KEBAB TOUCH: queue selftest: %u slots, %u pushed, %llu dropped "
	    "without a reader, %u drained: %s\n", (unsigned)KT_QUEUE_LEN,
	    (unsigned)(KT_QUEUE_LEN * 3), (unsigned long long)ktq.dropped,
	    drained, ok ? "ok" : "FAILED");

	/* Leave the ring empty and the counters clean for real traffic. */
	ktq.head = 0;
	ktq.tail = 0;
	ktq.count = 0;
	ktq.opens = 0;
	ktq.pushed = 0;
	ktq.dropped = 0;
	return ok;
}

static struct kt_point
kt_emit(uint32_t phase, uint8_t slot, uint32_t raw_x, uint32_t raw_y,
    uint32_t pressure, uint64_t now_ns, uint32_t seq)
{
	struct kt_event ev;
	struct kt_point p = kt_transform(raw_x, raw_y);

	bzero(&ev, sizeof(ev));
	ev.magic = KT_EVENT_MAGIC;
	ev.seq = seq;
	ev.timestamp_ns = now_ns;
	ev.raw_x = (int32_t)raw_x;
	ev.raw_y = (int32_t)raw_y;
	ev.panel_x = p.panel_x;
	ev.panel_y = p.panel_y;
	ev.logical_x = p.logical_x;
	ev.logical_y = p.logical_y;
	ev.pressure = (uint16_t)pressure;
	ev.phase = (uint8_t)phase;
	ev.slot = slot;
	ev.flags = p.clamped ? KT_EVENT_FLAG_CLAMPED : 0;
	ktq_push(&ev);
	return p;
}

static void
kt_print_event(const char *what, uint8_t slot, uint32_t raw_x, uint32_t raw_y,
    uint32_t pressure, const struct kt_point *p)
{
	printf("KEBAB TOUCH: %-4s obj %u raw %u %u panel %d %d logical %d %d "
	    "z %u%s\n", what, (unsigned)slot, raw_x, raw_y, p->panel_x,
	    p->panel_y, p->logical_x, p->logical_y, pressure,
	    p->clamped ? " (clamped)" : "");
}

static int
kebab_touch_open(__unused dev_t dev, __unused int flags,
    __unused int devtype, __unused struct proc *p)
{
	bool first;

	lck_mtx_lock(ktq.lock);
	first = (ktq.opens++ == 0);
	lck_mtx_unlock(ktq.lock);
	if (first) {
		printf("KEBAB TOUCH: /dev/kebab_touch opened\n");
	}
	return 0;
}

static int
kebab_touch_close(__unused dev_t dev, __unused int flags,
    __unused int mode, __unused struct proc *p)
{
	lck_mtx_lock(ktq.lock);
	if (ktq.opens != 0) {
		ktq.opens--;
	}
	lck_mtx_unlock(ktq.lock);
	return 0;
}

/*
 * read(2) returns whole 48-byte records, oldest first. It blocks until one is
 * available unless O_NONBLOCK is set. Several readers split the stream; there
 * is one tail, not one per open.
 */
static int
kebab_touch_read(__unused dev_t dev, struct uio *uio, int ioflag)
{
	int error;

	while (uio_resid(uio) >= (int)sizeof(struct kt_event)) {
		struct kt_event ev;

		lck_mtx_lock(ktq.lock);
		while (ktq.count == 0) {
			if (ioflag & IO_NDELAY) {
				lck_mtx_unlock(ktq.lock);
				return EAGAIN;
			}
			error = msleep(&ktq, ktq.lock, PRIBIO | PCATCH,
			    "kebabtouch", NULL);
			if (error != 0) {
				lck_mtx_unlock(ktq.lock);
				return error;
			}
		}
		ev = ktq.ev[ktq.tail];
		ktq.tail = (ktq.tail + 1) % KT_QUEUE_LEN;
		ktq.count--;
		lck_mtx_unlock(ktq.lock);

		error = uiomove((const char *)&ev, (int)sizeof(ev), uio);
		if (error != 0) {
			return error;
		}
	}
	return 0;
}

static const struct cdevsw kebab_touch_cdevsw = {
	.d_open     = kebab_touch_open,
	.d_close    = kebab_touch_close,
	.d_read     = kebab_touch_read,
	.d_write    = eno_rdwrt,
	.d_ioctl    = eno_ioctl,
	.d_stop     = eno_stop,
	.d_reset    = eno_reset,
	.d_select   = eno_select,
	.d_mmap     = eno_mmap,
	.d_strategy = eno_strat,
	.d_type     = 0,        /* a record stream, not a tty or a disk */
};

static void
kt_dev_register(void)
{
	void *node;

	kt.cmajor = cdevsw_add(-1, &kebab_touch_cdevsw);
	if (kt.cmajor < 0) {
		printf("KEBAB TOUCH: no cdevsw slot for /dev/kebab_touch\n");
		return;
	}
	node = devfs_make_node(makedev(kt.cmajor, 0), DEVFS_CHAR, UID_ROOT,
	    GID_OPERATOR, 0440, "kebab_touch");
	if (node == NULL) {
		printf("KEBAB TOUCH: cdev major %d, but no /dev node\n",
		    kt.cmajor);
		cdevsw_remove(kt.cmajor, &kebab_touch_cdevsw);
		kt.cmajor = -1;
		return;
	}
	kt.dev_ready = true;
	printf("KEBAB TOUCH: /dev/kebab_touch major %d, %u-byte records, "
	    "%u slots\n", kt.cmajor, (unsigned)sizeof(struct kt_event),
	    (unsigned)KT_QUEUE_LEN);
}

/* -------------------------------------------------------------- raw dump */


/* -------------------------------------------------------- board plumbing */

/*
 * Mux SDA/SCL to the QUP function. The values are the device tree's
 * qupv3_se13_i2c_active state: function "qup13", bias-disable, 2 mA.
 */

/* Drive RESET as a plain GPIO; level is the electrical level. */

/*
 * Reset pulse. Mode 1 is the active-low arrangement the vendor drivers use
 * (drive low, release high); mode 2 drives the opposite polarity, for the case
 * where the first hardware run shows the chip never answering.
 */

/* ------------------------------------------------------------ SE bring-up */

/*
 * What geni_se_init() + geni_se_select_mode(FIFO) + geni_se_config_packing()
 * do, written out without the Linux clock and interconnect layers.
 */

/* ------------------------------------------------------- read-only probes */

/* Defined with the TCM layer below; TOUCHINIT reaches it after se_init. */
static int kt_identify(void);
static const char *kt_mode_name(uint32_t mode);
static const char *kt_status_name(uint32_t status);

/*
 * The vendor DTS (kona-qupv3.dtsi, i2c@a94000) gives SE13 three GCC clocks,
 * and drivers/clk/qcom/gcc-kona.c gives their registers:
 *
 *   GCC_QUPV3_WRAP1_S5_CLK      enable 0x52008 bit 27  halt 0x185fc
 *   GCC_QUPV3_WRAP_1_M_AHB_CLK  enable 0x52008 bit 20  halt 0x18004
 *   GCC_QUPV3_WRAP_1_S_AHB_CLK  enable 0x52008 bit 21  halt 0x18008
 *
 * Linux enables them in the i2c driver's runtime resume.  This kernel never
 * has, so before a GENI register is written it matters whether the SE has a
 * clock at all.  Only the two 4 KiB pages holding those registers are mapped,
 * not the 2 MiB GCC block: io_map's submap is 8 MiB and shared with
 * everything else this port maps.
 */

/*
 * Read-only GENI state.  Every register here was already read by the boot
 * path on this board, so the probe adds no new access class.
 */


/*
 * Console entry points.  Neither runs at boot unless a boot argument asks for
 * it: the previous run showed that a bad GENI access can take the USB console
 * down with it, and the console is the only recovery channel that does not
 * need a key press on the phone.
 */
static void
kebab_touch_probe_locked(void)
{
	struct kebab_geni_info info;
	if (!kebab_geni_map_default()) {
		printf("KEBAB TOUCH: probe cannot map SE13 or TLMM\n");
		return;
	}
	kebab_geni_get_info(&info);
	kebab_geni_trace("KEBAB TOUCH: read-only probe begin; no register is written\n");
	kebab_geni_trace("KEBAB TOUCH: state: initialized %u, dead %u, rails %u, "
	    "unavailable %u, last mode %s\n", info.initialized, info.dead,
	    kt.rails_on, kt.unavailable,
	    kt.mode_known ? kt_mode_name(kt.mode) : "never read");
	kebab_geni_probe_hardware();
	kebab_geni_trace("KEBAB TOUCH: read-only probe end\n");
}

static void
kebab_touch_init_now_locked(void)
{
	if (!kebab_geni_map_default()) {
		printf("KEBAB TOUCH: TOUCHINIT cannot map SE13 or TLMM\n");
		return;
	}
	if (!kebab_geni_manual_init()) { return; }
	kt_identify();
	kebab_geni_trace("KEBAB TOUCH: TOUCHINIT end\n");
}

/* ---------------------------------------------------- bit-banged GPIO I2C */

/*
 * Optional second transport, selected with kebab_touch_bb=1; QUPv3 remains
 * the default.  The sibling Synaptics port for the OnePlus 9 (S6SY792, same
 * 0x48 address) bit-bangs the same kind of bus after finding the GENI/SE
 * setup was the unreliable part, and keeping one TCM layer above two byte
 * transports is what makes a comparison between them meaningful.
 *
 * Both lines are open drain.  A high level means "release" (mux 0, pull-up,
 * output disabled); a low level means "drive 0" (output enable, data 0).
 * Neither line is ever driven high.  The TLMM register layout is the one the
 * QUP path already uses: mux in bits 5:2, pull in 1:0, drive in 8:6, output
 * enable in bit 9; TLMM_IO bit 0 is the input level and bit 1 the output
 * value.  mux 0 is the plain GPIO function, the same value kt_reset_level()
 * already uses; the device tree gives pins 36/37 function "qup13" for the
 * QUP path, which is why the registers are read and printed before the mux
 * is changed here.
 */

/*
 * Show the pin state the previous boot stage left, then take SDA/SCL for
 * plain GPIO.  This is deliberately one read-only print before any write:
 * if the mux or pad layout differs from the QUP path's assumption it is
 * visible in the log before the first write happens.
 */


/*
 * Recover a sequencer that stopped making progress.  Linux calls
 * geni_cancel_m_cmd(), and only if the command is still active
 * geni_abort_m_cmd(); both are control-register bits, not the identically
 * named status bits.  There is no interrupt handler here, so each wait polls
 * the status register with the same bounded delay the transfers use.
 *
 * Returns true only when GENI_STATUS no longer shows an active command.  On
 * failure the SE is marked unusable for the rest of the run: sending another
 * command into a stuck sequencer would make the next log unattributable.
 */

/* -------------------------------------------------------------- TCM layer */

/*
 * Read and validate one message header.  The four bytes are printed whenever
 * they are rejected, and the window slides one byte at a time because the
 * failure this bring-up actually saw was a stream one byte out of phase: after
 * the identify payload came a 0x00, and the next four bytes read as a 0x5a00
 * payload length (23040) instead of a status message.  A wrong marker is the
 * same class of problem, so both slide.
 *
 * The slide is bounded at four bytes: a stream that needs more than that is not
 * one byte out of phase, it is not a TCM stream, and the caller's retry loop
 * will try again after a delay.
 */
static int
kt_tcm_read_header(uint8_t cmd, kebab_tcm_header *header)
{
	uint8_t win[KEBAB_TCM_HEADER_SIZE];
	uint8_t next;
	uint32_t slid;
	int err;

	err = kebab_geni_i2c_read(win, KEBAB_TCM_HEADER_SIZE);
	if (err != 0) {
		return -1;
	}
	for (slid = 0; slid < 4; slid++) {
		kebab_tcm_status st = kebab_tcm_header_parse(win, header);

		if (st == KEBAB_TCM_OK) {
			if (slid != 0) {
				kebab_geni_trace("KEBAB TOUCH: command 0x%02x: resynchronised after %u byte(s)\n",
				    cmd, slid);
			}
			return 0;
		}
		if (slid == 0) {
			kebab_geni_trace("KEBAB TOUCH: command 0x%02x: header %02x %02x %02x %02x rejected (%s), sliding\n",
			    cmd, win[0], win[1], win[2], win[3],
			    kebab_tcm_status_name(st));
		}
		if (kebab_geni_i2c_read(&next, 1) != 0) {
			return -1;
		}
		win[0] = win[1];
		win[1] = win[2];
		win[2] = win[3];
		win[3] = next;
	}
	return -1;
}

/*
 * One TCM command: write {command, length_lo, length_hi, payload...}, wait for
 * a message header, then read its payload. Returns -1 when no valid message
 * appears. An idle bus returns bytes that are not a message at all.
 *
 * want_report is the report id the answer is expected to carry, or 0 when the
 * answer is a status response.  Identify is answered with REPORT_IDENTIFY
 * (0x10) and every other command with a status code below 0x10.  A message from
 * the wrong range is read out of the stream, printed, and the wait continues:
 * a report the chip queued on its own must not be decoded as the answer to
 * whatever command happens to be next, and a status where a report was expected
 * is a failed command rather than a wrong decode.
 *
 * Every answer is printed raw before anything is decoded from it.  That is the
 * whole point of this layer: the measured bring-up could not tell a layout
 * mistake from a framing mistake, and the next run has to be able to.
 */
static int
kt_tcm_command(uint8_t cmd, const uint8_t *payload, uint32_t len,
    uint8_t *resp, uint32_t resp_size, uint32_t *resp_len, uint8_t *resp_code,
    uint8_t want_report)
{
	uint8_t out[KEBAB_TCM_REPORT_CONFIG_MAX + 3];
	uint8_t scratch[KEBAB_TCM_MAX_PAYLOAD + 1];
	kebab_tcm_header header;
	uint32_t written;
	uint32_t i;
	uint32_t pad;
	int err;

	written = kebab_tcm_build_command(out, sizeof(out), cmd, payload, len);
	if (written == 0) {
		printf("KEBAB TOUCH: command 0x%02x: a %u-byte payload does not fit the write buffer\n",
		    cmd, len);
		return -1;
	}
	kebab_geni_trace("KEBAB TOUCH: TCM command 0x%02x, %u payload byte(s)\n", cmd,
	    len);
	err = kebab_geni_i2c_write(out, written);
	if (err != 0) {
		printf("KEBAB TOUCH: command 0x%02x was not written\n", cmd);
		return -1;
	}
	kebab_geni_trace("KEBAB TOUCH: TCM command 0x%02x written, waiting for header\n",
	    cmd);

	/*
	 * A chip that never answers must not hold the single CPU forever: the
	 * old 100 x 1 ms retry loop can spend more than a second per command,
	 * and each bit-bang read can itself burn a bounded millisecond per
	 * stuck bit.  20 attempts is enough for a healthy part (it answers on
	 * the first) and keeps a dead bus bounded.
	 */
	for (i = 0; i < 20; i++) {
		kebab_tcm_status st;

		if (kt_tcm_read_header(cmd, &header) != 0) {
			if (i < 4) {
				kebab_geni_trace("KEBAB TOUCH: no usable TCM header yet, attempt %u\n",
				    i + 1);
			}
			delay(1000);
			continue;
		}
		pad = header.payload_len;
		err = kebab_geni_i2c_read(scratch, header.payload_len + 1);
		if (err != 0) {
			printf("KEBAB TOUCH: command 0x%02x: answer 0x%02x could not be read\n",
			    cmd, header.code);
			return -1;
		}
		printf("KEBAB TOUCH: command 0x%02x answer: code 0x%02x, %u payload byte(s), trailing byte 0x%02x\n",
		    cmd, header.code, header.payload_len, scratch[pad]);
		if (header.payload_len != 0) {
			kt_hexdump("answer", scratch, header.payload_len);
		}
		if (scratch[pad] != KEBAB_TCM_PADDING) {
			printf("KEBAB TOUCH: command 0x%02x: the trailing byte is not the vendor padding 0x%02x; the stream and this driver disagree about where the message ends\n",
			    cmd, KEBAB_TCM_PADDING);
		}

		if (want_report != 0) {
			/*
			 * A status where a report was expected is the chip
			 * refusing the command, not a message to skip: identify
			 * needs the report to carry the mode.
			 */
			if (header.code < KEBAB_TCM_REPORT_IDENTIFY) {
				printf("KEBAB TOUCH: command 0x%02x was answered with status 0x%02x (%s), not with report 0x%02x\n",
				    cmd, header.code, kt_status_name(header.code),
				    want_report);
				return -1;
			}
			st = kebab_tcm_expect_report(&header, want_report);
		} else {
			if (header.code >= KEBAB_TCM_REPORT_IDENTIFY) {
				printf("KEBAB TOUCH: report 0x%02x arrived while waiting for command 0x%02x; read out\n",
				    header.code, cmd);
				delay(1000);
				continue;
			}
			st = kebab_tcm_expect_status(&header);
		}
		if (st != KEBAB_TCM_OK) {
			printf("KEBAB TOUCH: command 0x%02x: code 0x%02x is not the answer (%s); read out and waiting\n",
			    cmd, header.code, kebab_tcm_status_name(st));
			delay(1000);
			continue;
		}

		kt.tcm_seen = true;
		*resp_code = header.code;
		{
			/*
			 * The whole payload was read even when the caller's
			 * buffer is smaller, so a truncating decode cannot leave
			 * bytes behind either.
			 */
			uint32_t copy = header.payload_len;

			if (copy > resp_size) {
				printf("KEBAB TOUCH: command 0x%02x: payload %u does not fit %u, truncating\n",
				    cmd, header.payload_len, resp_size);
				copy = resp_size;
			}
			if (copy != 0 && resp != NULL) {
				memcpy(resp, scratch, copy);
			}
			if (resp_len != NULL) {
				*resp_len = copy;
			}
		}
		return 0;
	}
	printf("KEBAB TOUCH: command 0x%02x: no usable answer after %u attempts\n",
	    cmd, i);
	return -1;
}

static uint32_t
kt_le16(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/* The mode and boot-status bytes only mean something named. */
static const char *
kt_mode_name(uint32_t mode)
{
	switch (mode) {
	case KEBAB_TCM_MODE_APPLICATION:
		return "application";
	case KEBAB_TCM_MODE_HOST_DOWNLOAD:
		return "host download";
	case KEBAB_TCM_MODE_BOOTLOADER:
		return "bootloader";
	case KEBAB_TCM_MODE_TDDI_BOOTLOADER:
		return "TDDI bootloader";
	case KEBAB_TCM_MODE_PRODUCTION_TEST:
		return "production test";
	default:
		return "unknown";
	}
}

static const char *
kt_status_name(uint32_t status)
{
	switch (status) {
	case 0x00:
		return "idle";
	case KEBAB_TCM_STATUS_OK:
		return "OK";
	case 0x02:
		return "busy";
	case 0x03:
		return "continued read";
	case 0x0b:
		return "not executed in deep sleep";
	case 0x0c:
		return "receive buffer overflow";
	case 0x0d:
		return "previous command pending";
	case 0x0e:
		return "not implemented";
	case 0x0f:
		return "error";
	default:
		return "unknown";
	}
}

static const char *
kt_boot_status_name(uint32_t status)
{
	switch (status) {
	case TCM_BOOT_STATUS_OK:
		return "OK";
	case TCM_BOOT_STATUS_BOOTING:
		return "booting";
	case TCM_BOOT_STATUS_APP_BAD_CRC:
		return "application bad display CRC";
	case TCM_BOOT_STATUS_BAD_DISPLAY:
		return "bad display config";
	case TCM_BOOT_STATUS_BAD_APP_FW:
		return "bad application firmware";
	case TCM_BOOT_STATUS_WARM_BOOT:
		return "warm boot";
	default:
		return "unknown";
	}
}

/*
 * Identify.  The payload is the vendor driver's syna_tcm_identification, which
 * the S3908 firmware implements:
 *
 *   vendor     [0] version, [1] mode, [2..17] part number, [18..21] build id
 *   measured   a5 03, then the same fields from offset 2: version 0x01,
 *              mode 0x01, "s3908-15.0.0", build 0x003cbab9 -- 24 bytes
 *
 * There is no status byte in this payload: identify is answered with a
 * REPORT_IDENTIFY report, not with a command status.  The bytes are printed
 * before the decode, and the decode itself anchors on the part number
 * (kebab_tcm_parse_identify) instead of indexing a fixed offset, so a firmware
 * that puts something in front of the version says so rather than shifting the
 * part name into the prefix.
 */
static int
kt_identify(void)
{
	uint8_t resp[KEBAB_TCM_MAX_PAYLOAD];
	uint32_t len = 0;
	uint8_t code = 0;
	kebab_tcm_identify id;

	if (kt_tcm_command(KEBAB_TCM_CMD_IDENTIFY, NULL, 0, resp, sizeof(resp),
	    &len, &code, KEBAB_TCM_REPORT_IDENTIFY) != 0) {
		kt.mode_known = false;
		return -1;
	}
	printf("KEBAB TOUCH: identify: message 0x%02x, %u payload byte(s)\n",
	    (unsigned)code, len);
	kt_hexdump("identify payload", resp, len);
	if (kebab_tcm_parse_identify(resp, len, &id) != KEBAB_TCM_OK) {
		printf("KEBAB TOUCH: identify: %u byte(s) carry no printable part number with version and mode in front; no identity decoded\n",
		    len);
		kt.mode_known = false;
		return -1;
	}
	printf("KEBAB TOUCH: identify: part \"%s\", version 0x%02x, mode 0x%02x (%s)\n",
	    id.part, (unsigned)id.version, (unsigned)id.mode,
	    kt_mode_name(id.mode));
	if (id.prefix_len != 0) {
		uint32_t show = id.prefix_len < sizeof(id.prefix) ?
		    id.prefix_len : (uint32_t)sizeof(id.prefix);

		printf("KEBAB TOUCH: identify: %u byte(s) in front of those are left uninterpreted\n",
		    id.prefix_len);
		kt_hexdump("identify prefix", id.prefix, show);
	}
	if (id.build_known) {
		printf("KEBAB TOUCH: identify: build 0x%08x\n", id.build);
	}
	kt.mode = id.mode;
	kt.mode_known = true;
	return 0;
}

/*
 * Boot info: [0] version, [1] boot status, [2..3] asic id, [4] write block size
 * in words, [5..6] erase page size, [7..8] max write payload, [9] last reset
 * reason.  Printed raw first for the same reason as the identity: this is the
 * one message a chip sitting in its bootloader still answers, so its bytes are
 * the evidence, not the decode.
 */
static void
kt_boot_info(void)
{
	uint8_t resp[40];
	uint32_t len = 0;
	uint8_t code = 0;

	if (kt_tcm_command(KEBAB_TCM_CMD_GET_BOOT_INFO, NULL, 0, resp, sizeof(resp),
	    &len, &code, 0) != 0) {
		printf("KEBAB TOUCH: boot status: the chip did not answer the get-boot-info command\n");
		return;
	}
	printf("KEBAB TOUCH: boot info: message 0x%02x, %u payload byte(s)\n",
	    (unsigned)code, len);
	kt_hexdump("boot info payload", resp, len);
	if (len < 2) {
		printf("KEBAB TOUCH: boot info: code 0x%02x (%s), %u payload byte(s): this firmware does not report boot status\n",
		    (unsigned)code, kt_status_name(code), len);
		return;
	}
	printf("KEBAB TOUCH: boot info: version 0x%02x status 0x%02x (%s)",
	    (unsigned)resp[0], (unsigned)resp[1],
	    kt_boot_status_name(resp[1]));
	if (len > 3) {
		printf(", asic 0x%04x", kt_le16(&resp[2]));
	}
	if (len > 8) {
		printf(", max write payload %u", kt_le16(&resp[7]));
	}
	if (len > 9) {
		printf(", last reset reason 0x%02x", (unsigned)resp[9]);
	}
	printf("\n");
}

/*
 * Application info: the vendor's syna_tcm_app_info, 46 bytes of little-endian
 * 16-bit fields.  Three of them matter before a report can be asked for:
 * max_touch_report_config_size (how many bytes the report program buffer is),
 * max_touch_report_payload_size (how big a report can get) and max_x/max_y
 * (the sensor geometry the transform maps onto the panel).
 *
 * Printed raw first and then field by field: the previous bring-up could not
 * read this answer at all ("payload 23040 too large" was a framing error, not a
 * layout one), so the next run has to show the bytes next to the decode.
 */
static void
kt_app_info(void)
{
	uint8_t resp[KEBAB_TCM_MAX_PAYLOAD];
	uint32_t len = 0;
	uint8_t code = 0;
	kebab_tcm_app_info app;

	kt.app_valid = false;
	if (kt_tcm_command(KEBAB_TCM_CMD_GET_APP_INFO, NULL, 0, resp,
	    sizeof(resp), &len, &code, 0) != 0) {
		printf("KEBAB TOUCH: app info: command 0x%02x did not answer; the report sizes stay unknown and max_x/max_y fall back to the device tree\n",
		    KEBAB_TCM_CMD_GET_APP_INFO);
		return;
	}
	printf("KEBAB TOUCH: app info: message 0x%02x, %u payload byte(s)\n",
	    (unsigned)code, len);
	kt_hexdump("app info payload", resp, len);
	if (kebab_tcm_parse_app_info(resp, len, &app) != KEBAB_TCM_OK) {
		printf("KEBAB TOUCH: app info: %u byte(s) is short of the 46 the vendor struct has; the geometry and the report sizes stay unknown\n",
		    len);
		return;
	}
	printf("KEBAB TOUCH: app info: version 0x%04x status 0x%04x, max_x %u max_y %u objects %u buttons %u rows %u cols %u hybrid %u\n",
	    app.version, app.status, app.max_x, app.max_y, app.max_objects,
	    app.num_of_buttons, app.num_of_image_rows, app.num_of_image_cols,
	    app.has_hybrid_data);
	printf("KEBAB TOUCH: app info: static config %u bytes, dynamic config %u bytes, app config %u bytes at block %u\n",
	    app.static_config_size, app.dynamic_config_size, app.app_config_size,
	    app.app_config_start_write_block);
	printf("KEBAB TOUCH: app info: max touch report config %u bytes, max touch report payload %u bytes\n",
	    app.max_touch_report_config_size, app.max_touch_report_payload_size);
	kt.app = app;
	kt.app_valid = true;
	kt.max_x = app.max_x;
	kt.max_y = app.max_y;
	kt.max_objects = app.max_objects;
	if (app.status != 0) {
		printf("KEBAB TOUCH: application status 0x%04x is not OK; the chip may still be booting\n",
		    app.status);
	}
}

/*
 * Ask the firmware for a known report layout and keep the program that was
 * accepted, because the decoder walks that program rather than a hardcoded
 * stride.
 *
 * The first attempt includes z, which is the only pressure proxy a touch report
 * carries.  If the firmware refuses it, the second attempt is the vendor
 * driver's own program: index, classification, x and y, no z.  The decoder does
 * not need a special case for that -- it reads whichever program the chip took.
 *
 * The payload is the program zero padded to max_touch_report_config_size when
 * the application info gave a usable size, which is what the vendor driver
 * sends; otherwise the program alone.  Sending the padded buffer is not
 * cosmetic: the firmware's program parser stops at TOUCH_END, which is a zero
 * byte, so the padding is the terminator's room.
 */
static bool
kt_set_report_config(void)
{
	uint8_t payload[KEBAB_TCM_REPORT_CONFIG_MAX];
	uint8_t resp[8];
	uint32_t firmware_max = 0;
	uint32_t program_len;
	uint32_t payload_len;
	uint32_t len = 0;
	uint8_t code = 0;
	int attempt;

	if (kt.app_valid) {
		firmware_max = kt.app.max_touch_report_config_size;
		if (firmware_max > KEBAB_TCM_REPORT_CONFIG_MAX) {
			printf("KEBAB TOUCH: app info allows a %u-byte report program; this driver carries %u, so the program is sent unpadded\n",
			    firmware_max, KEBAB_TCM_REPORT_CONFIG_MAX);
			firmware_max = 0;
		}
	}
	for (attempt = 0; attempt < 2; attempt++) {
		program_len = kebab_tcm_program_build(kt.program,
		    sizeof(kt.program), attempt == 0);
		if (program_len == 0) {
			printf("KEBAB TOUCH: the report program does not fit its buffer\n");
			return false;
		}
		payload_len = kebab_tcm_report_payload_build(payload,
		    sizeof(payload), kt.program, program_len, firmware_max);
		if (payload_len == 0) {
			printf("KEBAB TOUCH: the %u-byte program does not fit the firmware's %u-byte report config buffer%s\n",
			    program_len, firmware_max,
			    attempt == 0 ? "; retrying with the vendor program" : "");
			continue;
		}
		printf("KEBAB TOUCH: report program: %u bytes, %u bits per contact, %u bytes sent\n",
		    program_len,
		    kebab_tcm_program_object_bits(kt.program, program_len),
		    payload_len);
		kt_hexdump("report program", kt.program, program_len);
		if (kt_tcm_command(KEBAB_TCM_CMD_SET_REPORT_CONFIG, payload,
		    payload_len, resp, sizeof(resp), &len, &code, 0) != 0) {
			printf("KEBAB TOUCH: report config: the command did not complete\n");
			return false;
		}
		printf("KEBAB TOUCH: report config: answered code 0x%02x (%s), %u payload byte(s)\n",
		    (unsigned)code, kt_status_name(code), len);
		if (code == KEBAB_TCM_STATUS_OK) {
			kt.program_len = program_len;
			kt.object_bits = kebab_tcm_program_object_bits(kt.program,
			    program_len);
			printf("KEBAB TOUCH: report config accepted: %u bits per contact, %s\n",
			    kt.object_bits, attempt == 0 ? "z included" :
			    "vendor program without z");
			return true;
		}
		if (attempt == 0) {
			printf("KEBAB TOUCH: the chip did not accept the z program; retrying with the vendor program\n");
		}
		/*
		 * 0x0d is "previous command pending": a short pause is the only
		 * thing that clears it, and one retry is the whole budget.
		 */
		kebab_geni_sleep_ms(10);
	}
	printf("KEBAB TOUCH: the chip accepted neither report program; there is no layout to decode\n");
	return false;
}

/*
 * One touch report.  The decoder walks kt.program -- the program the firmware
 * accepted -- so the layout is the one that was negotiated, not a guess; the
 * tracker turns the contacts into down/move/up against the previous report.
 *
 * Two things are refused rather than decoded:
 *   - a program the payload does not fit, or a field that runs past the end
 *     (a truncated report): decoding it would read a contact out of whatever
 *     follows;
 *   - a tail after the last whole contact that is not zero bits: that is a cut
 *     contact, and turning it into an up would lift every held finger.
 */
static void
kt_touch_report(const uint8_t *data, uint32_t len)
{
	kebab_tcm_report report;
	kebab_tcm_event events[2 * KEBAB_TCM_MAX_OBJECTS];
	kebab_tcm_status st;
	uint64_t now_ns;
	uint32_t nev;
	uint32_t seq;
	uint32_t i;
	bool down_seen = false;

	if (kt.program_len == 0) {
		return;
	}
	st = kebab_tcm_decode_report(kt.program, kt.program_len, data, len,
	    kt.max_objects, &report);
	if (st != KEBAB_TCM_OK) {
		if (kt.truncated++ < 8) {
			printf("KEBAB TOUCH: report of %u byte(s) discarded: the payload and the program disagree (%s)\n",
			    len, kebab_tcm_status_name(st));
		}
		return;
	}
	if (report.truncated_nonzero != 0) {
		if (kt.truncated++ < 8) {
			printf("KEBAB TOUCH: report of %u byte(s) discarded: %u bit(s) left after %u contact(s), too short for another\n",
			    len, report.truncated_bits, report.objects);
		}
		return;
	}
	if (kt.raw && kt.raw_dumps < 16) {
		kt.raw_dumps++;
		for (i = 0; i < report.objects; i++) {
			printf("KEBAB TOUCH: contact %u idx %u class %u x %u y %u z %u\n",
			    i, report.object[i].index,
			    report.object[i].classification, report.object[i].x,
			    report.object[i].y, report.object[i].z);
		}
	}

	absolutetime_to_nanoseconds(mach_absolute_time(), &now_ns);
	seq = ++kt.seq;

	nev = kebab_tcm_track(&kt.tracker, &report, events,
	    2 * KEBAB_TCM_MAX_OBJECTS);
	for (i = 0; i < nev; i++) {
		const kebab_tcm_event *ev = &events[i];
		struct kt_point p;
		uint32_t phase;
		uint32_t pressure;

		switch (ev->phase) {
		case KEBAB_TCM_PHASE_DOWN:
			phase = KT_EVENT_DOWN;
			pressure = ev->pressure;
			break;
		case KEBAB_TCM_PHASE_MOVE:
			phase = KT_EVENT_MOVE;
			pressure = ev->pressure;
			break;
		default:
			phase = KT_EVENT_UP;
			pressure = 0;
			break;
		}
		p = kt_emit(phase, ev->slot, ev->raw_x, ev->raw_y, pressure,
		    now_ns, seq);
		if (phase == KT_EVENT_DOWN) {
			kt_print_event("down", ev->slot, ev->raw_x, ev->raw_y,
			    pressure, &p);
			down_seen = true;
			kt.last_move_ns = now_ns;
		} else if (phase == KT_EVENT_MOVE) {
			/*
			 * The rate limit is on the printed line only; the record
			 * is queued for every move so a reader sees all of them.
			 */
			if (kt.move_ms != 0 && now_ns - kt.last_move_ns >=
			    (uint64_t)kt.move_ms * 1000000ull) {
				kt_print_event("move", ev->slot, ev->raw_x,
				    ev->raw_y, pressure, &p);
				kt.last_move_ns = now_ns;
			}
		} else {
			kt_print_event("up", ev->slot, ev->raw_x, ev->raw_y, 0,
			    &p);
		}
	}

	/* kebab_touch_dump=1: the decoded report behind a touch-down. */
	if (kt.dump && down_seen) {
		printf("KEBAB TOUCH: report seq %u: %u contact(s), %u of %u payload bits\n",
		    seq, report.objects, report.bits, report.payload_bits);
		for (i = 0; i < report.objects; i++) {
			printf("KEBAB TOUCH:   obj %u idx %u class %u x %u y %u z %u\n",
			    i, report.object[i].index,
			    report.object[i].classification, report.object[i].x,
			    report.object[i].y, report.object[i].z);
		}
	}
}

/*
 * Read one queued TCM message, if any.  The framing is the module's: four
 * header bytes, payload, one 0x5a.  A header whose declared length is not
 * plausible is not a header, and a message whose last byte is not the padding
 * is not necessarily in phase either -- both are counted and printed, and only
 * a message that passes is decoded.  That is the check the previous bring-up
 * lacked, where a 0x5a00 length turned into "payload 23040 too large" and then
 * into a framing error on every poll.
 */
static int
kt_poll_once(void)
{
	uint8_t hdr[KEBAB_TCM_HEADER_SIZE];
	uint8_t payload[KEBAB_TCM_MAX_PAYLOAD + 1]; /* payload plus padding */
	kebab_tcm_header header;
	kebab_tcm_status st;
	uint32_t len;
	int err;

	err = kebab_geni_i2c_read(hdr, KEBAB_TCM_HEADER_SIZE);
	if (err != 0) {
		return -1;
	}
	st = kebab_tcm_header_parse(hdr, &header);
	if (st == KEBAB_TCM_ERR_MARKER) {
		/*
		 * An idle bus reads back bytes that are not a message.  That is
		 * the normal "nothing there" answer, not a polling error.
		 */
		return 0;
	}
	if (st != KEBAB_TCM_OK) {
		if (kt.padding++ < 8) {
			printf("KEBAB TOUCH: message header %02x %02x %02x %02x rejected (%s); not decoded\n",
			    hdr[0], hdr[1], hdr[2], hdr[3],
			    kebab_tcm_status_name(st));
		}
		return -1;
	}
	len = header.payload_len;
	err = kebab_geni_i2c_read(payload, len + 1);
	if (err != 0) {
		return -1;
	}
	if (payload[len] != KEBAB_TCM_PADDING && kt.padding++ < 8) {
		/*
		 * A header that passed the marker and length checks is strong
		 * evidence of phase, so one wrong padding byte is more likely a
		 * firmware that pads with something else than a lost byte.  It is
		 * printed loudly and the message is still decoded; a run that
		 * shows this line on every report says the firmware pads with
		 * 0x%02x and nothing else is wrong.
		 */
		printf("KEBAB TOUCH: message 0x%02x: padding 0x%02x, not the vendor 0x%02x; decoding anyway\n",
		    (unsigned)header.code, (unsigned)payload[len],
		    KEBAB_TCM_PADDING);
	}
	if (kt.raw && kt.raw_dumps < 16) {
		kt.raw_dumps++;
		printf("KEBAB TOUCH: message code 0x%02x len %u\n",
		    (unsigned)header.code, len);
		kt_hexdump("report", payload, len);
	}
	if (header.code == KEBAB_TCM_REPORT_TOUCH) {
		if (kt.decode) {
			kt_touch_report(payload, len);
		}
	} else if (header.code == KEBAB_TCM_REPORT_STATUS) {
		printf("KEBAB TOUCH: status report, %u bytes\n", len);
	} else if (header.code > 0x0f && kt.unknown++ < 8) {
		printf("KEBAB TOUCH: message code 0x%02x len %u\n",
		    (unsigned)header.code, len);
	}
	return 1;
}

/* the polling thread never returns */
static void kt_thread(void *arg, wait_result_t wr) __dead2;

static void
kt_thread(void *arg __unused, wait_result_t wr __unused)
{
	for (;;) {
		kebab_geni_transaction_lock();
		int n = kt_poll_once();
		uint32_t errors = n < 0 ? kebab_geni_record_poll_error() : 0;
		kebab_geni_transaction_unlock();

		if (n < 0) {
			if (errors == 1 || (errors % 500u) == 0) {
				printf("KEBAB TOUCH: %u polling errors\n", errors);
			}
			kebab_geni_sleep_ms(100);
		} else {
			kebab_geni_sleep_ms(5);
		}
	}
}

/*
 * Gate D: bring the SE up (the S5 clock must already be on), then send exactly
 * one address-only write, so the FIFO handshake, ACK/NACK and the timeout path
 * can be judged without TCM command bytes in flight.
 */

/* ------------------------------------------------------------------ start */

static int
kebab_touch_start_locked(void)
{
	uint32_t enabled = 0;
	uint32_t value = 0;
	uint32_t se_pa = KT_SE13_PA_DEFAULT;
	uint32_t hz = 100000;
	uint32_t pins = 1;
	uint32_t reset = 1;
	uint32_t boot = 0;
	uint32_t probe = 0;
	uint32_t power = 0;

	thread_t thread;

	if (!PE_parse_boot_argn("kebab_touch", &enabled, sizeof(enabled)) ||
	    enabled == 0) {
		return 0;
	}
	kt.raw = PE_parse_boot_argn("kebab_touch_raw", &value, sizeof(value)) &&
	    value != 0;
	if (PE_parse_boot_argn("kebab_touch_se", &value, sizeof(value)) &&
	    value >= 0x00100000u) {
		se_pa = value;
	}
	if (PE_parse_boot_argn("kebab_touch_hz", &value, sizeof(value)) &&
	    value != 0) {
		hz = value;
	}
	if (PE_parse_boot_argn("kebab_touch_pins", &value, sizeof(value))) {
		pins = value;
	}
	if (PE_parse_boot_argn("kebab_touch_reset", &value, sizeof(value))) {
		reset = value;
	}
	if (PE_parse_boot_argn("kebab_touch_boot", &value, sizeof(value))) {
		boot = value;
	}
	if (PE_parse_boot_argn("kebab_touch_probe", &value, sizeof(value))) {
		probe = value;
	}
	if (PE_parse_boot_argn("kebab_touch_power", &value, sizeof(value))) {
		power = value;
	}

	kt.dump = PE_parse_boot_argn("kebab_touch_dump", &value,
	    sizeof(value)) && value != 0;
	kt.bb = PE_parse_boot_argn("kebab_touch_bb", &value,
	    sizeof(value)) && value != 0;
	kt.move_ms = 200;
	if (PE_parse_boot_argn("kebab_touch_move_ms", &value, sizeof(value))) {
		kt.move_ms = value;
	}

	/*
	 * The USB console event thread runs on the same CPU and cannot make
	 * progress while this thread is busy, so give the host time to see the
	 * CDC-ACM port and open it before the first register access.  delay()
	 * blocks (rather than spins) for millisecond intervals, so the event
	 * thread and the host enumeration run during this sleep.
	 */
	kebab_geni_trace("KEBAB TOUCH: bring-up start: bb=%u pins=%u reset=%u hz=%u; pausing 3 s for the USB console\n",
	    (unsigned)kt.bb, pins, reset, hz);
	kebab_geni_sleep_ms(3000);
	kebab_geni_trace("KEBAB TOUCH: console window over, starting MMIO\n");
	/*
	 * Rails before anything else on the bus.  Both enables are RPMh VREG
	 * registers that come back 0x00 on every reboot, and an unpowered SE13
	 * target aborts the transfer with GENI state 0x21 instead of NACKing,
	 * so this has to happen before the first byte rather than after a
	 * failure.  kebab_spmi_vrm_boot_power() reads each enable through the
	 * PWR? observer path, sends at most one ENABLE per rail, waits for the
	 * TCS to complete and reads the rail back; any failure closes the
	 * channel for the boot, which is the module's own rule.
	 */
	if (power != 0) {
		if (kebab_spmi_vrm_boot_power() != 0) {
			kt.unavailable = true;
			printf("KEBAB TOUCH: touch unavailable: a rail did not "
			    "come up, so no transfer is attempted; the boot "
			    "continues without touch\n");
			return -1;
		}
		kt.rails_on = true;
		/*
		 * The sequence ends when ldoc1 completes; the chip wants the
		 * supplies stable for 10-20 ms before the reset edge.
		 */
		kebab_geni_sleep_ms(KT_RAIL_SETTLE_MS);
		printf("KEBAB TOUCH: both rails up, waited %u ms before the reset\n",
		    KT_RAIL_SETTLE_MS);
	} else if (boot != 0) {
		printf("KEBAB TOUCH: kebab_touch_power is off; assuming the rails are already powered\n");
	}

	/*
	 * The record storage is static; the mutex is not. Allocate it here so
	 * the queue selftest can exercise the real push/pop path before the
	 * device node or the poll thread exist.
	 */
	ktq.lock_group = lck_grp_alloc_init("kebabtouch", LCK_GRP_ATTR_NULL);
	ktq.lock = lck_mtx_alloc_init(ktq.lock_group, LCK_ATTR_NULL);
	if (ktq.lock == NULL) {
		printf("KEBAB TOUCH: cannot allocate the queue lock\n");
		return -1;
	}
	kt.queue_ok = kt_queue_selftest();

	if (kebab_geni_map(se_pa, kt.bb, pins, reset, kt.raw) != 0) { return -1; }
	if (probe != 0) {
		kebab_touch_probe_locked();
		return 0;
	}
	if (boot == 0) {
		printf("KEBAB TOUCH: MMIO mapped, hardware untouched; "
		    "TOUCH? is read-only, TOUCHINIT starts the driver\n");
		return 0;
	}
	if (kebab_geni_boot_prepare(se_pa, hz, pins, reset, kt.raw) != 0) { return -1; }

	/*
	 * Identity first.  Identify is answered from either mode, so this is
	 * also the test for whether the touch application is running at all;
	 * the bytes are printed before the decode so a mismatch between this
	 * driver's reading and the chip's answer is visible in the log.
	 */
	kebab_geni_trace("KEBAB TOUCH: TCM identify begin\n");
	if (kt_identify() != 0) {
		kt.unavailable = true;
		printf("KEBAB TOUCH: touch unavailable: the controller did not "
		    "answer the identify command, so the poll thread is not "
		    "started. Try kebab_touch_raw=1, and check clocking, pins "
		    "and the rails\n");
		return -1;
	}
	/*
	 * Boot status is read even when the mode above is wrong: a chip in its
	 * bootloader still answers this, and that answer is the evidence for
	 * what it is doing.
	 */
	kt_boot_info();
	if (kt.mode != KEBAB_TCM_MODE_APPLICATION) {
		kt.unavailable = true;
		printf("KEBAB TOUCH: touch unavailable: the controller is in %s "
		    "mode (0x%02x), not application mode, so the touch firmware "
		    "is not running; making it run needs a firmware download, "
		    "which this driver does not implement\n",
		    kt_mode_name(kt.mode), (unsigned)kt.mode);
		return -1;
	}
	printf("KEBAB TOUCH: identity: APPLICATION MODE (mode 0x%02x), the "
	    "touch firmware is running\n", (unsigned)kt.mode);
	kt_app_info();
	if (kt.max_x == 0 || kt.max_y == 0) {
		printf("KEBAB TOUCH: no usable app info; the device tree says "
		    "1079x2399 with a 16x36 grid\n");
	}
	kt.decode = kt_set_report_config();
	if (!kt.decode) {
		/*
		 * No report program, no decoder.  Polling anyway would only read
		 * messages this driver cannot lay out and print a framing error
		 * every poll interval, which is a console flood on the only
		 * readable channel this port has.
		 */
		printf("KEBAB TOUCH: the chip did not accept the report program, so the poll thread is not started and there is no /dev/kebab_touch; the event path needs a decoder\n");
		return 0;
	}

	if (kernel_thread_start((thread_continue_t)kt_thread, NULL,
	    &thread) != KERN_SUCCESS) {
		printf("KEBAB TOUCH: no polling thread\n");
		return -1;
	}
	thread_deallocate(thread);

	/*
	 * Only publish the node once there is a thread that can fill it. Code
	 * the other way round would leave a readable device that never blocks
	 * and never returns a record if thread creation failed.
	 */
	if (kt.decode && kt.queue_ok) {
		kt_dev_register();
	} else if (!kt.decode) {
		printf("KEBAB TOUCH: report decode is off; no /dev/kebab_touch "
		    "because there would be nothing to record\n");
	} else {
		printf("KEBAB TOUCH: queue selftest failed; no "
		    "/dev/kebab_touch\n");
	}

	printf("KEBAB TOUCH: polling for reports, decode %s, dump %s, move "
	    "lines every %u ms\n", kt.decode ? "on" : "off",
	    kt.dump ? "on" : "off", kt.move_ms);
	return 0;
}

void
kebab_touch_probe(void)
{
	kebab_geni_transaction_lock();
	kebab_touch_probe_locked();
	kebab_geni_transaction_unlock();
}

void
kebab_touch_init_now(void)
{
	kebab_geni_transaction_lock();
	kebab_touch_init_now_locked();
	kebab_geni_transaction_unlock();
}

int
kebab_touch_start(void)
{
	kebab_geni_transaction_lock();
	int result = kebab_touch_start_locked();
	kebab_geni_transaction_unlock();
	return result;
}

void
kebab_touch_get_status(struct kebab_touch_status *status)
{
	kebab_geni_transaction_lock();
	*status = (struct kebab_touch_status) {
		.max_x = kt.max_x, .max_y = kt.max_y, .max_objects = kt.max_objects,
		.identified = kt.tcm_seen, .decoding = kt.decode, .queue_ok = kt.queue_ok,
		.device_node = kt.dev_ready, .unavailable = kt.unavailable,
	};
	kebab_geni_transaction_unlock();
}
#endif /* KEBAB */
