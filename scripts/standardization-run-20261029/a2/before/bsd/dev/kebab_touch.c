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

/* arm/machine_routines.h declares this behind a guard this build does not
 * satisfy; the kebab UFS and USB drivers spell it out for the same reason. */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

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

#define KT_SE13_PA_DEFAULT      0x00a94000u
#define KT_SE13_SIZE            0x00004000u
#define KT_WRAP_PA_DEFAULT      0x00ac0000u
#define KT_WRAP_SIZE            0x00002000u
#define KT_TLMM_PA              0x0f000000u
/*
 * The TLMM block is 16 MiB in the device tree, but ml_io_map() allocates from
 * the kernel's 8 MiB io_submap and passes KMA_NOFAIL, so an over-size request
 * panics in kmem_alloc instead of returning 0.  This driver touches only the
 * SOUTH-tile per-pin registers (GPIO 36..39, so offsets 0x24000..0x27000 from
 * the tile base); 0x40000 covers SOUTH-tile pins 0..63.  Do not widen this
 * back to the full block.
 */
#define KT_TLMM_SIZE            0x00040000u

/*
 * Timing for the powered bring-up.  The rails must be settled before the reset
 * edge, and the chip needs time after the release before it answers the first
 * transfer; 15 ms and 20 ms sit inside the 10-20 ms the hardware was measured
 * in, and neither is a datasheet number.
 */
#define KT_RAIL_SETTLE_MS       15
#define KT_RESET_SETTLE_MS      20

#define KT_I2C_ADDR             0x4b    /* synaptics-s3908@4B; see the DT note */
#define KT_PIN_SDA              36      /* qupv3_se13_i2c_active: gpio36 */
#define KT_PIN_SCL              37      /* qupv3_se13_i2c_active: gpio37 */
#define KT_PIN_RESET            38      /* reset-gpio */
#define KT_PIN_IRQ              39      /* irq-gpio, unused: we poll */

/* ------------------------------------------------------- QUP/GENI registers */

#define QUP_HW_VER              0x4     /* wrapper, geni_se_get_qup_hw_version */

#define GENI_FORCE_DEFAULT_REG  0x20
#define GENI_OUTPUT_CTRL        0x24
#define GENI_CGC_CTRL           0x28
#define SE_GENI_STATUS          0x40
#define GENI_SER_M_CLK_CFG      0x48
#define GENI_IF_DISABLE_RO      0x64
#define GENI_FW_REVISION_RO     0x68
#define SE_GENI_CLK_SEL         0x7c
#define SE_GENI_BYTE_GRAN       0x254
#define SE_GENI_DMA_MODE_EN     0x258
#define SE_GENI_TX_PACKING_CFG0 0x260
#define SE_GENI_TX_PACKING_CFG1 0x264
#define SE_GENI_RX_PACKING_CFG0 0x284
#define SE_GENI_RX_PACKING_CFG1 0x288
#define SE_GENI_M_CMD0          0x600
#define SE_GENI_M_CMD_CTRL_REG  0x604
#define SE_GENI_M_IRQ_STATUS    0x610
#define SE_GENI_M_IRQ_EN        0x614
#define SE_GENI_M_IRQ_CLEAR     0x618
#define SE_GENI_S_CMD0          0x630
#define SE_GENI_S_IRQ_STATUS    0x640
#define SE_GENI_S_IRQ_EN        0x644
#define SE_GENI_S_IRQ_CLEAR     0x648
#define SE_GENI_TX_FIFO         0x700
#define SE_GENI_RX_FIFO         0x780
#define SE_GENI_TX_FIFO_STATUS  0x800
#define SE_GENI_RX_FIFO_STATUS  0x804
#define SE_GENI_TX_WATERMARK_REG 0x80c
#define SE_GENI_RX_WATERMARK_REG 0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_GENI_IOS             0x908
#define SE_GSI_EVENT_EN         0xe18
#define SE_IRQ_EN               0xe1c
#define SE_HW_PARAM_0           0xe24
#define SE_DMA_GENERAL_CFG      0xe30

/* I2C sequencer registers (i2c-qcom-geni.c) */
#define SE_I2C_TX_TRANS_LEN     0x26c
#define SE_I2C_RX_TRANS_LEN     0x270
#define SE_I2C_SCL_COUNTERS     0x278

/* GENI_FW_REVISION_RO: protocol in bits 15:8, GENI_SE_I2C == 3 */
#define FW_REV_PROTOCOL_MSK     0x0000ff00u
#define FW_REV_PROTOCOL_SHFT    8
#define GENI_SE_I2C_PROTO       3

/* GENI_M_CMD0: opcode in bits 31:27, parameters in 26:0 */
#define M_OPCODE_SHFT           27

/* I2C master opcodes */
#define I2C_WRITE               0x1
#define I2C_READ                0x2

/* I2C command parameters: the 7-bit slave address in bits 15:9. */
#define SLV_ADDR_SHFT           9
#define SLV_ADDR_MSK            0x0000fe00u

/* Master interrupt status/enable bits (geni-se.h). */
#define M_CMD_DONE              (1u << 0)
#define M_CMD_OVERRUN           (1u << 1)
#define M_ILLEGAL_CMD           (1u << 2)
#define M_CMD_FAILURE           (1u << 3)
#define M_CMD_CANCEL            (1u << 4)
#define M_CMD_ABORT             (1u << 5)
#define M_GP_IRQ_0              (1u << 9)
#define M_GP_IRQ_1_NACK         (1u << 10)
#define M_RX_FIFO_WATERMARK     (1u << 26)
#define M_RX_FIFO_LAST          (1u << 27)
#define M_TX_FIFO_WATERMARK     (1u << 30)

#define M_ERRORS                (M_CMD_OVERRUN | M_ILLEGAL_CMD | \
	M_CMD_FAILURE | M_CMD_ABORT | M_GP_IRQ_1_NACK)

/*
 * SE_GENI_M_CMD_CTRL_REG is not the status register, and its bits are
 * numbered differently (geni-se.h): CANCEL is bit 2 and ABORT bit 1, while
 * the status bits of the same names are 4 and 5.  Writing a status bit into
 * the control register is a silent no-op.
 */
#define M_GENI_CMD_ACTIVE       (1u << 0)       /* GENI_STATUS bit 0 */
#define M_GENI_CMD_ABORT_REQ    (1u << 1)       /* CMD_CTRL: abort */
#define M_GENI_CMD_CANCEL_REQ   (1u << 2)       /* CMD_CTRL: cancel */

/* M_COMMON_GENI_M_IRQ_EN: bits 6:1 plus the FIFO access error bits. */
#define M_IRQ_COMMON            (0x0000007eu | 0x03c00000u | 0x30000000u)
/* S_COMMON_GENI_S_IRQ_EN, from the same header; the secondary engine is idle
 * in I2C mode but the register is still programmed by geni_se_init(). */
#define S_IRQ_COMMON            (0x0000003eu | 0x00003e00u | 0x03000000u)
/* FIFO mode: command done and the watermarks select_fifo_mode() adds. */
#define M_IRQ_FIFO              (M_CMD_DONE | M_TX_FIFO_WATERMARK | \
	M_RX_FIFO_WATERMARK | M_RX_FIFO_LAST)

/* RX_FIFO_STATUS */
#define RX_FIFO_WC_MSK          0x01ffffffu

