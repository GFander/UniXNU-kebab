/* RPMh command DB/RSC controller adapter and board rail policy.
 * TCS is an RSC transport, not an SPMI transaction. The tested encoder/state
 * machine stays in kebab_rpmh_vrm.c; PMIC readback goes through its SPMI client.
 */
#if defined(KEBAB)
#define KEBAB_RPMH_KERNEL 1
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include <pexpert/pexpert.h>
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
#include "kebab_pmic_regs.h"
#include "kebab_rpmh_vrm.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void kebab_usb_log_flush(void);
/* ------------------------------------------- RPMh discovery (read only) */

/*
 * The rails the S3908 needs are RPMh VRM resources.  Before any request can
 * be formed, two things have to be read, never written:
 *
 *   - the command DB, a 128 KiB reserved region at 0x80860000 whose entries
 *     map a resource name to the address a VRM command must carry;
 *   - the APPS RSC's drv-2 window at 0x18220000, where the active TCS slots
 *     live at tcs-offset 0xd00 with a 672-byte stride.
 *
 * Layouts are from the vendor cmd-db.c and rpmh-rsc.c.  Every offset is
 * bounds-checked against the mapped window before it is read, and nothing
 * below writes a register.
 */
#define KEBAB_CMD_DB_PA         0x80860000u
#define KEBAB_CMD_DB_SIZE       0x00020000u
#define KEBAB_CMD_DB_HEADER     144u    /* 4 + 4 + 8*16 + 4 + 4 */
#define KEBAB_CMD_DB_SLAVES     8u
#define KEBAB_CMD_DB_ENTRY      24u

#define KEBAB_RSC_DRV2_PA       0x18220000u
#define KEBAB_RSC_SIZE          0x00010000u
#define KEBAB_RSC_TCS_OFF       0x00000d00u
#define KEBAB_RSC_TCS_STRIDE    672u
#define KEBAB_RSC_CMD_STRIDE    20u
#define KEBAB_RSC_ACTIVE_TCS    2u
#define KEBAB_RSC_TCS_TOTAL     8u      /* prnt_chld says 8; the control TCS is not in this array */
#define KEBAB_RSC_CMD_WAIT      0x10u
#define KEBAB_RSC_RESP_DATA     0x40u
#define KEBAB_RSC_IRQ_CLEAR     0x08u
#define KEBAB_RSC_PRNT_CHLD     0x0cu
#define KEBAB_RSC_TCS_MASK      0x3fu
#define KEBAB_RSC_TCS_SHIFT     6u
#define KEBAB_RSC_NCPT_MASK     0x1fu
#define KEBAB_RSC_NCPT_SHIFT    27u
#define KEBAB_RSC_NCPT_MAX      16u
#define KEBAB_RSC_AMC_ENABLE    (1u << 16)
#define KEBAB_RSC_AMC_TRIGGER   (1u << 24)
#define KEBAB_RSC_CMD_ISSUED    (1u << 8)
#define KEBAB_RSC_CMD_COMPL     (1u << 16)

#define KEBAB_RSC_IRQ_ENABLE    0x00u
#define KEBAB_RSC_IRQ_STATUS    0x04u
#define KEBAB_RSC_CONTROL       0x14u
#define KEBAB_RSC_STATUS        0x18u
#define KEBAB_RSC_CMD_ENABLE    0x1cu
#define KEBAB_RSC_CMD_MSGID     0x30u
#define KEBAB_RSC_CMD_ADDR      0x34u
#define KEBAB_RSC_CMD_DATA      0x38u
#define KEBAB_RSC_CMD_STATUS    0x3cu


#define KEBAB_VRM_EN_ON 0x80u
static struct {
	bool        parsed;
	bool        gate;
	vm_offset_t drv;               /* drv-2 window; zero until mapped */
	vm_offset_t tcs;               /* TCS1 window inside it */
	kebab_rpmh_tcs channel;
	bool        ldoa13_done;
	bool        ldoa13_ok;
	bool        ldoc1_done;
	bool        ldoc1_ok;
} vrm;

