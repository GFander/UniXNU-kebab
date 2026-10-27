/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * R5-C2: the TCS1 send state machine driven against a virtual register file.
 * No MMIO, no kernel, no device.
 */
#include "KebabRpmhVrm.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, what) do { \
	checks++; \
	if (cond) { \
		printf("  PASS  %s\n", what); \
	} else { \
		failures++; \
		printf("  FAIL  %s\n", what); \
	} \
} while (0)

#define REGS 64u
#define LOGMAX 64u

typedef struct {
	uint32_t reg[REGS];
	uint32_t wr_off[LOGMAX];
	uint32_t wr_val[LOGMAX];
	uint32_t wr_count;
	int      mismatch_off;   /* write accepted, readback differs */
	int      status_reads;
	int      issued_after;   /* <0: never */
	int      compl_after;    /* <0: never */
	uint32_t status_preset;  /* value before the trigger */
	int      triggered;
	int      wr_fail_off;    /* write returns failure */
} fake;

static int fake_rd(void *ctx, uint32_t off, uint32_t *val)
{
	fake *f = ctx;

	if (off + 4 > REGS * 4) {
		return -1;
	}
	if (off == KEBAB_TCS_CMD_STATUS) {
		uint32_t st = f->status_preset;

		if (f->triggered) {
			f->status_reads++;
			if (f->issued_after >= 0 && f->status_reads >= f->issued_after) {
				st |= KEBAB_TCS_CMD_ISSUED;
			}
			if (f->compl_after >= 0 && f->status_reads >= f->compl_after) {
				st |= KEBAB_TCS_CMD_COMPL;
			}
		}
		*val = st;
		return 0;
	}
	*val = f->reg[off / 4];
	return 0;
}

static int fake_wr(void *ctx, uint32_t off, uint32_t val)
{
	fake *f = ctx;

	if (off + 4 > REGS * 4) {
		return -1;
	}
	if (f->wr_count < LOGMAX) {
		f->wr_off[f->wr_count] = off;
		f->wr_val[f->wr_count] = val;
	}
	f->wr_count++;
	if (f->wr_fail_off >= 0 && off == (uint32_t)f->wr_fail_off) {
		return -1;
	}
	f->reg[off / 4] = (f->mismatch_off >= 0 &&
	    off == (uint32_t)f->mismatch_off) ? (val ^ 0x1u) : val;
	if (off == KEBAB_TCS_CONTROL) {
		if ((val & KEBAB_TCS_AMC_TRIGGER) != 0) {
			f->triggered = 1;
		} else {
			f->triggered = 0;
		}
	}
	return 0;
}

static void fake_init(fake *f, uint32_t control)
{
	memset(f, 0, sizeof(*f));
	f->reg[KEBAB_TCS_CONTROL / 4] = control;
	f->mismatch_off = -1;
	f->wr_fail_off = -1;
	f->issued_after = 1;
	f->compl_after = 2;
}

static kebab_tcs_io fake_io(fake *f)
{
	kebab_tcs_io io = { fake_rd, fake_wr, f };

	return io;
}

int main(void)
{
	kebab_rpmh_cmd cmd;
	kebab_rpmh_tcs tcs;
	fake f;

	printf("C1 initial CONTROL=0x00010000 is accepted\n");
	kebab_rpmh_encode_enable("ldoa13", &cmd);
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK,
	    "AMC enabled, trigger clear, send succeeds");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_COMPLETE, "phase complete");

	printf("C2 normal completion and cleanup order\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK, "send ok");
	CHECK(f.wr_count == 10, "ten register writes in total");
	CHECK(f.wr_off[0] == KEBAB_TCS_CMD_MSGID && f.wr_val[0] == cmd.msgid,
	    "writes msgid first");
	CHECK(f.wr_off[1] == KEBAB_TCS_CMD_ADDR && f.wr_val[1] == cmd.addr,
	    "then addr");
	CHECK(f.wr_off[2] == KEBAB_TCS_CMD_DATA && f.wr_val[2] == cmd.data,
	    "then data");
	CHECK(f.wr_off[6] == KEBAB_TCS_CONTROL &&
	    (f.wr_val[6] & KEBAB_TCS_AMC_TRIGGER) != 0,
	    "trigger written seventh");
	CHECK(f.wr_off[7] == KEBAB_TCS_CONTROL &&
	    (f.wr_val[7] & KEBAB_TCS_AMC_TRIGGER) == 0 &&
	    (f.wr_val[7] & KEBAB_TCS_AMC_ENABLE) != 0,
	    "trigger released first on cleanup");
	CHECK(f.wr_off[8] == KEBAB_TCS_CMD_ENABLE && f.wr_val[8] == 0 &&
	    f.wr_off[9] == KEBAB_TCS_CMD_WAIT && f.wr_val[9] == 0,
	    "enable and wait cleared last");
	CHECK(f.reg[KEBAB_TCS_CMD_ENABLE / 4] == 0, "enable is zero at the end");

	printf("C3 unauthorized command never reaches the registers\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	{
		kebab_rpmh_cmd forged = { KEBAB_RPMH_MSGID_WRITE_ACK,
			0x00040c00u, 3300u };

		CHECK(kebab_rpmh_tcs_send(&tcs, &forged) ==
		    KEBAB_RPMH_ERR_NOT_ALLOWED, "forged command refused");
		CHECK(f.wr_count == 0, "zero writes");
	}

	printf("C4 occupied channel or stale completion is refused\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.reg[KEBAB_TCS_CMD_ENABLE / 4] = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "enable non-zero refused");
	CHECK(f.wr_count == 0, "zero writes");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.reg[KEBAB_TCS_CMD_ENABLE / 4] = 0;
	f.status_preset = KEBAB_TCS_CMD_COMPL;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "stale COMPL before trigger refused");
	CHECK(f.wr_count == 0, "zero writes");

	printf("C5 write readback mismatch is bounded\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.mismatch_off = KEBAB_TCS_CMD_ENABLE;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_SINK_FAILED,
	    "readback mismatch is bounded and reported");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_FAILED_PRE_TRIGGER,
	    "phase says pre-trigger");

	printf("C6 completion needs ISSUED and COMPL\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.issued_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "COMPL without ISSUED is not success");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "ISSUED without COMPL is not success");

	printf("C7 post-trigger timeout preserves the channel\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "timeout reported");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_FAILED_POST_TRIGGER,
	    "phase says post-trigger");
	CHECK(f.wr_count == 7, "no cleanup write after the trigger");
	CHECK(f.reg[KEBAB_TCS_CMD_ENABLE / 4] == 1,
	    "enable left as it was, for the record");
	CHECK(tcs.dead == 1, "channel marked unusable");

	printf("C8 stale completion cannot satisfy a later command\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.status_preset = KEBAB_TCS_CMD_COMPL;
	f.issued_after = 1;
	f.compl_after = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "pre-existing COMPL blocks the attempt");

	printf("C9 one shot per boot\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK, "first send ok");
	{
		uint32_t before = f.wr_count;

		CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) ==
		    KEBAB_RPMH_ERR_TCS_STATE, "second send refused");
		CHECK(f.wr_count == before, "no writes on the refused second send");
	}

	printf("C10 only TCS1 is addressed\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f));
	tcs.tcs = 0;
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "TCS0 refused");
	tcs.tcs = 2;
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "TCS2 refused");
	CHECK(f.wr_count == 0, "zero writes for both");

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