/* geni_se_io_init() values. */
#define DEFAULT_CGC_EN          0x0000007fu
#define DEFAULT_IO_OUTPUT_CTRL  0x0000007fu
#define DMA_GENERAL_CFG_VAL     0x0000000fu

/*
 * Packing configuration produced by geni_se_config_packing(se, 8, 4, true,
 * true, true): four 4x8 vectors, MSB to LSB, stop bit on the last vector.
 * cfg0 = 0x07f8fe, cfg1 = 0x0ffefe (computed from the PACKING_* shifts in
 * qcom-geni-se.c).
 */
#define PACK_CFG0               0x0007f8feu
#define PACK_CFG1               0x000ffefeu

/* SCL timing, from geni_i2c_clk_map[] {rate, div, high, low, cycle}. */
#define KT_SER_CLK_CFG(div)     ((((uint32_t)(div)) << 4) | 1u)
#define KT_SCL_COUNTERS(high, low, cycle) \
	((((uint32_t)(high)) << 20) | (((uint32_t)(low)) << 10) | \
	(uint32_t)(cycle))

/* ---------------------------------------------------------- TLMM registers */

/*
 * Kona's TLMM is tiled.  drivers/pinctrl/qcom/pinctrl-kona.c defines the tile
 * bases NORTH 0x900000, SOUTH 0x500000, WEST 0x100000, EAST 0 and computes
 * every pin as tile + 0x1000 * pin; GPIO 36..39 (SDA, SCL, reset, IRQ) are in
 * the SOUTH tile.  A flat "block base + 0x1000 * pin" address lands in a hole
 * between tiles and reads as zero, which is exactly what the first hardware
 * run showed.  kt.tlmm is therefore the mapped SOUTH tile, not the block base.
 */
#define TLMM_TILE_SOUTH         0x00500000u
#define TLMM_REG_SIZE           0x1000u
#define TLMM_CTL(pin)           (TLMM_REG_SIZE * (pin))
#define TLMM_IO(pin)            (TLMM_REG_SIZE * (pin) + 0x4u)
#define TLMM_MUX_MSK            (0xfu << 2)     /* 10 functions per pin */
#define TLMM_MUX_QUP13          (1u << 2)       /* "qup13" is entry 1 */
#define TLMM_PULL_MSK           0x3u            /* 0 == bias-disable */
#define TLMM_DRV_MSK            (0x7u << 6)     /* field 0 == 2 mA */
#define TLMM_OE                 (1u << 9)
#define TLMM_OUT                (1u << 1)

/* ------------------------------------------------------------ TCM protocol */

/*
 * The protocol itself -- framing, command codes, report ids, boot modes, the
 * report program and the report decoder -- lives in bsd/dev/kebab_touch_tcm.h,
 * which is the host-tested copy of package/KebabTouchTcm.h.  This file owns the
 * transport and the order of the bring-up; it owns no wire format.
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
	vm_offset_t se;
	vm_offset_t wrap;
	vm_offset_t tlmm;
	bool        raw;
	bool        bb;                 /* kebab_touch_bb=1: GPIO bit-banged I2C */
	bool        se_dead;            /* recovery failed; no more transfers */
	bool        se_initialized;     /* se_init ran and did not fail */
	vm_offset_t gcc_en;             /* GCC page with the SE13 enable bits */
	vm_offset_t gcc_halt;           /* GCC page with the halt registers */
	bool        tcm_seen;
	bool        decode;
	bool        dump;
	bool        queue_ok;
	bool        dev_ready;
	uint32_t    max_x;
	uint32_t    max_y;
	uint32_t    max_objects;
	uint32_t    errors;
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
static void
kt_trace(const char *fmt, ...)
{
	char line[192];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n <= 0) {
		return;
	}
	printf("%s", line);
	if (kebab_usb_console_ready()) {
		kebab_usb_log_flush();
	}
}

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

/* --------------------------------------------------------------- helpers */

static inline uint32_t
kt_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile ("dsb sy" ::: "memory");
	return v;
}

static inline void
kt_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
kt_se_rd(uint32_t off)
{
	return kt_rd(kt.se, off);
}

static inline void
kt_se_wr(uint32_t off, uint32_t v)
{
	kt_wr(kt.se, off, v);
}

static void
kt_sleep_ms(uint32_t ms)
{
	delay(ms * 1000);
}

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

struct kt_reg {
	const char *name;
	uint32_t    off;
};

static const struct kt_reg kt_se_regs[] = {
	{ "GENI_STATUS",          SE_GENI_STATUS },
	{ "GENI_FW_REVISION_RO",  GENI_FW_REVISION_RO },
	{ "GENI_IF_DISABLE_RO",   GENI_IF_DISABLE_RO },
	{ "SER_M_CLK_CFG",        GENI_SER_M_CLK_CFG },
	{ "SE_GENI_DMA_MODE_EN",  SE_GENI_DMA_MODE_EN },
	{ "SE_HW_PARAM_0",        SE_HW_PARAM_0 },
	{ "SE_IRQ_EN",            SE_IRQ_EN },
	{ "M_CMD0",               SE_GENI_M_CMD0 },
	{ "M_IRQ_STATUS",         SE_GENI_M_IRQ_STATUS },
	{ "M_IRQ_EN",             SE_GENI_M_IRQ_EN },
	{ "S_IRQ_STATUS",         SE_GENI_S_IRQ_STATUS },
	{ "TX_FIFO_STATUS",       SE_GENI_TX_FIFO_STATUS },
	{ "RX_FIFO_STATUS",       SE_GENI_RX_FIFO_STATUS },
	{ "GENI_IOS",             SE_GENI_IOS },
	{ "I2C_SCL_COUNTERS",     SE_I2C_SCL_COUNTERS },
};

static void
kt_dump_regs(const char *when)
{
	uint32_t i;
	uint32_t proto;

	printf("KEBAB TOUCH: SE13 register dump (%s)\n", when);
	if (kt.se == 0) {
		printf("KEBAB TOUCH:   SE13 not mapped (bit-bang transport); TLMM only\n");
	} else {
		printf("KEBAB TOUCH:   wrapper QUP_HW_VER [0x004] 0x%08x\n",
		    kt.wrap != 0 ? kt_rd(kt.wrap, QUP_HW_VER) : 0);
		proto = (kt_se_rd(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >>
		    FW_REV_PROTOCOL_SHFT;
		printf("KEBAB TOUCH:   protocol %u (want %u for I2C), GENI_STATUS 0x%08x\n",
		    proto, GENI_SE_I2C_PROTO, kt_se_rd(SE_GENI_STATUS));
		for (i = 0; i < sizeof(kt_se_regs) / sizeof(kt_se_regs[0]); i++) {
			printf("KEBAB TOUCH:   %-20s [0x%03x] 0x%08x\n",
			    kt_se_regs[i].name, kt_se_regs[i].off,
			    kt_se_rd(kt_se_regs[i].off));
		}
	}
	if (kt.tlmm != 0) {
		printf("KEBAB TOUCH:   TLMM ctl %u..%u 0x%08x 0x%08x 0x%08x 0x%08x\n",
		    KT_PIN_SDA, KT_PIN_IRQ,
		    kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_SDA)),
		    kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_SCL)),
		    kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_RESET)),
		    kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_IRQ)));
		printf("KEBAB TOUCH:   TLMM io  %u..%u 0x%08x 0x%08x 0x%08x 0x%08x\n",
		    KT_PIN_SDA, KT_PIN_IRQ,
		    kt_rd(kt.tlmm, TLMM_IO(KT_PIN_SDA)),
		    kt_rd(kt.tlmm, TLMM_IO(KT_PIN_SCL)),
		    kt_rd(kt.tlmm, TLMM_IO(KT_PIN_RESET)),
		    kt_rd(kt.tlmm, TLMM_IO(KT_PIN_IRQ)));
	}
}

