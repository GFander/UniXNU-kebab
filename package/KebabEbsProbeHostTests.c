/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host tests for the EBS probe record (E4b-F revision). */
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

static uint32_t mirror_stage;
static int mirror_calls;

static void mirror(uint32_t stage, void *ctx)
{
	(void)ctx;
	mirror_stage = stage;
	mirror_calls++;
}

static kebab_ebs_record rec;
static kebab_ebs_target tgt;

static void fresh(void)
{
	kebab_ebs_test_reset();
	memset(&rec, 0, sizeof(rec));
	memset(&tgt, 0, sizeof(tgt));
	kebab_ebs_target_set(&tgt, 0xa000, 0xb000, 0x1111, 0x2222, 0xc000);
	kebab_ebs_init(&rec, &tgt, mirror, NULL);
}

static int add_ok(kebab_ebs_stage s, uint32_t ctx)
{
	return kebab_ebs_event_add(s, 0xa000, 0xb000, 0xc000, ctx, 0x1234, 0);
}

static void *writer(void *arg)
{
	int i;
	(void)arg;

	for (i = 0; i < 33; i++) {
		(void)add_ok(KEBAB_EBS_E1, 1);
	}
	return NULL;
}

int main(void)
{

	printf("F1 identity is enforced inside the entry point\n");
	{
		uint32_t attempted;

		fresh();
		attempted = rec.last_attempted_stage;
		CHECK(kebab_ebs_event_add(KEBAB_EBS_E4, 0xa000, 0xb000, 0xdead, 1,
		    0, 0) == 0, "wrong interface refused");
		CHECK(rec.committed == 0 && rec.last_attempted_stage == attempted,
		    "nothing written on refusal");
		CHECK(rec.lost == 1, "refusal is recorded as lost");
		CHECK(kebab_ebs_event_add(KEBAB_EBS_E4, 0xa000, 0xbeef, 0xc000, 1,
		    0, 0) == 0, "wrong GUID pointer refused");
	}

	printf("F1b an invalid target refuses everything\n");
	{
		memset(&rec, 0, sizeof(rec));
		memset(&tgt, 0, sizeof(tgt));
		kebab_ebs_init(&rec, &tgt, NULL, NULL);
		CHECK(kebab_ebs_event_add(KEBAB_EBS_E1, 0, 0, 0, 0, 0, 0) == 0,
		    "all-zero target refuses");
		CHECK(rec.committed == 0 && rec.last_attempted_stage == 0,
		    "no write with an invalid target");
	}

	printf("F2 ordering, enter/return and commit fields\n");
	fresh();
	mirror_calls = 0;
	CHECK(add_ok(KEBAB_EBS_E4, 7) == 1, "E4 committed");
	CHECK(add_ok(KEBAB_EBS_E4B_ENTER, 7) == 1, "E4B_ENTER committed");
	CHECK(add_ok(KEBAB_EBS_E4B_RETURN, 7) == 1, "E4B_RETURN committed");
	CHECK(add_ok(KEBAB_EBS_E4C_ENTER, 7) == 1, "E4C_ENTER committed");
	CHECK(add_ok(KEBAB_EBS_E4C_RETURN, 7) == 1, "E4C_RETURN committed");
	CHECK(add_ok(KEBAB_EBS_E5_ENTER, 7) == 1, "E5_ENTER committed");
	CHECK(add_ok(KEBAB_EBS_E6_RETURN, 7) == 1, "E6_RETURN committed");
	CHECK(rec.committed == 7 && rec.claimed == 7, "counts agree");
	CHECK(rec.ev[0].seq == 1 && rec.ev[6].seq == 7, "sequence is 1-based");
	CHECK(rec.ev[2].stage == KEBAB_EBS_E4B_RETURN, "return stage distinct");
	CHECK(rec.last_committed_stage == KEBAB_EBS_E6_RETURN,
	    "last committed stage tracks the write");
	CHECK(mirror_calls == 7, "mirror called per committed event");

	printf("F3 uncommitted slots are visible as seq == 0\n");
	CHECK(rec.ev[7].seq == 0, "slot past the last write is empty");

	printf("F4 real reentry: two threads, no shared slot\n");
	fresh();
	{
		pthread_t a, b;

		pthread_create(&a, NULL, writer, NULL);
		pthread_create(&b, NULL, writer, NULL);
		pthread_join(a, NULL);
		pthread_join(b, NULL);
	}
	{
		int ok = 1, seen[KEBAB_EBS_EVENTS + 1];
		uint32_t k;

		memset(seen, 0, sizeof(seen));
		for (k = 0; k < rec.committed && k < KEBAB_EBS_EVENTS; k++) {
			if (rec.ev[k].seq != k + 1) {
				ok = 0;
			}
			seen[rec.ev[k].seq % 65]++;
		}
		CHECK(rec.committed == 64, "both threads filled the record");
		CHECK(rec.lost == 1, "overflow of the 65th write is flagged");
		CHECK(ok == 1, "every committed slot has a unique sequence");
	}

	printf("F5 the record is sealed after init\n");
	fresh();
	{
		kebab_ebs_record other;

		memset(&other, 0, sizeof(other));
		kebab_ebs_init(&other, &tgt, NULL, NULL);
		CHECK(kebab_ebs_sealed() == 1, "sealed flag set");
		CHECK(other.magic == 0 && other.capacity == 0,
		    "second init did not touch the other record");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
