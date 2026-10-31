/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host tests for the EBS probe record, E4b-F3 revision. */
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
static kebab_ebs_callback cb;
static uint32_t mirror_calls;
static uint64_t mirror_max;
static uint64_t mirror_last;
static int mirror_regressed;

static void mirror(uint64_t newest, void *ctx)
{
	uint64_t prev;

	(void)ctx;
	__atomic_fetch_add(&mirror_calls, 1u, __ATOMIC_RELAXED);
	prev = __atomic_load_n(&mirror_max, __ATOMIC_ACQUIRE);
	if (newest < prev) {
		mirror_regressed = 1;
	}
	while (newest > prev) {
		if (__atomic_compare_exchange_n(&mirror_max, &prev, newest, 0,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			break;
		}
	}
	mirror_last = newest;
}

static void fresh(void)
{
	kebab_ebs_test_reset();
	memset(&rec, 0, sizeof(rec));
	memset(&tgt, 0, sizeof(tgt));
	memset(&cb, 0, sizeof(cb));
	kebab_ebs_target_set(&tgt, 0xa000, 0xb000, 0xc000);
	kebab_ebs_set_callback(&cb, 0xd000, 0xe000);
	kebab_ebs_init(&rec, &tgt, mirror, NULL);
	mirror_calls = 0;
	mirror_max = 0;
	mirror_last = 0;
	mirror_regressed = 0;
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
	printf("G1 global stages are E1/E2/E8 only\n");
	fresh();
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E1, 0, 0) == 1, "E1 accepted");
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E2, 0, 0) == 1, "E2 accepted");
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E7, 0, 0) == 0,
	    "E7 must go through the callback entry");
	CHECK(kebab_ebs_event_add_global(KEBAB_EBS_E8, 0, 0) == 1, "E8 accepted");

	printf("G2 callback identity gates E3 and E7\n");
	fresh();
	CHECK(kebab_ebs_event_add_callback(KEBAB_EBS_E3, 0xd000, 0xe000, 0) == 1,
	    "E3 with the registered callback accepted");
	CHECK(kebab_ebs_event_add_callback(KEBAB_EBS_E3, 0xdead, 0xe000, 0) == 0,
	    "other notify function refused");
	CHECK(kebab_ebs_event_add_callback(KEBAB_EBS_E7, 0xd000, 0xbeef, 0) == 0,
	    "other event object refused");
	CHECK(kebab_ebs_event_add_callback(KEBAB_EBS_E8, 0xd000, 0xe000, 0) == 0,
	    "E8 is not a callback stage");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E3, 0xa000, 0xb000, NULL,
	    0xc000, 0, 0, 0) == 0, "E3 is not a target stage");

	printf("G3 an invalid target refuses every target event\n");
	kebab_ebs_test_reset();
	memset(&rec, 0, sizeof(rec));
	memset(&tgt, 0, sizeof(tgt));
	kebab_ebs_init(&rec, &tgt, NULL, NULL);
	CHECK(kebab_ebs_target_valid(&tgt) == 0, "all-zero target is invalid");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0, 0, NULL, 0, 0, 0,
	    0) == 0, "event refused");
	CHECK(rec.claimed == 0, "no slot was claimed");
	CHECK(rec.rejected == 1, "refusal counted as rejected");

	printf("G4 identity: interface, pointer and GUID value\n");
	fresh();
	CHECK(add_t(KEBAB_EBS_E4, 1, 0) == 1, "matching target accepted");
	CHECK(kebab_ebs_event_add_target(KEBAB_EBS_E4, 0xa000, 0xb000, NULL,
	    0xdead, 0, 0, 0) == 0, "wrong interface refused");
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

	printf("E1 deterministic out-of-order commit\n");
	fresh();
	kebab_ebs_test_block_slot(0);
	{
		pthread_t t1, t2;

		pthread_create(&t1, NULL, late_commit, NULL);   /* slot 0 */
		while (__atomic_load_n(&rec.claimed, __ATOMIC_ACQUIRE) == 0) {
			;
		}
		pthread_create(&t2, NULL, late_commit, NULL);   /* slot 1 */
		pthread_join(t2, NULL);
		CHECK(__atomic_load_n(&rec.ev[1].seq, __ATOMIC_ACQUIRE) == 2,
		    "slot 1 committed while slot 0 is held");
		kebab_ebs_test_release_slot();
		pthread_join(t1, NULL);
	}
	CHECK(__atomic_load_n(&rec.ev[0].seq, __ATOMIC_ACQUIRE) == 1,
	    "slot 0 committed after slot 1");
	CHECK((uint32_t)(rec.newest >> 32) == 2, "newest seq is 2, not 1");
	CHECK((uint32_t)rec.newest == KEBAB_EBS_E4, "newest stage unchanged");
	CHECK(mirror_regressed == 0, "the mirror never saw a lower value");
	CHECK((uint32_t)(mirror_last >> 32) == 2, "the display shows seq 2");

	printf("E2 the mirror gets the newest value, not the caller stage\n");
	fresh();
	(void)add_t(KEBAB_EBS_E4B_ENTER, 1, 0);
	(void)add_t(KEBAB_EBS_E4B_RETURN, 1, 0);
	CHECK((uint32_t)(mirror_last >> 32) == 2 &&
	    (uint32_t)mirror_last == KEBAB_EBS_E4B_RETURN,
	    "mirror last value is the latest stage");

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

	printf("B1 one-shot identity bindings\n");
	kebab_ebs_test_reset();
	memset(&cb, 0, sizeof(cb));
	memset(&tgt, 0, sizeof(tgt));
	CHECK(kebab_ebs_bind_callback(&cb, 0xd000, 0xe000) == 1,
	    "first callback bind wins");
	CHECK(kebab_ebs_bind_callback(&cb, 0xdead, 0xbeef) == 0,
	    "second callback bind refused");
	CHECK(cb.notify == 0xd000 && cb.event == 0xe000, "identity unchanged");
	{
		uint8_t g[16];

		memset(g, 0x33, sizeof(g));
		CHECK(kebab_ebs_bind_target(&tgt, 0xa000, 0xb000, g, 0xc000) == 1,
		    "first target bind wins");
		CHECK(kebab_ebs_bind_target(&tgt, 0x1, 0x2, g, 0x3) == 0,
		    "second target bind refused");
		CHECK(tgt.handle == 0xa000 && tgt.have_guid == 1,
		    "target identity and GUID kept");
	}
	kebab_ebs_test_reset();

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
