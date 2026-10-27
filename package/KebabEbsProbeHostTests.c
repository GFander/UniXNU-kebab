/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host tests for the EBS probe record: ordering, overflow, identity match. */
#include "KebabEbsProbe.h"

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

int main(void)
{
	kebab_ebs_record rec;
	kebab_ebs_record *heap = &rec;
	kebab_ebs_target t;
	uint32_t i;

	printf("P1 record init and ordering\n");
	memset(&rec, 0, sizeof(rec));
	kebab_ebs_init(heap, mirror, NULL);
	mirror_calls = 0;
	kebab_ebs_event_add(heap, KEBAB_EBS_E1, 1, 0, 0);
	kebab_ebs_event_add(heap, KEBAB_EBS_E2, 1, 0, 0);
	CHECK(rec.magic == KEBAB_EBS_MAGIC, "magic set");
	CHECK(rec.count == 2, "two events recorded");
	CHECK(rec.ev[0].seq == 0 && rec.ev[1].seq == 1, "sequence is monotonic");
	CHECK(rec.ev[0].stage == KEBAB_EBS_E1 && rec.ev[1].stage == KEBAB_EBS_E2,
	    "stages stored in order");
	CHECK(rec.last_stage == KEBAB_EBS_E2, "last stage mirrored");
	CHECK(mirror_calls == 2 && mirror_stage == KEBAB_EBS_E2,
	    "mirror saw both writes");

	printf("P2 nested uninstall keeps history\n");
	memset(&rec, 0, sizeof(rec));
	kebab_ebs_init(heap, NULL, NULL);
	{
		kebab_ebs_stage seq[] = {
			KEBAB_EBS_E4, KEBAB_EBS_E4A, KEBAB_EBS_E4B,
			KEBAB_EBS_E4C, KEBAB_EBS_E4B, KEBAB_EBS_E5,
			KEBAB_EBS_E6
		};

		for (i = 0; i < sizeof(seq)/sizeof(seq[0]); i++) {
			kebab_ebs_event_add(heap, seq[i], 7, 0x1000 + i, 0);
		}
	}
	CHECK(rec.count == 7, "all seven stages kept");
	CHECK(rec.ev[3].stage == KEBAB_EBS_E4C, "OpenData free visible");
	CHECK(rec.ev[4].stage == KEBAB_EBS_E4B, "second disconnect visible");

	printf("P3 overflow is flagged, not silently dropped\n");
	memset(&rec, 0, sizeof(rec));
	kebab_ebs_init(heap, NULL, NULL);
	for (i = 0; i < KEBAB_EBS_EVENTS + 5; i++) {
		kebab_ebs_event_add(heap, KEBAB_EBS_E1, 1, i, 0);
	}
	CHECK(rec.count == KEBAB_EBS_EVENTS, "count capped at capacity");
	CHECK(rec.overflow == 1, "overflow flag set");
	CHECK(rec.ev[KEBAB_EBS_EVENTS - 1].object == KEBAB_EBS_EVENTS - 1,
	    "last stored event intact");

	printf("P4 identity match uses the interface pointer\n");
	kebab_ebs_target_set(&t, 0xaaaa, 0xbbbb, 0xcccc);
	CHECK(kebab_ebs_target_matches(&t, 0xaaaa, 0xbbbb, 0xcccc) == 1,
	    "exact handle/guid/interface matches");
	CHECK(kebab_ebs_target_matches(&t, 0xaaaa, 0xbbbb, 0xdddd) == 0,
	    "another interface does not match");
	CHECK(kebab_ebs_target_matches(&t, 0xaaaa, 0xeeee, 0xcccc) == 0,
	    "another GUID does not match");
	CHECK(kebab_ebs_target_matches(&t, 0xffff, 0xbbbb, 0xcccc) == 0,
	    "another handle does not match");
	{
		/* A PROTOCOL_INTERFACE pointer is never the interface value. */
		uint64_t prot_object = 0x9999;

		CHECK(kebab_ebs_target_matches(&t, 0xaaaa, 0xbbbb,
		    prot_object) == 0, "the Prot object address is rejected");
	}

	printf("P5 unknown record refuses writes\n");
	{
		kebab_ebs_record bad;

		memset(&bad, 0, sizeof(bad));
		kebab_ebs_event_add(&bad, KEBAB_EBS_E1, 1, 0, 0);
		CHECK(bad.count == 0, "record without magic is not written");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