/* -------------------------------------------------------- board plumbing */

/*
 * Mux SDA/SCL to the QUP function. The values are the device tree's
 * qupv3_se13_i2c_active state: function "qup13", bias-disable, 2 mA.
 */
static void
kt_pins_config(void)
{
	uint32_t pin;

	for (pin = KT_PIN_SDA; pin <= KT_PIN_SCL; pin++) {
		uint32_t old;
		uint32_t val;

		kt_trace("KEBAB TOUCH: qup13 pin %u: reading TLMM_CTL at 0x%08x\n",
		    pin, KT_TLMM_PA + TLMM_TILE_SOUTH +
		    TLMM_REG_SIZE * pin);
		old = kt_rd(kt.tlmm, TLMM_CTL(pin));
		val = old;

		val &= ~TLMM_MUX_MSK;
		val |= TLMM_MUX_QUP13;
		val &= ~TLMM_PULL_MSK;
		val &= ~TLMM_DRV_MSK;
		kt_trace("KEBAB TOUCH: qup13 pin %u: ctl 0x%08x -> 0x%08x\n",
		    pin, old, val);
		kt_wr(kt.tlmm, TLMM_CTL(pin), val);
		printf("KEBAB TOUCH: pin %u mux qup13, ctl 0x%08x -> 0x%08x\n",
		    pin, old, kt_rd(kt.tlmm, TLMM_CTL(pin)));
	}
}

/* Drive RESET as a plain GPIO; level is the electrical level. */
static void
kt_reset_level(uint32_t level)
{
	uint32_t ctl;
	uint32_t io;

	kt_trace("KEBAB TOUCH: reset pin %u level %u: reading TLMM_CTL/TLMM_IO\n",
	    KT_PIN_RESET, level);
	ctl = kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_RESET));
	io = kt_rd(kt.tlmm, TLMM_IO(KT_PIN_RESET));
	kt_trace("KEBAB TOUCH: reset ctl 0x%08x io 0x%08x -> level %u\n",
	    ctl, io, level);

	ctl &= ~TLMM_MUX_MSK;           /* mux 0 == plain GPIO */
	ctl &= ~TLMM_PULL_MSK;
	ctl &= ~TLMM_DRV_MSK;
	ctl |= TLMM_OE;

	if (level != 0) {
		io |= TLMM_OUT;
	} else {
		io &= ~TLMM_OUT;
	}
	/* value before output enable, so the pin does not glitch */
	kt_wr(kt.tlmm, TLMM_IO(KT_PIN_RESET), io);
	kt_wr(kt.tlmm, TLMM_CTL(KT_PIN_RESET), ctl);
}

/*
 * Reset pulse. Mode 1 is the active-low arrangement the vendor drivers use
 * (drive low, release high); mode 2 drives the opposite polarity, for the case
 * where the first hardware run shows the chip never answering.
 */
static void
kt_reset_pulse(uint32_t mode)
{
	uint32_t assert_level = (mode == 2) ? 1u : 0u;
	uint32_t release_level = (mode == 2) ? 0u : 1u;

	kt_reset_level(assert_level);
	kt_sleep_ms(20);
	kt_reset_level(release_level);
	printf("KEBAB TOUCH: reset pulse (mode %u), ctl 0x%08x io 0x%08x\n",
	    mode, kt_rd(kt.tlmm, TLMM_CTL(KT_PIN_RESET)),
	    kt_rd(kt.tlmm, TLMM_IO(KT_PIN_RESET)));
	kt_sleep_ms(200);
}

/* ------------------------------------------------------------ SE bring-up */

/*
 * What geni_se_init() + geni_se_select_mode(FIFO) + geni_se_config_packing()
 * do, written out without the Linux clock and interconnect layers.
 */
