/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host tests for the EBS probe record, E4b-F2 revision. */
#include "KebabEbsProbe.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(c, w) do { \
	checks++; \
	if (c) { \
		printf("  PASS  %s\n", w); \
	} else { \
		failures++; \
		printf("  FAIL  %s\n", w); \
	} \
} while (0)

static kebab_ebs_record rec;
static kebab_ebs_target tgt;
static uint32_t mirror_calls;

static void mirror(uint32_t stage, void *ctx)
{
	(void)stage;
	(void)ctx;
	__atomic_fetch_add(&mirror_calls, 1u, __ATOMIC_RELAXED);
}

static void fresh(void)
{
	kebab_ebs_test_reset();
	memset(&rec, 0, sizeof(rec));
	memset(&tgt, 0, sizeof(tgt));
	kebab_ebs_target_set(&tgt, 0xa000, 0xb000, 0xc000);
	kebab_ebs_init(&rec, &tgt, mirror, NULL);
	mirror_calls = 0;
}

static int add_t(kebab_ebs_stage s, uint32_t ctx, uint64_t obj)
{
	return kebab_ebs_event_add_target(s, 0xa000, 0xb000, NULL, 0xc000, ctx,
	    obj, 0);
}

static void *late_commit(void *arg)
{
	(void)arg;
	(void)add_t(KEBAB_EBS_E4, 1, 0x10);
	return NULL;
}

int main(void)
{
	printf("G1 global stages go through the global entry only\n");
	fresh();
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E1, 0, 0) == 1, "E1 accepted");
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E2, 0, 0) == 1, "E2 accepted");
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E4, 0, 0) == 0,
	    "target stage refused by the global entry");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E8, 0xa000, 0xb000, NULL,
	    0xc000, 0, 0, 0) == 0, "global stage refused by the target entry");

	printf("G2 an invalid target refuses every event\n");
	kebab_ebs_test_reset();
	memset(&rec, 0, sizeof(rec));
	memset(&tgt, 0, sizeof(tgt));
	kebab_ebs_init(&rec, &tgt, NULL, NULL);
	CHECK(kebab_ebs_target_valid(&tgt) == 0, "all-zero target is invalid");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0, 0, NULL, 0, 0, 0,
	    0) == 0, "event refused");
	CHECK(rec.claimed == 0, "no slot was claimed");
	CHECK(rec.rejected == 1, "refusal counted as rejected, not overflow");

	printf("G3 identity: interface, pointer and GUID value\n");
	fresh();
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0xa000, 0xb000, NULL,
	    0xdead, 0, 0, 0) == 0, "wrong interface refused");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0xa000, 0xbeef, NULL,
	    0xc000, 0, 0, 0) == 0, "wrong GUID pointer refused");
	{
		uint8_t g1[16], g2[16];

		memset(g1, 0x11, sizeof(g1));
		memset(g2, 0x22, sizeof(g2));
		kebab_ebs_target_set_guid(&tgt, g1);
		CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0xa000, 0xb000,
		    g2, 0xc000, 0, 0, 0) == 0, "different GUID bytes refused");
		CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0xa000, 0xb000,
		    g1, 0xc000, 0, 0, 0) == 1, "same GUID bytes accepted");
	}

	printf("E1 ordering and newest stage\n");
	fresh();
	CHECK(add_t(KEBAB_EBS_E4, 7, 0x4000) == 1, "E4 committed");
	CHECK(add_t(KEBAB_EBS_E4B_ENTER, 7, 0x4000) == 1, "E4B_ENTER");
	CHECK(add_t(KEBAB_EBS_E4B_RETURN, 7, 0x4000) == 1, "E4B_RETURN");
	CHECK(add_t(KEBAB_EBS_E5_ENTER, 7, 0x5000) == 1, "E5_ENTER");
	CHECK(rec.ev[3].seq == 4, "slot 3 has seq 4");
	CHECK((uint32_t)(rec.newest >> 32) == 4, "newest seq is 4");
	CHECK((uint32_t)rec.newest == KEBAB_EBS_E5_ENTER, "newest stage is E5");

	printf("E2 out-of-order commit cannot regress the newest stage\n");
	fresh();
	kebab_ebs_test_delay_slot(0, 2000000u);
	{
		pthread_t t1, t2;

		pthread_create(&t1, NULL, late_commit, NULL);   /* slot 0 */
		while (__atomic_load_n(&rec.claimed, __ATOMIC_ACQUIRE) == 0) {
			;
		}
		pthread_create(&t2, NULL, late_commit, NULL);   /* slot 1 */
		pthread_join(t2, NULL);
		pthread_join(t1, NULL);
	}
	kebab_ebs_test_delay_slot(0, 0);
	CHECK(__atomic_load_n(&rec.ev[0].seq, __ATOMIC_ACQUIRE) == 1,
	    "slot 0 committed late");
	CHECK(__atomic_load_n(&rec.ev[1].seq, __ATOMIC_ACQUIRE) == 2,
	    "slot 1 committed first");
	CHECK((uint32_t)(rec.newest >> 32) == 2, "newest seq is 2, not 1");

	printf("E3 overflow is separate from rejection\n");
	fresh();
	{
		uint32_t i;
		int ok = 0;

		for (i = 0; i < KEBAB_EBS_EVENTS + 3; i++) {
			ok += kebab_ebs_event_add_global(KEBAB_EBS_E1, i, 0);
		}
		CHECK(ok == KEBAB_EBS_EVENTS, "capacity events committed");
		CHECK(rec.overflow == 1 && rec.rejected == 0,
		    "overflow flagged, rejection untouched");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