static uint32_t
kebab_rpmh_read32(vm_offset_t base, uint32_t offset)
{
	uint32_t value = *(volatile uint32_t *)(base + offset);

	/* Device memory: finish the read before its result is used. */
	__asm__ volatile ("dmb sy" ::: "memory");
	return value;
}

static void
kebab_rpmh_write32(vm_offset_t base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(base + offset) = value;
	/* The command must reach the arbiter before the status is polled. */
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
kebab_rpmh_cmd_db(void)
{
	vm_offset_t db;
	uint32_t version, magic, i, j;

	db = ml_io_map(KEBAB_CMD_DB_PA, KEBAB_CMD_DB_SIZE);
	if (db == 0) {
		printf("KEBAB RPMH: cannot map the command DB at 0x%08x\n",
		    KEBAB_CMD_DB_PA);
		return;
	}
	version = kebab_rpmh_read32(db, 0);
	magic = kebab_rpmh_read32(db, 4);
	printf("KEBAB RPMH: cmd-db 0x%08x+0x%x: version 0x%08x magic 0x%08x (%s)\n",
	    KEBAB_CMD_DB_PA, KEBAB_CMD_DB_SIZE, version, magic,
	    magic == 0x0c0330dbu ? "match" : "MISMATCH");
	if (magic != 0x0c0330dbu) {
		return;
	}
	for (i = 0; i < KEBAB_CMD_DB_SLAVES; i++) {
		uint32_t off = 8u + i * 16u;
		uint32_t w0 = kebab_rpmh_read32(db, off);
		uint32_t w1 = kebab_rpmh_read32(db, off + 4u);
		uint32_t slv = w0 & 0xffffu;
		uint32_t hdr_off = (w0 >> 16) & 0xffffu;
		uint32_t cnt = (w1 >> 16) & 0xffffu;

		if (cnt == 0) {
			continue;
		}
		printf("KEBAB RPMH: slave %u: %u entries at +0x%x\n",
		    slv, cnt, hdr_off);
		/*
		 * Only bound the walk so a corrupt count cannot spin: the
		 * per-entry check below rejects anything past the window.  67
		 * entries is a normal VREG slave, so 64 was too tight.
		 */
		if (cnt > 1024u) {
			printf("KEBAB RPMH: slave %u: count %u is implausible; skipped\n",
			    slv, cnt);
			continue;
		}
		for (j = 0; j < cnt; j++) {
			uint32_t e = KEBAB_CMD_DB_HEADER + hdr_off +
			    j * KEBAB_CMD_DB_ENTRY;
			uint32_t w_id0, w_id1, addr, tail;
			char id[9];

			if (e + KEBAB_CMD_DB_ENTRY > KEBAB_CMD_DB_SIZE) {
				printf("KEBAB RPMH: slave %u: entry %u outside the window; stopped\n",
				    slv, j);
				break;
			}
			w_id0 = kebab_rpmh_read32(db, e);
			w_id1 = kebab_rpmh_read32(db, e + 4u);
			addr = kebab_rpmh_read32(db, e + 16u);
			tail = kebab_rpmh_read32(db, e + 20u);
			memcpy(id, &w_id0, 4);
			memcpy(id + 4, &w_id1, 4);
			id[8] = 0;
			if (strcmp(id, "ldoa13") != 0 && strcmp(id, "ldoc1") != 0) {
				continue;
			}
			printf("KEBAB RPMH: %s: addr 0x%08x len %u offset %u\n",
			    id, addr, tail & 0xffffu, (tail >> 16) & 0xffffu);
		}
	}
}

static void
kebab_rpmh_rsc(void)
{
	vm_offset_t drv, base, t;
	uint32_t i, c, prnt, max_tcs, ncpt, control;

	drv = ml_io_map(KEBAB_RSC_DRV2_PA, KEBAB_RSC_SIZE);
	if (drv == 0) {
		printf("KEBAB RPMH: cannot map the drv-2 window at 0x%08x\n",
		    KEBAB_RSC_DRV2_PA);
		return;
	}
	base = drv + KEBAB_RSC_TCS_OFF;
	printf("KEBAB RPMH: drv-2 0x%08x, tcs_base +0x%x -> 0x%llx\n",
	    KEBAB_RSC_DRV2_PA, KEBAB_RSC_TCS_OFF, (unsigned long long)base);
	/*
	 * IRQ_ENABLE/STATUS/CLEAR are shared but live at tcs_base + 0x0/0x4/0x8
	 * (rpmh-rsc.c reads them through read_tcs_reg with tcs_id 0); only
	 * DRV_PRNT_CHLD_CONFIG sits at drv->base.
	 */
	printf("KEBAB RPMH: irq_en 0x%08x irq_status 0x%08x irq_clear 0x%08x prnt_chld 0x%08x\n",
	    kebab_rpmh_read32(base, KEBAB_RSC_IRQ_ENABLE),
	    kebab_rpmh_read32(base, KEBAB_RSC_IRQ_STATUS),
	    kebab_rpmh_read32(base, KEBAB_RSC_IRQ_CLEAR),
	    kebab_rpmh_read32(drv, KEBAB_RSC_PRNT_CHLD));
	prnt = kebab_rpmh_read32(drv, KEBAB_RSC_PRNT_CHLD);
	max_tcs = (prnt >> (KEBAB_RSC_TCS_SHIFT * 2u)) & KEBAB_RSC_TCS_MASK;
	ncpt = (prnt >> KEBAB_RSC_NCPT_SHIFT) & KEBAB_RSC_NCPT_MASK;
	printf("KEBAB RPMH: prnt_chld decode: drv-2 tcs %u, ncpt %u\n",
	    max_tcs, ncpt);
	if (max_tcs == 0 || max_tcs > KEBAB_RSC_TCS_TOTAL) {
		max_tcs = KEBAB_RSC_TCS_TOTAL;
	}
	for (i = 0; i < max_tcs; i++) {
		t = base + i * KEBAB_RSC_TCS_STRIDE;
		control = kebab_rpmh_read32(t, KEBAB_RSC_CONTROL);
		printf("KEBAB RPMH: tcs %u: wait 0x%08x control 0x%08x (amc_en %u amc_trig %u) status 0x%08x enable 0x%08x\n",
		    i, kebab_rpmh_read32(t, KEBAB_RSC_CMD_WAIT), control,
		    (control & KEBAB_RSC_AMC_ENABLE) != 0,
		    (control & KEBAB_RSC_AMC_TRIGGER) != 0,
		    kebab_rpmh_read32(t, KEBAB_RSC_STATUS),
		    kebab_rpmh_read32(t, KEBAB_RSC_CMD_ENABLE));
		/*
		 * NCPT says how many command slots the hardware has; walk all
		 * of them, not just the two this kernel happens to poke.
		 */
		for (c = 0; c < KEBAB_RSC_NCPT_MAX; c++) {
			vm_offset_t slot = t + c * KEBAB_RSC_CMD_STRIDE;
			uint32_t msgid = kebab_rpmh_read32(slot, KEBAB_RSC_CMD_MSGID);
			uint32_t addr = kebab_rpmh_read32(slot, KEBAB_RSC_CMD_ADDR);
			uint32_t data = kebab_rpmh_read32(slot, KEBAB_RSC_CMD_DATA);
			uint32_t status = kebab_rpmh_read32(slot, KEBAB_RSC_CMD_STATUS);
			uint32_t resp = kebab_rpmh_read32(slot, KEBAB_RSC_RESP_DATA);

			if (msgid == 0 && addr == 0 && data == 0 &&
			    status == 0 && resp == 0) {
				continue;
			}
			printf("KEBAB RPMH: tcs %u slot %u: msgid 0x%08x addr 0x%08x data 0x%08x status 0x%08x resp 0x%08x\n",
			    i, c, msgid, addr, data, status, resp);
		}
	}
}

static const char *
kebab_vrm_status_name(kebab_rpmh_status status)
{
	switch (status) {
	case KEBAB_RPMH_OK:                 return "OK";
	case KEBAB_RPMH_ERR_ARGUMENT:       return "ERR_ARGUMENT";
	case KEBAB_RPMH_ERR_RESOURCE:       return "ERR_RESOURCE";
	case KEBAB_RPMH_ERR_ADDRESS:        return "ERR_ADDRESS";
	case KEBAB_RPMH_ERR_VOLTAGE:        return "ERR_VOLTAGE";
	case KEBAB_RPMH_ERR_COMMAND_DB:     return "ERR_COMMAND_DB";
	case KEBAB_RPMH_ERR_DUPLICATE:      return "ERR_DUPLICATE";
	case KEBAB_RPMH_ERR_TCS_BUSY:       return "ERR_TCS_BUSY";
	case KEBAB_RPMH_ERR_TCS_STATE:      return "ERR_TCS_STATE";
	case KEBAB_RPMH_ERR_NO_SINK:        return "ERR_NO_SINK";
	case KEBAB_RPMH_ERR_SINK_FAILED:    return "ERR_SINK_FAILED";
	case KEBAB_RPMH_ERR_NOT_ALLOWED:    return "ERR_NOT_ALLOWED";
	case KEBAB_RPMH_PENDING:            return "PENDING";
	case KEBAB_RPMH_ERR_TIMEOUT:        return "ERR_TIMEOUT";
	case KEBAB_RPMH_ERR_RECLAIM:        return "ERR_RECLAIM";
	}
	return "UNKNOWN";
}

static const char *
kebab_vrm_phase_name(kebab_tcs_phase phase)
{
	switch (phase) {
	case KEBAB_TCS_PHASE_IDLE:                 return "IDLE";
	case KEBAB_TCS_PHASE_VALIDATED:            return "VALIDATED";
	case KEBAB_TCS_PHASE_ARMED:                return "ARMED";
	case KEBAB_TCS_PHASE_TRIGGERED:            return "TRIGGERED";
	case KEBAB_TCS_PHASE_COMPLETE:             return "COMPLETE";
	case KEBAB_TCS_PHASE_FAILED_PRE_TRIGGER:   return "FAILED_PRE_TRIGGER";
	case KEBAB_TCS_PHASE_FAILED_POST_TRIGGER:  return "FAILED_POST_TRIGGER";
	case KEBAB_TCS_PHASE_COMPLETE_UNCLEAN:     return "COMPLETE_UNCLEAN";
	}
	return "UNKNOWN";
}

/*
 * The io the state machine talks to.  ctx is the TCS1 window; the offsets are
 * the module's KEBAB_TCS_* constants, which are the same numbers the TCS uses
 * (0x10 CMD_WAIT, 0x14 CONTROL, 0x18 STATUS, 0x1c CMD_ENABLE, 0x30..0x3c the
 * slot registers, 0x40 RESP_DATA).  Bounding every access to one TCS window
 * keeps a bad offset from becoming a read of the neighbouring TCS.
 */
static int
kebab_vrm_rd(void *ctx, uint32_t off, uint32_t *val)
{
	vm_offset_t t = (vm_offset_t)ctx;

	if (t == 0 || val == NULL || off + 4u > KEBAB_RSC_TCS_STRIDE) {
		return -1;
	}
	*val = kebab_rpmh_read32(t, off);
	return 0;
}

static int
kebab_vrm_wr(void *ctx, uint32_t off, uint32_t val)
{
	vm_offset_t t = (vm_offset_t)ctx;

	if (t == 0 || off + 4u > KEBAB_RSC_TCS_STRIDE) {
		return -1;
	}
	kebab_rpmh_write32(t, off, val);
	return 0;
}

/*
 * The state machine does not sleep: it has a poll cap and, when a clock is
 * present, a wall budget as well.  Feeding it the monotonic clock means a
 * stalled or jumping one can only extend a wait to the cap, never past it.
 */
static uint64_t
kebab_vrm_now_us(void *ctx)
{
	uint64_t ns = 0;

	(void)ctx;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000u;
}

/*
 * One gate for both rails, parsed once.  Default off: an unattended boot never
 * powers a rail, and VROMON printed from the panic loop only reads a boolean.
 */
bool
kebab_spmi_vrm_writes_enabled(void)
{
	uint32_t enabled = 0;

	if (!vrm.parsed) {
		vrm.parsed = true;
		vrm.gate = PE_parse_boot_argn("kebab_vrm_write", &enabled,
		    sizeof(enabled)) && enabled != 0;
		printf("KEBAB VRM: kebab_vrm_write=%u: RPMh rail writes %s\n",
		    enabled, vrm.gate ? "ENABLED" : "disabled (default)");
	}
	return vrm.gate;
}

/*
 * Map the drv-2 window and compute TCS1's address in it.  Called from
 * kebab_spmi_init() when the gate is on, and from TCS? which has to be able to
 * look whether or not a write was ever allowed.  Mapped once: a second call is
 * a no-op, so a command can never remap over a live window.
 */
static bool
kebab_vrm_map(void)
{
	if (vrm.tcs != 0) {
		return true;
	}
	if (vrm.drv == 0) {
		vrm.drv = ml_io_map(KEBAB_RSC_DRV2_PA, KEBAB_RSC_SIZE);
		if (vrm.drv == 0) {
			printf("KEBAB VRM: cannot map the drv-2 window at 0x%08x\n",
			    KEBAB_RSC_DRV2_PA);
			return false;
		}
	}
	vrm.tcs = vrm.drv + KEBAB_RSC_TCS_OFF + 1u * KEBAB_RSC_TCS_STRIDE;
	printf("KEBAB VRM: drv-2 0x%08x+0x%x -> tcs_base 0x%llx; TCS1 = tcs_base + 0x%x = 0x%llx (stride %u)\n",
	    KEBAB_RSC_DRV2_PA, KEBAB_RSC_SIZE,
	    (unsigned long long)(vrm.drv + KEBAB_RSC_TCS_OFF),
	    1u * KEBAB_RSC_TCS_STRIDE, (unsigned long long)vrm.tcs,
	    KEBAB_RSC_TCS_STRIDE);
	return true;
}

/* Map the TCS1 window now, in normal context, so a command only reads a bool. */
void
kebab_spmi_vrm_init(void)
{
	if (vrm.tcs != 0 || !kebab_spmi_vrm_writes_enabled()) {
		return;
	}
	if (!kebab_vrm_map()) {
		printf("KEBAB VRM: VROMON will refuse: no window\n");
		return;
	}
	printf("KEBAB VRM: write path ready, gated by kebab_vrm_write\n");
}

/*
 * TCS?: the read-only state of the channel a send would use.  Two questions,
 * both answered from registers only: is the channel idle, and is it clean (no
 * completion left over from somebody else's command)?  A wedged or dirty
 * channel is reported as such and never touched: the TCS may carry another
 * execution environment's vote, so clearing it here could revoke a resource
 * this kernel does not own.
 */
void
kebab_tcs_report(void)
{
	vm_offset_t base, t, irq_en, irq_status, irq_clear;
	uint32_t i, control, enable, wait, status;
	bool wedged = false, dirty = false;

	printf("KEBAB TCS: === read-only TCS dump, APPS RSC drv-2 ===\n");
	if (!kebab_vrm_map()) {
		printf("KEBAB TCS: FAIL: the drv-2 window is not mapped\n");
		return;
	}
	base = vrm.drv + KEBAB_RSC_TCS_OFF;
	irq_en = kebab_rpmh_read32(base, KEBAB_RSC_IRQ_ENABLE);
	irq_status = kebab_rpmh_read32(base, KEBAB_RSC_IRQ_STATUS);
	irq_clear = kebab_rpmh_read32(base, KEBAB_RSC_IRQ_CLEAR);
	printf("KEBAB TCS: drv-2 0x%08x, tcs_base = drv + 0x%x = 0x%llx, stride %u\n",
	    KEBAB_RSC_DRV2_PA, KEBAB_RSC_TCS_OFF, (unsigned long long)base,
	    KEBAB_RSC_TCS_STRIDE);
	printf("KEBAB TCS: rsc irq: enable[tcs_base+0x0] 0x%08x status[+0x4] 0x%08x clear[+0x8] 0x%08x\n",
	    irq_en, irq_status, irq_clear);
	for (i = 0; i < KEBAB_RSC_ACTIVE_TCS; i++) {
		t = base + i * KEBAB_RSC_TCS_STRIDE;
		control = kebab_rpmh_read32(t, KEBAB_RSC_CONTROL);
		enable = kebab_rpmh_read32(t, KEBAB_RSC_CMD_ENABLE);
		wait = kebab_rpmh_read32(t, KEBAB_RSC_CMD_WAIT);
		status = kebab_rpmh_read32(t, KEBAB_RSC_CMD_STATUS);
		printf("KEBAB TCS: tcs %u at 0x%llx: CONTROL[+0x%x] 0x%08x (AMC_ENABLE %u, AMC_TRIGGER %u) CMD_ENABLE[+0x%x] 0x%08x CMD_WAIT[+0x%x] 0x%08x CMD_STATUS[+0x%x] 0x%08x (ISSUED %u, COMPL %u)\n",
		    i, (unsigned long long)t, KEBAB_RSC_CONTROL, control,
		    (control & KEBAB_RSC_AMC_ENABLE) != 0,
		    (control & KEBAB_RSC_AMC_TRIGGER) != 0,
		    KEBAB_RSC_CMD_ENABLE, enable, KEBAB_RSC_CMD_WAIT, wait,
		    KEBAB_RSC_CMD_STATUS, status,
		    (status & KEBAB_RSC_CMD_ISSUED) != 0,
		    (status & KEBAB_RSC_CMD_COMPL) != 0);
		if ((status & KEBAB_RSC_CMD_ISSUED) != 0 &&
		    (status & KEBAB_RSC_CMD_COMPL) == 0) {
			wedged = true;
		}
		if (i == 1) {
			/* The channel VROMON would use, judged on its own. */
			if (enable != 0 || wait != 0 ||
			    (control & KEBAB_RSC_AMC_TRIGGER) != 0 ||
			    (status & KEBAB_RSC_CMD_ISSUED) != 0) {
				wedged = true;
			}
			if ((status & KEBAB_RSC_CMD_COMPL) != 0) {
				dirty = true;
			}
		}
	}
	if (wedged) {
		printf("KEBAB TCS: VERDICT: TCS1 is NOT idle (a command is in flight or wedged); a kernel-side send must refuse\n");
	} else if (dirty) {
		printf("KEBAB TCS: VERDICT: TCS1 holds a leftover COMPL; the state machine refuses it instead of reading it as ours\n");
	} else {
		printf("KEBAB TCS: VERDICT: TCS1 is idle and clean; a kernel-side send is plausible\n");
	}
	printf("KEBAB TCS: done; no register was written, cleared or recovered\n");
}

/*
 * Enable one rail.  This is the whole VRM sequence, and it is deliberately
 * one rail per call: the console enables ldoa13, then verifies with PWR?, and
 * only then enables ldoc1.  Nothing here retries, nothing here picks a
 * different address on failure, and a rail that failed is never attempted
 * again in the same boot.
 */
int
kebab_spmi_vrm_enable(const char *rail)
{
	kebab_rpmh_cmd cmd;
	kebab_rpmh_status encoded, sent;
	kebab_tcs_io io;
	uint32_t bit;
	bool *done, *ok;

	if (rail != NULL && strcmp(rail, "ldoa13") == 0) {
		done = &vrm.ldoa13_done;
		ok = &vrm.ldoa13_ok;
	} else if (rail != NULL && strcmp(rail, "ldoc1") == 0) {
		done = &vrm.ldoc1_done;
		ok = &vrm.ldoc1_ok;
	} else {
		printf("KEBAB VRM: %s is not a rail this command may power; the whitelist is ldoa13 (VROMON) and ldoc1 (VROMON ldoc1), nothing sent\n",
		    rail != NULL ? rail : "(no argument)");
		return -1;
	}
	if (!kebab_spmi_vrm_writes_enabled()) {
		printf("KEBAB VRM: kebab_vrm_write is off; refusing to send %s\n",
		    rail);
		return -1;
	}
	if (*done) {
		printf("KEBAB VRM: %s was already attempted this boot (%s); not retried\n",
		    rail, *ok ? "it completed" : "it failed");
		return -1;
	}
	if (strcmp(rail, "ldoc1") == 0 && !vrm.ldoa13_ok) {
		printf("KEBAB VRM: ldoc1 is the second rail: send VROMON, confirm ldoa13 with PWR?, then VROMON ldoc1\n");
		return -1;
	}
	if (!kebab_vrm_map()) {
		printf("KEBAB VRM: no TCS1 window; nothing sent\n");
		return -1;
	}

	encoded = kebab_rpmh_encode_enable(rail, &cmd);
	printf("KEBAB VRM: encode %s ENABLE -> %s msgid 0x%08x addr 0x%08x data 0x%08x\n",
	    rail, kebab_vrm_status_name(encoded), cmd.msgid, cmd.addr, cmd.data);
	if (encoded != KEBAB_RPMH_OK) {
		printf("KEBAB VRM: the encoder refused; nothing sent\n");
		return -1;
	}
	bit = kebab_rpmh_cmd_bit(&cmd);
	if (bit == 0 || !kebab_rpmh_cmd_allowed(&cmd)) {
		printf("KEBAB VRM: the whitelist refused the encoded command (slot 0x%x); nothing sent\n",
		    bit);
		return -1;
	}
	printf("KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of %s only, slot 0x%x\n",
	    rail, bit);

	io.rd = kebab_vrm_rd;
	io.wr = kebab_vrm_wr;
	io.now_us = kebab_vrm_now_us;
	io.ctx = (void *)vrm.tcs;
	kebab_rpmh_tcs_init_global(&vrm.channel, io);

	*done = true;
	sent = kebab_rpmh_tcs_send(&vrm.channel, &cmd);
	*ok = sent == KEBAB_RPMH_OK;
	printf("KEBAB VRM: send %s -> %s (%d)\n", rail,
	    kebab_vrm_status_name(sent), (int)sent);
	printf("KEBAB VRM: phase %s, CMD_STATUS 0x%08x (ISSUED %u, COMPL %u)\n",
	    kebab_vrm_phase_name(vrm.channel.phase), vrm.channel.last_status,
	    (vrm.channel.last_status & KEBAB_TCS_CMD_ISSUED) != 0,
	    (vrm.channel.last_status & KEBAB_TCS_CMD_COMPL) != 0);
	if (!*ok) {
		printf("KEBAB VRM: %s did not complete; the channel is closed for this boot and the rail must not be assumed on\n",
		    rail);
		return -1;
	}
	printf("KEBAB VRM: %s ENABLE acknowledged by RPMh; confirm the rail with PWR? before the next step\n",
	    rail);
	return 0;
}

/*
 * The two-rail power sequence for a boot that asked for it with
 * kebab_touch_power=1.  It is the VROMON sequence with the console removed:
 * read the enable, encode and send one ENABLE, wait for the TCS to complete,
 * read the enable back, then the same for the second rail.  It calls
 * kebab_spmi_vrm_enable() instead of re-implementing the send, so the encoder,
 * the RPMh whitelist, the per-rail one-shot gate and the dead-channel rule are
 * exactly the ones VROMON was measured through.
 *
 * Returns 0 only when both rails end at 0x80.  Any failure returns -1 and
 * leaves the channel closed for the boot; there is no retry here, because a
 * second attempt into a state machine that refused the first one is exactly
 * what that gate exists to prevent.  The caller must not retry either.
 */
static int
kebab_spmi_vrm_boot_power_unserialized(void)
{
	static const struct {
		const char *rail;
		const char *peripheral;
	} order[2] = {
		{ "ldoa13", "pm8150_l13" },
		{ "ldoc1",  "pm8150a_l1"  },
	};
	uint32_t i;

	printf("KEBAB VRM: === boot power: touch rails, one ENABLE each ===\n");
	if (!kebab_spmi_vrm_writes_enabled()) {
		printf("KEBAB VRM: boot power: kebab_vrm_write=0, so no rail may be commanded; touch stays unpowered\n");
		return -1;
	}
	for (i = 0; i < 2; i++) {
		uint8_t before = 0;
		uint8_t after = 0;
		int rc;

		if (kebab_spmi_rail_enable_read_locked(order[i].rail,
		    order[i].peripheral, &before) != 0) {
			printf("KEBAB VRM: boot power: %s enable unreadable; nothing sent\n",
			    order[i].rail);
			return -1;
		}
		if (before == KEBAB_VRM_EN_ON) {
			printf("KEBAB VRM: boot power: %s already on (EN 0x%02x), skipped\n",
			    order[i].rail, before);
			/*
			 * ldoc1 is only allowed to follow a rail that is up.  A
			 * skipped ldoa13 is up, so record that without claiming
			 * a send: ldoa13_done stays false, so a later VROMON may
			 * still spend the one-shot on it.
			 */
			if (i == 0) {
				vrm.ldoa13_ok = true;
			}
			continue;
		}
		rc = kebab_spmi_vrm_enable(order[i].rail);
		if (kebab_spmi_rail_enable_read_locked(order[i].rail,
		    order[i].peripheral, &after) != 0) {
			printf("KEBAB VRM: boot power: %s: the read-back failed; the rail must not be assumed on\n",
			    order[i].rail);
			return -1;
		}
		printf("KEBAB VRM: boot power: %s %s, phase %s, CMD_STATUS 0x%08x (ISSUED %u, COMPL %u), EN 0x%02x -> 0x%02x\n",
		    order[i].rail, rc == 0 ? "COMPL" : "ERROR",
		    kebab_vrm_phase_name(vrm.channel.phase),
		    vrm.channel.last_status,
		    (vrm.channel.last_status & KEBAB_TCS_CMD_ISSUED) != 0,
		    (vrm.channel.last_status & KEBAB_TCS_CMD_COMPL) != 0,
		    before, after);
		if (rc != 0 || after != KEBAB_VRM_EN_ON) {
			printf("KEBAB VRM: boot power: %s is not up; stopping here, the boot continues without touch\n",
			    order[i].rail);
			return -1;
		}
	}
	printf("KEBAB VRM: boot power: ldoa13 and ldoc1 both read 0x80\n");
	return 0;
}

int
kebab_spmi_vrm_boot_power(void)
{
	kebab_spmi_bus_lock();
	int result = kebab_spmi_vrm_boot_power_unserialized();
	kebab_spmi_bus_unlock();
	return result;
}

void
kebab_rpmh_report(void)
{
	printf("KEBAB RPMH: === read-only discovery: cmd-db and drv-2 TCS ===\n");
	kebab_rpmh_cmd_db();
	kebab_rpmh_rsc();
	printf("KEBAB RPMH: done; no register was written\n");
}
#endif /* KEBAB */
