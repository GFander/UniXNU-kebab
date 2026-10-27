/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define _GNU_SOURCE
/*
 * R5-C2/C3: the TCS1 send state machine against a virtual register file,
 * including per-stage fault injection.  No MMIO, no kernel, no device.
 */
#include "KebabRpmhVrm.h"

#include <pthread.h>
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
	int      mismatch_off;
	int      wr_fail_off;
	int      rd_fail_off;
	int      cleanup_fail;
	int      status_reads;
	int      issued_after;
	int      compl_after;
	uint32_t status_preset;
	int      triggered;
	int      trigger_no_echo;
	int      now_mode;      /* 0 advance, 1 frozen, 2 backwards, 3 no clock */
	uint64_t now;
} fake;

static int fake_rd(void *ctx, uint32_t off, uint32_t *val)
{
	fake *f = ctx;

	if (off + 4 > REGS * 4) {
		return -1;
	}
	if (f->rd_fail_off >= 0 && off == (uint32_t)f->rd_fail_off) {
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

static uint64_t fake_now(void *ctx)
{
	fake *f = ctx;

	if (f->now_mode == 1) {
		return f->now;          /* frozen: the poll cap must end it */
	}
	if (f->now_mode == 2) {
		f->now -= 50;           /* backwards: unsigned math must not loop */
		return f->now;
	}
	f->now += 100;                  /* 100 us per call by default */
	return f->now;
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
	if (f->mismatch_off >= 0 && off == (uint32_t)f->mismatch_off) {
		f->reg[off / 4] = val ^ 0x1u;
		return 0;
	}
	if (f->cleanup_fail != 0 && off == KEBAB_TCS_CONTROL &&
	    (val & KEBAB_TCS_AMC_TRIGGER) == 0 && f->triggered) {
		/* Let the trigger write land, break the release write. */
		f->reg[off / 4] = val ^ 0x1u;
		return 0;
	}
	if (off == KEBAB_TCS_CONTROL && f->trigger_no_echo != 0 &&
	    (val & KEBAB_TCS_AMC_TRIGGER) != 0) {
		f->triggered = 1;
		return 0;       /* the write lands, the register does not echo */
	}
	f->reg[off / 4] = val;
	if (off == KEBAB_TCS_CONTROL) {
		f->triggered = (val & KEBAB_TCS_AMC_TRIGGER) != 0;
	}
	return 0;
}

static void fake_init(fake *f, uint32_t control)
{
	memset(f, 0, sizeof(*f));
	f->reg[KEBAB_TCS_CONTROL / 4] = control;
	f->mismatch_off = -1;
	f->wr_fail_off = -1;
	f->rd_fail_off = -1;
	f->issued_after = 1;
	f->compl_after = 2;
}

static kebab_tcs_io fake_io(fake *f)
{
	kebab_tcs_io io = { fake_rd, fake_wr, fake_now, f };

	return io;
}


static pthread_barrier_t race_barrier;

typedef struct {
	kebab_rpmh_tcs  *tcs;
	kebab_rpmh_cmd  *cmd;
	int             *rc;
} thread_arg;

static void *race_thread(void *arg)
{
	thread_arg *a = arg;

	pthread_barrier_wait(&race_barrier);
	*a->rc = kebab_rpmh_tcs_send(a->tcs, a->cmd);
	return NULL;
}

int main(void)
{
	kebab_rpmh_cmd cmd;
	kebab_rpmh_tcs tcs;
	kebab_rpmh_lock lock;
	fake f;

	kebab_rpmh_encode_enable("ldoa13", &cmd);
	memset(&lock, 0, sizeof(lock));

	printf("C1 initial CONTROL=0x00010000 is accepted\n");
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK, "send succeeds");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_COMPLETE, "phase complete");

	printf("C2 normal completion and cleanup order\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK, "send ok");
	CHECK(f.wr_count == 10, "ten register writes in total");
	CHECK(f.wr_off[0] == KEBAB_TCS_CMD_MSGID && f.wr_val[0] == cmd.msgid,
	    "msgid first");
	CHECK(f.wr_off[1] == KEBAB_TCS_CMD_ADDR && f.wr_val[1] == cmd.addr,
	    "addr second");
	CHECK(f.wr_off[2] == KEBAB_TCS_CMD_DATA && f.wr_val[2] == cmd.data,
	    "data third");
	CHECK(f.wr_off[6] == KEBAB_TCS_CONTROL &&
	    (f.wr_val[6] & KEBAB_TCS_AMC_TRIGGER) != 0, "trigger seventh");
	CHECK(f.wr_off[7] == KEBAB_TCS_CONTROL &&
	    (f.wr_val[7] & KEBAB_TCS_AMC_TRIGGER) == 0 &&
	    (f.wr_val[7] & KEBAB_TCS_AMC_ENABLE) != 0,
	    "trigger released first on cleanup");
	CHECK(f.wr_off[8] == KEBAB_TCS_CMD_ENABLE && f.wr_val[8] == 0 &&
	    f.wr_off[9] == KEBAB_TCS_CMD_WAIT && f.wr_val[9] == 0,
	    "enable and wait cleared last");

	printf("C3 unauthorized command never reaches the registers\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	{
		kebab_rpmh_cmd forged = { KEBAB_RPMH_MSGID_WRITE_ACK,
			0x00040c00u, 3300u };

		CHECK(kebab_rpmh_tcs_send(&tcs, &forged) ==
		    KEBAB_RPMH_ERR_NOT_ALLOWED, "forged command refused");
		CHECK(f.wr_count == 0, "zero writes");
	}

	printf("C4 occupied or dirty channel is refused\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.reg[KEBAB_TCS_CMD_ENABLE / 4] = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "enable non-zero refused");
	CHECK(f.wr_count == 0, "zero writes");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.status_preset = KEBAB_TCS_CMD_COMPL;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "stale COMPL refused");
	CHECK(f.wr_count == 0, "zero writes");

	printf("C5 readback mismatch is bounded and locks the channel\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.mismatch_off = KEBAB_TCS_CMD_ENABLE;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "unconfirmed write is uncertainty, not a rollback");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_FAILED_PRE_TRIGGER, "pre-trigger phase");
	CHECK(lock.dead == 1, "lock closed after a failed attempt");

	printf("C6 completion needs ISSUED and COMPL\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.issued_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "COMPL without ISSUED is not success");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "ISSUED without COMPL is not success");

	printf("C7 post-trigger timeout preserves the channel\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "timeout reported");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_FAILED_POST_TRIGGER,
	    "post-trigger phase");
	CHECK(f.wr_count == 7, "no cleanup write after the trigger");
	CHECK(f.reg[KEBAB_TCS_CMD_ENABLE / 4] == 1, "enable kept as evidence");
	CHECK(lock.dead == 1, "channel locked");

	printf("C8 failure at the very first write rolls back nothing\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.wr_fail_off = KEBAB_TCS_CMD_MSGID;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_SINK_FAILED,
	    "msgid write failure reported");
	CHECK(f.wr_count == 1, "only the failed write was attempted");
	CHECK(lock.dead == 1, "lock closed");

	printf("C9 cleanup failure is not reported as success\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.cleanup_fail = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_RECLAIM,
	    "completion with a failed release is RECLAIM");
	CHECK(tcs.phase == KEBAB_TCS_PHASE_COMPLETE_UNCLEAN,
	    "phase says complete but unclean");
	CHECK(lock.used == 1 && lock.dead == 1, "channel locked for good");

	printf("C10 status read failure is refused before any write\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.rd_fail_off = KEBAB_TCS_CMD_STATUS;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "unreadable status refused");
	CHECK(f.wr_count == 0, "zero writes");

	printf("C11 one shot survives a re-init of the channel\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK, "first send ok");
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	{
		uint32_t before = f.wr_count;

		CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) ==
		    KEBAB_RPMH_ERR_TCS_STATE, "second send refused after re-init");
		CHECK(f.wr_count == before, "no writes on the refused send");
	}

	printf("C12 only TCS1 is addressed\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	tcs.tcs = 0;
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "TCS0 refused");
	tcs.tcs = 2;
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "TCS2 refused");
	CHECK(f.wr_count == 0, "zero writes for both");


	printf("C13 preflight read failure closes the lock\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.rd_fail_off = KEBAB_TCS_CMD_WAIT;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "unreadable preflight register refused");
	CHECK(lock.dead == 1, "fail-closed");

	printf("C14 a stale ISSUED bit is refused\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.status_preset = KEBAB_TCS_CMD_ISSUED;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "leftover ISSUED refused");
	CHECK(f.wr_count == 0, "zero writes");

	printf("C15 the trigger write is not expected to echo\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.trigger_no_echo = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_OK,
	    "completion judged by status, not by the trigger bit");

	printf("C16 a stalled clock cannot spin forever\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	f.now = 0;
	f.now_mode = 1;         /* frozen clock */
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "frozen clock still ends by the poll cap");
	CHECK(f.status_reads >= 1000, "the poll cap was actually exercised");

	printf("C17 a busy lock refuses a second caller\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	lock.busy = 1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TCS_STATE,
	    "concurrent claim refused");
	CHECK(f.wr_count == 0, "zero writes");


	printf("C18 completion has a real wall-clock budget\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "normal clock still times out");
	CHECK(f.status_reads > 100 && f.status_reads < 3000,
	    "the wall budget ends the loop, not the poll cap");

	printf("C19 a backwards clock cannot extend the wait\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	f.now = 1000000;
	f.now_mode = 2;
	kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "backwards clock terminates");

	printf("C20 no clock callback falls back to the poll cap\n");
	memset(&lock, 0, sizeof(lock));
	fake_init(&f, KEBAB_TCS_AMC_ENABLE);
	f.compl_after = -1;
	{
		kebab_tcs_io io = { fake_rd, fake_wr, NULL, &f };

		kebab_rpmh_tcs_init(&tcs, io, &lock);
	}
	CHECK(kebab_rpmh_tcs_send(&tcs, &cmd) == KEBAB_RPMH_ERR_TIMEOUT,
	    "missing clock still terminates");

	printf("C21 two threads cannot both claim the channel\n");
	{
		int round, wins = 0, refusals = 0, bad_writes = 0;

		for (round = 0; round < 50; round++) {
			int rc1 = -1, rc2 = -1;
			thread_arg a1 = { &tcs, &cmd, &rc1 };
			thread_arg a2 = { &tcs, &cmd, &rc2 };
			pthread_t t1, t2;

			memset(&lock, 0, sizeof(lock));
			fake_init(&f, KEBAB_TCS_AMC_ENABLE);
			kebab_rpmh_tcs_init(&tcs, fake_io(&f), &lock);
			pthread_barrier_init(&race_barrier, NULL, 3);
			pthread_create(&t1, NULL, race_thread, &a1);
			pthread_create(&t2, NULL, race_thread, &a2);
			pthread_barrier_wait(&race_barrier);
			pthread_join(t1, NULL);
			pthread_join(t2, NULL);
			pthread_barrier_destroy(&race_barrier);
			if ((rc1 == KEBAB_RPMH_OK) != (rc2 == KEBAB_RPMH_OK)) {
				wins++;
			}
			if (rc1 == KEBAB_RPMH_ERR_TCS_STATE ||
			    rc2 == KEBAB_RPMH_ERR_TCS_STATE) {
				refusals++;
			}
			if (f.wr_count != 10) {
				bad_writes++;
			}
		}
		CHECK(wins == 50, "every round had exactly one winner");
		CHECK(refusals == 50, "every round refused the loser");
		CHECK(bad_writes == 0, "every round issued exactly one transaction");
	}

	printf("C22 the global lock is shared across channels\n");
	{
		kebab_rpmh_tcs tcs2;
		kebab_rpmh_lock *g1, *g2;

		kebab_rpmh_tcs_init_global(&tcs, fake_io(&f));
		kebab_rpmh_tcs_init_global(&tcs2, fake_io(&f));
		g1 = kebab_rpmh_global_lock_get();
		g2 = kebab_rpmh_global_lock_get();
		CHECK(g1 == g2, "both channels use the same lock object");
		CHECK(tcs.lock == g1 && tcs2.lock == g1, "init_global wired it through");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