static bool
kt_se_init(uint32_t hz)
{
	uint32_t v;
	uint32_t depth;
	uint32_t proto;

	kt_trace("KEBAB TOUCH: se_init: clear IRQs\n");
	kt_se_wr(SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	kt_se_wr(SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
	kt_se_wr(SE_GENI_DMA_MODE_EN, kt_se_rd(SE_GENI_DMA_MODE_EN) & ~1u);
	kt_se_wr(SE_GSI_EVENT_EN, 0);

	/* geni_se_io_init() */
	kt_se_wr(GENI_CGC_CTRL, kt_se_rd(GENI_CGC_CTRL) | DEFAULT_CGC_EN);
	kt_se_wr(SE_DMA_GENERAL_CFG,
	    kt_se_rd(SE_DMA_GENERAL_CFG) | DMA_GENERAL_CFG_VAL);
	kt_se_wr(GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT_CTRL);
	kt_trace("KEBAB TOUCH: se_init: force default, output ctrl\n");
	kt_se_wr(GENI_FORCE_DEFAULT_REG, 1);

	/* geni_se_io_set_mode() */
	kt_se_wr(SE_IRQ_EN, kt_se_rd(SE_IRQ_EN) | 0xfu);
	kt_se_wr(SE_GENI_DMA_MODE_EN, kt_se_rd(SE_GENI_DMA_MODE_EN) & ~1u);
	kt_se_wr(SE_GSI_EVENT_EN, 0);

	/* Watermarks, sized from the SE's own TX FIFO width. */
	depth = (kt_se_rd(SE_HW_PARAM_0) >> 24) & 0x3fu;
	if (depth == 0 || depth > 32) {
		depth = 16;
	}
	kt_se_wr(SE_GENI_RX_WATERMARK_REG, depth - 1);
	kt_se_wr(SE_GENI_RX_RFR_WATERMARK_REG, depth);

	kt_se_wr(SE_GENI_M_IRQ_EN, kt_se_rd(SE_GENI_M_IRQ_EN) | M_IRQ_COMMON);
	kt_se_wr(SE_GENI_S_IRQ_EN, kt_se_rd(SE_GENI_S_IRQ_EN) | S_IRQ_COMMON);
	kt_se_wr(SE_GENI_M_IRQ_EN, kt_se_rd(SE_GENI_M_IRQ_EN) | M_IRQ_FIFO);

	/* geni_se_config_packing(8 bits, 4 words, msb first) */
	kt_trace("KEBAB TOUCH: se_init: packing + byte granularity\n");
	kt_se_wr(SE_GENI_TX_PACKING_CFG0, PACK_CFG0);
	kt_se_wr(SE_GENI_TX_PACKING_CFG1, PACK_CFG1);
	kt_se_wr(SE_GENI_RX_PACKING_CFG0, PACK_CFG0);
	kt_se_wr(SE_GENI_RX_PACKING_CFG1, PACK_CFG1);
	kt_se_wr(SE_GENI_BYTE_GRAN, 0);

	/* qcom_geni_i2c_conf(): internal SE clock, then the SCL timing. */
	kt_trace("KEBAB TOUCH: se_init: clock select + SCL counters\n");
	kt_se_wr(SE_GENI_CLK_SEL, 0);
	if (hz == 400000u) {
		kt_se_wr(GENI_SER_M_CLK_CFG, KT_SER_CLK_CFG(2));
		kt_se_wr(SE_I2C_SCL_COUNTERS, KT_SCL_COUNTERS(5, 12, 24));
	} else {
		kt_se_wr(GENI_SER_M_CLK_CFG, KT_SER_CLK_CFG(7));
		kt_se_wr(SE_I2C_SCL_COUNTERS, KT_SCL_COUNTERS(10, 11, 26));
	}

	v = kt_se_rd(SE_GENI_STATUS);
	if ((v & 0x1u) != 0) {
		printf("KEBAB TOUCH: sequencer already active, GENI_STATUS 0x%08x\n",
		    v);
	}

	proto = (kt_se_rd(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >>
	    FW_REV_PROTOCOL_SHFT;
	if (proto != GENI_SE_I2C_PROTO) {
		printf("KEBAB TOUCH: SE13 protocol is %u, not %u (I2C): the "
		    "bootloader did not configure this SE for I2C and this driver "
		    "does not know how to select it\n", proto, GENI_SE_I2C_PROTO);
		return false;
	}
	return true;
}

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
#define KT_GCC_PA               0x00100000u
#define KT_GCC_PAGE_EN          0x00052000u
#define KT_GCC_PAGE_HALT        0x00018000u
#define KT_GCC_ENABLE           0x00052008u
#define KT_GCC_HALT_M_AHB       0x00018004u
#define KT_GCC_HALT_S_AHB       0x00018008u
#define KT_GCC_HALT_WRAP1_S5    0x000185fcu
#define KT_GCC_EN_S5            (1u << 27)
#define KT_GCC_EN_M_AHB         (1u << 20)
#define KT_GCC_EN_S_AHB         (1u << 21)

static void
kt_gcc_probe(void)
{
	uint32_t en, m_ahb, s_ahb, s5;

	if (kt.gcc_en == 0) {
		kt.gcc_en = ml_io_map(KT_GCC_PA + KT_GCC_PAGE_EN, 0x1000);
		kt.gcc_halt = ml_io_map(KT_GCC_PA + KT_GCC_PAGE_HALT, 0x1000);
	}
	if (kt.gcc_en == 0 || kt.gcc_halt == 0) {
		printf("KEBAB TOUCH: cannot map the GCC pages; clock state unknown\n");
		return;
	}
	en = kt_rd(kt.gcc_en, KT_GCC_ENABLE - KT_GCC_PAGE_EN);
	kt_trace("KEBAB TOUCH: GCC 0x%05x = 0x%08x: wrap1_s5 %u m_ahb %u s_ahb %u\n",
	    KT_GCC_ENABLE, en, (en & KT_GCC_EN_S5) != 0,
	    (en & KT_GCC_EN_M_AHB) != 0, (en & KT_GCC_EN_S_AHB) != 0);
	m_ahb = kt_rd(kt.gcc_halt, KT_GCC_HALT_M_AHB - KT_GCC_PAGE_HALT);
	s_ahb = kt_rd(kt.gcc_halt, KT_GCC_HALT_S_AHB - KT_GCC_PAGE_HALT);
	s5 = kt_rd(kt.gcc_halt, KT_GCC_HALT_WRAP1_S5 - KT_GCC_PAGE_HALT);
	kt_trace("KEBAB TOUCH: GCC halt m_ahb 0x%08x s_ahb 0x%08x wrap1_s5 0x%08x\n",
	    m_ahb, s_ahb, s5);
}

/*
 * Read-only GENI state.  Every register here was already read by the boot
 * path on this board, so the probe adds no new access class.
 */
static void
kt_geni_probe(void)
{
	uint32_t rev;

	rev = kt_se_rd(GENI_FW_REVISION_RO);
	kt_trace("KEBAB TOUCH: probe wrapper QUP_HW_VER 0x%08x\n",
	    kt.wrap != 0 ? kt_rd(kt.wrap, QUP_HW_VER) : 0u);
	kt_trace("KEBAB TOUCH: probe GENI_FW_REVISION_RO 0x%08x protocol %u\n",
	    rev, (rev & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT);
	kt_trace("KEBAB TOUCH: probe GENI_STATUS 0x%08x M_IRQ_STATUS 0x%08x "
	    "S_IRQ_STATUS 0x%08x\n", kt_se_rd(SE_GENI_STATUS),
	    kt_se_rd(SE_GENI_M_IRQ_STATUS), kt_se_rd(SE_GENI_S_IRQ_STATUS));
	kt_trace("KEBAB TOUCH: probe DMA_MODE_EN 0x%08x CGC_CTRL 0x%08x\n",
	    kt_se_rd(SE_GENI_DMA_MODE_EN), kt_se_rd(GENI_CGC_CTRL));
	kt_trace("KEBAB TOUCH: probe TX_WATERMARK 0x%08x M_IRQ_EN 0x%08x\n",
	    kt_se_rd(SE_GENI_TX_WATERMARK_REG), kt_se_rd(SE_GENI_M_IRQ_EN));
	kt_trace("KEBAB TOUCH: probe SER_M_CLK_CFG 0x%08x SCL_COUNTERS 0x%08x\n",
	    kt_se_rd(GENI_SER_M_CLK_CFG), kt_se_rd(SE_I2C_SCL_COUNTERS));
	kt_trace("KEBAB TOUCH: probe TX_FIFO_STATUS 0x%08x RX_FIFO_STATUS 0x%08x\n",
	    kt_se_rd(SE_GENI_TX_FIFO_STATUS), kt_se_rd(SE_GENI_RX_FIFO_STATUS));
}

static void
kt_pins_probe(void)
{
	static const uint32_t pins[] = {
		KT_PIN_SDA, KT_PIN_SCL, KT_PIN_RESET, KT_PIN_IRQ
	};
	uint32_t i;

	for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		kt_trace("KEBAB TOUCH: probe pin %u ctl 0x%08x io 0x%08x\n",
		    pins[i], kt_rd(kt.tlmm, TLMM_CTL(pins[i])),
		    kt_rd(kt.tlmm, TLMM_IO(pins[i])));
	}
}

/*
 * Console entry points.  Neither runs at boot unless a boot argument asks for
 * it: the previous run showed that a bad GENI access can take the USB console
 * down with it, and the console is the only recovery channel that does not
 * need a key press on the phone.
 */
void
kebab_touch_probe(void)
{
	if (kt.se == 0) {
		kt.se = ml_io_map(KT_SE13_PA_DEFAULT, KT_SE13_SIZE);
		kt.wrap = ml_io_map(KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE);
	}
	if (kt.tlmm == 0) {
		kt.tlmm = ml_io_map(KT_TLMM_PA + TLMM_TILE_SOUTH, KT_TLMM_SIZE);
	}
	if (kt.se == 0 || kt.tlmm == 0) {
		printf("KEBAB TOUCH: probe cannot map SE13 or TLMM\n");
		return;
	}
	kt_trace("KEBAB TOUCH: read-only probe begin; no register is written\n");
	kt_trace("KEBAB TOUCH: state: initialized %u, dead %u, rails %u, "
	    "unavailable %u, last mode %s\n", kt.se_initialized, kt.se_dead,
	    kt.rails_on, kt.unavailable,
	    kt.mode_known ? kt_mode_name(kt.mode) : "never read");
	kt_gcc_probe();
	kt_geni_probe();
	kt_pins_probe();
	kt_trace("KEBAB TOUCH: read-only probe end\n");
}

void
kebab_touch_init_now(void)
{
	if (kt.se == 0) {
		kt.se = ml_io_map(KT_SE13_PA_DEFAULT, KT_SE13_SIZE);
		kt.wrap = ml_io_map(KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE);
	}
	if (kt.tlmm == 0) {
		kt.tlmm = ml_io_map(KT_TLMM_PA + TLMM_TILE_SOUTH, KT_TLMM_SIZE);
	}
	if (kt.se == 0 || kt.tlmm == 0) {
		printf("KEBAB TOUCH: TOUCHINIT cannot map SE13 or TLMM\n");
		return;
	}
	kt_trace("KEBAB TOUCH: TOUCHINIT begin; writes follow\n");
	kt_gcc_probe();
	kt_pins_config();
	kt_reset_pulse(1);
	if (!kt_se_init(100000)) {
		return;
	}
	kt.se_initialized = true;
	kt_identify();
	kt_trace("KEBAB TOUCH: TOUCHINIT end\n");
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

#define KT_BB_HALF_US   5u      /* ~100 kHz, like the reference port */
#define KT_BB_STRETCH   200u    /* 1 ms of clock stretching, then fail */

static bool
kt_bb_level(uint32_t pin)
{
	return (kt_rd(kt.tlmm, TLMM_IO(pin)) & 1u) != 0;
}

static void
kt_bb_line(uint32_t pin, bool high)
{
	uint32_t ctl = kt_rd(kt.tlmm, TLMM_CTL(pin));

	if (!high) {
		kt_wr(kt.tlmm, TLMM_IO(pin), 0);
	}
	ctl &= ~(TLMM_MUX_MSK | TLMM_PULL_MSK | TLMM_DRV_MSK | TLMM_OE);
	ctl |= 3u;                      /* pull-up, mux 0 == plain GPIO */
	if (!high) {
		ctl |= TLMM_OE;
	}
	kt_wr(kt.tlmm, TLMM_CTL(pin), ctl);
}

static uint32_t kt_bb_stretch_bails;

static bool
kt_bb_clock_high(void)
{
	kt_bb_line(KT_PIN_SCL, true);
	for (uint32_t i = 0; i < KT_BB_STRETCH; i++) {
		if (kt_bb_level(KT_PIN_SCL)) {
			return true;
		}
		delay(KT_BB_HALF_US);
	}
	if (++kt_bb_stretch_bails <= 4) {
		kt_trace("KEBAB TOUCH: SCL did not rise within %u us (SDA %u); bailing out of this transaction\n",
		    KT_BB_STRETCH * KT_BB_HALF_US, kt_bb_level(KT_PIN_SDA));
	}
	return false;
}

static bool
kt_bb_start(void)
{
	kt_bb_line(KT_PIN_SDA, true);
	delay(KT_BB_HALF_US);
	if (!kt_bb_clock_high() || !kt_bb_level(KT_PIN_SDA)) {
		return false;
	}
	delay(KT_BB_HALF_US);
	kt_bb_line(KT_PIN_SDA, false);
	delay(KT_BB_HALF_US);
	kt_bb_line(KT_PIN_SCL, false);
	return true;
}

static void
kt_bb_stop(void)
{
	kt_bb_line(KT_PIN_SCL, false);
	kt_bb_line(KT_PIN_SDA, false);
	delay(KT_BB_HALF_US);
	(void)kt_bb_clock_high();
	delay(KT_BB_HALF_US);
	kt_bb_line(KT_PIN_SDA, true);
	delay(KT_BB_HALF_US);
}

static bool
kt_bb_send(uint8_t byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		kt_bb_line(KT_PIN_SDA, ((byte >> bit) & 1u) != 0);
		delay(KT_BB_HALF_US);
		if (!kt_bb_clock_high()) {
			return false;
		}
		delay(KT_BB_HALF_US);
		kt_bb_line(KT_PIN_SCL, false);
	}
	kt_bb_line(KT_PIN_SDA, true);
	delay(KT_BB_HALF_US);
	if (!kt_bb_clock_high()) {
		return false;
	}
	delay(KT_BB_HALF_US);
	bool ack = !kt_bb_level(KT_PIN_SDA);
	kt_bb_line(KT_PIN_SCL, false);
	return ack;
}

static bool
kt_bb_receive(uint8_t *byte, bool ack)
{
	uint32_t value = 0;

	kt_bb_line(KT_PIN_SDA, true);
	for (uint32_t bit = 0; bit < 8; bit++) {
		delay(KT_BB_HALF_US);
		if (!kt_bb_clock_high()) {
			return false;
		}
		delay(KT_BB_HALF_US);
		value = (value << 1) | (kt_bb_level(KT_PIN_SDA) ? 1u : 0u);
		kt_bb_line(KT_PIN_SCL, false);
	}
	kt_bb_line(KT_PIN_SDA, !ack);
	delay(KT_BB_HALF_US);
	if (!kt_bb_clock_high()) {
		return false;
	}
	delay(KT_BB_HALF_US);
	kt_bb_line(KT_PIN_SCL, false);
	kt_bb_line(KT_PIN_SDA, true);
	*byte = (uint8_t)value;
	return true;
}

static int
kt_bb_fail(const char *what, uint32_t phase)
{
	if (kt.errors < 8) {
		kt_trace("KEBAB TOUCH: bit-bang %s failed at phase %u (SDA %u SCL %u)\n",
		    what, phase, kt_bb_level(KT_PIN_SDA),
		    kt_bb_level(KT_PIN_SCL));
	}
	kt.errors++;
	kt_bb_stop();
	return -1;
}

static int
kt_bb_write(const uint8_t *buf, uint32_t len)
{
	if (len == 0 || len > 64) {
		return -1;
	}
	kt_trace("KEBAB TOUCH: bb write %u bytes (first 0x%02x)\n", len, buf[0]);
	if (!kt_bb_start()) {
		return kt_bb_fail("start", 0);
	}
	if (!kt_bb_send((uint8_t)(KT_I2C_ADDR << 1))) {
		return kt_bb_fail("address", 1);
	}
	for (uint32_t i = 0; i < len; i++) {
		if (!kt_bb_send(buf[i])) {
			return kt_bb_fail("write", 2 + i);
		}
	}
	kt_bb_stop();
	return 0;
}

static int
kt_bb_read(uint8_t *buf, uint32_t len)
{
	if (len == 0 || len > 128) {
		return -1;
	}
	kt_trace("KEBAB TOUCH: bb read %u bytes\n", len);
	if (!kt_bb_start()) {
		return kt_bb_fail("start", 0);
	}
	if (!kt_bb_send((uint8_t)((KT_I2C_ADDR << 1) | 1u))) {
		return kt_bb_fail("address", 1);
	}
	for (uint32_t i = 0; i < len; i++) {
		if (!kt_bb_receive(&buf[i], i + 1 < len)) {
			return kt_bb_fail("read", 2 + i);
		}
	}
	kt_bb_stop();
	return 0;
}

/*
 * Show the pin state the previous boot stage left, then take SDA/SCL for
 * plain GPIO.  This is deliberately one read-only print before any write:
 * if the mux or pad layout differs from the QUP path's assumption it is
 * visible in the log before the first write happens.
 */
static void
kt_bb_pins_config(void)
{
	static const uint32_t pins[] = {
		KT_PIN_SDA, KT_PIN_SCL, KT_PIN_RESET, KT_PIN_IRQ
	};
	uint32_t i;

	for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		uint32_t ctl;
		uint32_t io;

		/* Print first: if this read hangs, the last line names the pin. */
		kt_trace("KEBAB TOUCH: reading pin %u TLMM_CTL at 0x%08x\n",
		    pins[i], KT_TLMM_PA + TLMM_TILE_SOUTH +
		    TLMM_REG_SIZE * pins[i]);
		ctl = kt_rd(kt.tlmm, TLMM_CTL(pins[i]));
		io = kt_rd(kt.tlmm, TLMM_IO(pins[i]));
		kt_trace("KEBAB TOUCH: bit-bang pin %u ctl 0x%08x (mux %u pull %u drive %u oe %u) io 0x%08x\n",
		    pins[i], ctl, (ctl & TLMM_MUX_MSK) >> 2, ctl & TLMM_PULL_MSK,
		    (ctl & TLMM_DRV_MSK) >> 6, (ctl >> 9) & 1u, io);
	}
	for (i = 0; i < 2; i++) {
		uint32_t old = kt_rd(kt.tlmm, TLMM_CTL(pins[i]));
		uint32_t val = old;

		val &= ~(TLMM_MUX_MSK | TLMM_PULL_MSK | TLMM_DRV_MSK |
		    TLMM_OE);
		val |= 3u;              /* mux 0 GPIO, pull-up, released */
		kt_trace("KEBAB TOUCH: writing pin %u ctl 0x%08x -> 0x%08x\n",
		    pins[i], old, val);
		kt_wr(kt.tlmm, TLMM_CTL(pins[i]), val);
		kt_trace("KEBAB TOUCH: pin %u readback 0x%08x\n", pins[i],
		    kt_rd(kt.tlmm, TLMM_CTL(pins[i])));
	}
}

/* ------------------------------------------------------------- I2C master */

static bool kt_se_recover(const char *why);

static uint32_t
kt_i2c_param(void)
{
	return ((uint32_t)KT_I2C_ADDR << SLV_ADDR_SHFT) & SLV_ADDR_MSK;
}

static int
kt_i2c_error(uint32_t status)
{
	if ((status & M_GP_IRQ_1_NACK) != 0) {
		printf("KEBAB TOUCH: no ACK from 0x%02x (M_IRQ_STATUS 0x%08x)\n",
		    KT_I2C_ADDR, status);
	} else {
		printf("KEBAB TOUCH: I2C error, M_IRQ_STATUS 0x%08x GENI_STATUS 0x%08x\n",
		    status, kt_se_rd(SE_GENI_STATUS));
	}
	/*
	 * Keep the status word as the evidence and only clear it after the
	 * decision to recover, so a failed run still shows why it failed.
	 */
	if ((kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) != 0) {
		(void)kt_se_recover("i2c error");
	} else {
		kt_se_wr(SE_GENI_M_IRQ_CLEAR, status);
	}
	return -1;
}

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
static bool
kt_se_recover(const char *why)
{
	uint32_t status = 0;
	uint32_t i;

	status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
	kt_trace("KEBAB TOUCH: recover (%s): GENI_STATUS 0x%08x M_IRQ_STATUS 0x%08x\n",
	    why, kt_se_rd(SE_GENI_STATUS), status);
	if ((kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) == 0) {
		kt_se_wr(SE_GENI_TX_WATERMARK_REG, 0);
		kt_se_wr(SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
		return true;
	}

	kt_se_wr(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL_REQ);
	for (i = 0; i < 100; i++) {
		status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
		if ((status & (M_CMD_CANCEL | M_CMD_DONE)) != 0 ||
		    (kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) == 0) {
			break;
		}
		delay(100);
	}
	if ((kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) != 0) {
		kt_se_wr(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT_REQ);
		for (i = 0; i < 100; i++) {
			status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
			if ((status & M_CMD_ABORT) != 0 ||
			    (kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) == 0) {
				break;
			}
			delay(100);
		}
	}
	status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
	kt_se_wr(SE_GENI_TX_WATERMARK_REG, 0);
	kt_se_wr(SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	if ((kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) != 0) {
		kt.se_dead = true;
		printf("KEBAB TOUCH: SE still active after cancel and abort "
		    "(GENI_STATUS 0x%08x, M_IRQ_STATUS 0x%08x); no further I2C "
		    "this run\n", kt_se_rd(SE_GENI_STATUS), status);
		return false;
	}
	kt_trace("KEBAB TOUCH: recovered: GENI_STATUS 0x%08x, last status 0x%08x\n",
	    kt_se_rd(SE_GENI_STATUS), status);
	return true;
}

static int
kt_i2c_write(const uint8_t *buf, uint32_t len)
{
	uint32_t i;
	uint32_t status = 0;
	uint32_t words;

	if (kt.bb) {
		return kt_bb_write(buf, len);
	}
	if (kt.se_dead) {
		printf("KEBAB TOUCH: SE is marked unusable after a failed "
		    "recovery; not writing\n");
		return -1;
	}
	if (len == 0 || len > 64) {
		return -1;
	}
	words = (len + 3) / 4;
	kt_se_wr(SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	kt_se_wr(SE_I2C_TX_TRANS_LEN, len);
	kt_se_wr(SE_GENI_M_CMD0, (I2C_WRITE << M_OPCODE_SHFT) | kt_i2c_param());
	/*
	 * Arm one word of TX watermark.  Qualcomm's FIFO path writes 1 here
	 * right after the command word (i2c-qcom-geni.c, "Get FIFO IRQ");
	 * without it the sequencer never raises the watermark interrupt and
	 * this driver waits for a status bit that cannot arrive.
	 */
	kt_se_wr(SE_GENI_TX_WATERMARK_REG, 1);

	/* The sequencer asks for data by raising the TX watermark. */
	for (i = 0; i < 100; i++) {
		status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
		if ((status & M_ERRORS) != 0) {
			return kt_i2c_error(status);
		}
		if ((status & (M_TX_FIFO_WATERMARK | M_CMD_DONE)) != 0) {
			break;
		}
		delay(100);
	}
	if (i == 100) {
		printf("KEBAB TOUCH: write watermark timeout, status 0x%08x\n",
		    status);
		(void)kt_se_recover("write watermark timeout");
		return -1;
	}

	for (i = 0; i < words; i++) {
		uint32_t w = 0;
		uint32_t b;

		for (b = 0; b < 4 && i * 4 + b < len; b++) {
			w |= (uint32_t)buf[i * 4 + b] << (8 * b);
		}
		kt_se_wr(SE_GENI_TX_FIFO, w);
	}
	kt_se_wr(SE_GENI_TX_WATERMARK_REG, 0);
	kt_se_wr(SE_GENI_M_IRQ_CLEAR, status);

	for (i = 0; i < 200; i++) {
		status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
		if ((status & M_ERRORS) != 0) {
			return kt_i2c_error(status);
		}
		if ((status & M_CMD_DONE) != 0) {
			kt_se_wr(SE_GENI_M_IRQ_CLEAR, status);
			return 0;
		}
		delay(100);
	}
	printf("KEBAB TOUCH: write did not complete, status 0x%08x\n", status);
	(void)kt_se_recover("write completion timeout");
	return -1;
}

static int
kt_i2c_read(uint8_t *buf, uint32_t len)
{
	uint32_t got = 0;
	uint32_t i;
	uint32_t status = 0;

	if (kt.bb) {
		return kt_bb_read(buf, len);
	}
	if (kt.se_dead) {
		printf("KEBAB TOUCH: SE is marked unusable after a failed "
		    "recovery; not reading\n");
		return -1;
	}
	if (len == 0 || len > 128) {
		return -1;
	}
	kt_se_wr(SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	kt_se_wr(SE_I2C_RX_TRANS_LEN, len);
	kt_se_wr(SE_GENI_M_CMD0, (I2C_READ << M_OPCODE_SHFT) | kt_i2c_param());

	for (i = 0; i < 300; i++) {
		uint32_t count;

		status = kt_se_rd(SE_GENI_M_IRQ_STATUS);
		if ((status & M_ERRORS) != 0) {
			return kt_i2c_error(status);
		}
		count = kt_se_rd(SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
		while (count-- > 0 && got < len) {
			uint32_t w = kt_se_rd(SE_GENI_RX_FIFO);
			uint32_t b;

			for (b = 0; b < 4 && got < len; b++) {
				buf[got++] = (uint8_t)((w >> (8 * b)) & 0xffu);
			}
		}
		if (status != 0) {
			kt_se_wr(SE_GENI_M_IRQ_CLEAR, status);
		}
		if ((status & M_CMD_DONE) != 0) {
			break;
		}
		delay(100);
	}
	if (i == 300) {
		printf("KEBAB TOUCH: read did not complete, status 0x%08x got %u "
		    "of %u\n", status, got, len);
		(void)kt_se_recover("read completion timeout");
		return -1;
	}
	if (got != len) {
		printf("KEBAB TOUCH: short read, %u of %u bytes\n", got, len);
		return -1;
	}
	return 0;
}

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

	err = kt_i2c_read(win, KEBAB_TCM_HEADER_SIZE);
	if (err != 0) {
		return -1;
	}
	for (slid = 0; slid < 4; slid++) {
		kebab_tcm_status st = kebab_tcm_header_parse(win, header);

		if (st == KEBAB_TCM_OK) {
			if (slid != 0) {
				kt_trace("KEBAB TOUCH: command 0x%02x: resynchronised after %u byte(s)\n",
				    cmd, slid);
			}
			return 0;
		}
		if (slid == 0) {
			kt_trace("KEBAB TOUCH: command 0x%02x: header %02x %02x %02x %02x rejected (%s), sliding\n",
			    cmd, win[0], win[1], win[2], win[3],
			    kebab_tcm_status_name(st));
		}
		if (kt_i2c_read(&next, 1) != 0) {
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
	kt_trace("KEBAB TOUCH: TCM command 0x%02x, %u payload byte(s)\n", cmd,
	    len);
	err = kt_i2c_write(out, written);
	if (err != 0) {
		printf("KEBAB TOUCH: command 0x%02x was not written\n", cmd);
		return -1;
	}
	kt_trace("KEBAB TOUCH: TCM command 0x%02x written, waiting for header\n",
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
				kt_trace("KEBAB TOUCH: no usable TCM header yet, attempt %u\n",
				    i + 1);
			}
			delay(1000);
			continue;
		}
		pad = header.payload_len;
		err = kt_i2c_read(scratch, header.payload_len + 1);
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
		kt_sleep_ms(10);
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

	err = kt_i2c_read(hdr, KEBAB_TCM_HEADER_SIZE);
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
	err = kt_i2c_read(payload, len + 1);
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
		int n = kt_poll_once();

		if (n < 0) {
			kt.errors++;
			if (kt.errors == 1 || (kt.errors % 500u) == 0) {
				printf("KEBAB TOUCH: %u polling errors\n", kt.errors);
			}
			kt_sleep_ms(100);
		} else {
			kt_sleep_ms(5);
		}
	}
}

/*
 * Gate D: bring the SE up (the S5 clock must already be on), then send exactly
 * one address-only write, so the FIFO handshake, ACK/NACK and the timeout path
 * can be judged without TCM command bytes in flight.
 */
void
kebab_touch_se(void)
{
	if (kt.se == 0) {
		kt.se = ml_io_map(KT_SE13_PA_DEFAULT, KT_SE13_SIZE);
		kt.wrap = ml_io_map(KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE);
	}
	if (kt.tlmm == 0) {
		kt.tlmm = ml_io_map(KT_TLMM_PA + TLMM_TILE_SOUTH, KT_TLMM_SIZE);
	}
	if (kt.se == 0 || kt.tlmm == 0) {
		printf("KEBAB TOUCH: TOUCHSE cannot map SE13 or TLMM\n");
		return;
	}
	/*
	 * A dead SE is only cleared by a reboot: re-running TOUCHSE into a
	 * sequencer that failed its own cancel/abort would make the next log
	 * unattributable, which is exactly what the flag exists to prevent.
	 */
	if (kt.se_dead) {
		printf("KEBAB TOUCH: SE is marked unusable after a failed "
		    "recovery; reboot before TOUCHSE\n");
		return;
	}
	kt.se_initialized = false;
	kt_trace("KEBAB TOUCH: TOUCHSE begin: pins, reset, se_init\n");
	kt_gcc_probe();
	if (kt.gcc_en != 0 &&
	    (kt_rd(kt.gcc_en, KT_GCC_ENABLE - KT_GCC_PAGE_EN) & KT_GCC_EN_S5) == 0) {
		printf("KEBAB TOUCH: S5 clock is off; run TOUCHCLK first\n");
		return;
	}
	kt_pins_config();
	kt_reset_pulse(1);
	if (!kt_se_init(100000)) {
		return;
	}
	kt.se_initialized = true;
	kt_trace("KEBAB TOUCH: TOUCHSE done; TOUCHPING can be sent\n");
}

void
kebab_touch_ping(void)
{
	uint8_t zero = 0;
	int rc;

	if (!kt.se_initialized) {
		printf("KEBAB TOUCH: TOUCHPING needs a successful TOUCHSE first\n");
		return;
	}
	if (kt.se_dead) {
		printf("KEBAB TOUCH: SE is marked unusable; reboot to clear it\n");
		return;
	}
	if ((kt_se_rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) != 0) {
		printf("KEBAB TOUCH: a command is still active; recovering first\n");
		if (!kt_se_recover("ping precondition")) {
			return;
		}
	}
	/*
	 * One data byte.  An address-only write (TX_TRANS_LEN = 0) leaves the
	 * command active on this SE with no interrupt at all, which is why the
	 * first attempt never produced an ACK, a NACK or a completion; a write
	 * with one byte goes through the FIFO path this driver just fixed.
	 */
	kt_trace("KEBAB TOUCH: ping 0x%02x: one byte, FIFO path\n", KT_I2C_ADDR);
	rc = kt_i2c_write(&zero, 1);
	kt_trace("KEBAB TOUCH: ping 0x%02x: rc %d GENI_STATUS 0x%08x\n",
	    KT_I2C_ADDR, rc, kt_se_rd(SE_GENI_STATUS));
	if (rc == 0) {
		printf("KEBAB TOUCH: ping: ACK, one-byte write completed\n");
	}
}

#define KT_GCC_RCG_S5       0x00018600u
#define KT_GCC_RCG_WORDS    8u

void
kebab_touch_clock_enable(void)
{
	uint32_t en, i;

	if (kt.gcc_en == 0) {
		kt.gcc_en = ml_io_map(KT_GCC_PA + KT_GCC_PAGE_EN, 0x1000);
		kt.gcc_halt = ml_io_map(KT_GCC_PA + KT_GCC_PAGE_HALT, 0x1000);
	}
	if (kt.gcc_en == 0 || kt.gcc_halt == 0) {
		printf("KEBAB TOUCH: cannot map the GCC pages\n");
		return;
	}
	kt_trace("KEBAB TOUCH: TOUCHCLK: S5 RCG at 0x%05x\n", KT_GCC_RCG_S5);
	for (i = 0; i < KT_GCC_RCG_WORDS; i++) {
		kt_trace("KEBAB TOUCH:   RCG+0x%02x = 0x%08x\n", i * 4,
		    kt_rd(kt.gcc_halt, KT_GCC_RCG_S5 + i * 4 - KT_GCC_PAGE_HALT));
	}
	en = kt_rd(kt.gcc_en, KT_GCC_ENABLE - KT_GCC_PAGE_EN);
	kt_trace("KEBAB TOUCH: TOUCHCLK: 0x%05x 0x%08x -> 0x%08x\n",
	    KT_GCC_ENABLE, en, en | KT_GCC_EN_S5);
	kt_wr(kt.gcc_en, KT_GCC_ENABLE - KT_GCC_PAGE_EN, en | KT_GCC_EN_S5);
	kt_sleep_ms(2);
	en = kt_rd(kt.gcc_en, KT_GCC_ENABLE - KT_GCC_PAGE_EN);
	kt_trace("KEBAB TOUCH: TOUCHCLK: 0x%05x now 0x%08x (s5 %u), halt 0x%08x\n",
	    KT_GCC_ENABLE, en, (en & KT_GCC_EN_S5) != 0,
	    kt_rd(kt.gcc_halt, KT_GCC_HALT_WRAP1_S5 - KT_GCC_PAGE_HALT));
}

/* ------------------------------------------------------------------ start */

int
kebab_touch_start(void)
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
	kt_trace("KEBAB TOUCH: bring-up start: bb=%u pins=%u reset=%u hz=%u; pausing 3 s for the USB console\n",
	    (unsigned)kt.bb, pins, reset, hz);
	kt_sleep_ms(3000);
	kt_trace("KEBAB TOUCH: console window over, starting MMIO\n");
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
		kt_sleep_ms(KT_RAIL_SETTLE_MS);
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

	if (!kt.bb) {
		kt_trace("KEBAB TOUCH: mapping SE13 0x%08x+0x%x, wrapper 0x%08x+0x%x\n",
		    se_pa, KT_SE13_SIZE, KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE);
		kt.se = ml_io_map(se_pa, KT_SE13_SIZE);
		if (kt.se == 0) {
			printf("KEBAB TOUCH: cannot map SE13 at 0x%08x\n", se_pa);
			return -1;
		}
		kt.wrap = ml_io_map(KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE);
	} else {
		/*
		 * The bit-bang transport never reads an SE or wrapper register:
		 * the point of the alternate path is to remove that dependency.
		 * Only TLMM is mapped.
		 */
		kt_trace("KEBAB TOUCH: transport bit-banged GPIO I2C; QUPv3 SE13 is not used (SDA %u SCL %u reset %u IRQ %u)\n",
		    KT_PIN_SDA, KT_PIN_SCL, KT_PIN_RESET, KT_PIN_IRQ);
	}
	kt_trace("KEBAB TOUCH: mapping TLMM SOUTH at 0x%08x+0x%x\n",
	    KT_TLMM_PA + TLMM_TILE_SOUTH, KT_TLMM_SIZE);
	if (kt.bb || pins != 0 || reset != 0 || kt.raw) {
		kt.tlmm = ml_io_map(KT_TLMM_PA + TLMM_TILE_SOUTH,
		    KT_TLMM_SIZE);
	}
	kt_trace("KEBAB TOUCH: MMIO done: se 0x%llx wrap 0x%llx tlmm 0x%llx\n",
	    (unsigned long long)kt.se, (unsigned long long)kt.wrap,
	    (unsigned long long)kt.tlmm);
	if (probe != 0) {
		kebab_touch_probe();
		return 0;
	}
	if (boot == 0) {
		printf("KEBAB TOUCH: MMIO mapped, hardware untouched; "
		    "TOUCH? is read-only, TOUCHINIT starts the driver\n");
		return 0;
	}
	if (kt.bb && kt.tlmm == 0) {
		printf("KEBAB TOUCH: bit-bang transport needs TLMM and it is not mapped\n");
		return -1;
	}

	if (!kt.bb) {
		/*
		 * The SE13 branch clock is off at boot, and the first GENI
		 * register group written without it stops the console: 0116
		 * measured that, and this bring-up hit the same wall at
		 * "se_init: force default, output ctrl".  TOUCHCLK followed by
		 * TOUCHSE has been working from the console, so the boot path
		 * runs the same step in the same place instead of being a
		 * variant that is known to hang.  A GCC that cannot be mapped is
		 * not fatal here: the bootloader may have left the clock on, and
		 * the reads below are harmless either way.
		 */
		kebab_touch_clock_enable();
		kt_trace("KEBAB TOUCH: reading wrapper/SE revision registers\n");
		printf("KEBAB TOUCH: S3908 on QUPv3 SE13 at 0x%08x (I2C 0x%02x, %u Hz)"
		    ", wrapper 0x%08x\n", se_pa, KT_I2C_ADDR, hz,
		    KT_WRAP_PA_DEFAULT);
		printf("KEBAB TOUCH: wrapper QUP_HW_VER 0x%08x, SE13 GENI_FW_REVISION_RO "
		    "0x%08x\n", kt.wrap != 0 ? kt_rd(kt.wrap, QUP_HW_VER) : 0u,
		    kt_se_rd(GENI_FW_REVISION_RO));
	}

	kt_trace("KEBAB TOUCH: configuring pins (%s)\n",
	    kt.bb ? "bit-bang GPIO" : "qup13");
	if (kt.tlmm != 0 && kt.bb) {
		kt_bb_pins_config();
	} else if (kt.tlmm != 0 && pins != 0) {
		kt_pins_config();
	}
	kt_trace("KEBAB TOUCH: pins done, reset pulse next\n");
	if (kt.tlmm != 0 && reset != 0) {
		kt_reset_pulse(reset);
	}
	/*
	 * The pulse already waited 200 ms after the release edge; this is the
	 * explicit delay the first transfer is gated on.  With the rails up it
	 * makes the order rail settle -> reset -> boot time, which is the order
	 * the chip is specified to see.
	 */
	kt_sleep_ms(KT_RESET_SETTLE_MS);
	kt_trace("KEBAB TOUCH: reset pulse done, waited %u ms\n",
	    KT_RESET_SETTLE_MS);
	if (kt.raw) {
		kt_dump_regs("before init");
	}

	if (!kt.bb && !kt_se_init(hz)) {
		if (kt.raw) {
			kt_dump_regs("after failed init");
		}
		return -1;
	}
	if (kt.raw) {
		kt_dump_regs("after init");
	}

	/*
	 * Identity first.  Identify is answered from either mode, so this is
	 * also the test for whether the touch application is running at all;
	 * the bytes are printed before the decode so a mismatch between this
	 * driver's reading and the chip's answer is visible in the log.
	 */
	kt_trace("KEBAB TOUCH: TCM identify begin\n");
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

#endif /* KEBAB */
