/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* O3-C6-E5 host tests: the two EBS diagnostics against a mock dispatcher. */
#include "KebabEbsFilter.h"

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

typedef struct {
	const uint8_t *group;
	uint64_t notify;
	uint64_t event;
	int ran;
} mock_event;

static int
dispatch(mock_event *evs, int n, kebab_filter_policy policy)
{
	int i, ran = 0;

	for (i = 0; i < n; i++) {
		if (kebab_filter_should_skip(policy, evs[i].group, evs[i].notify,
		    evs[i].event)) {
			continue;
		}
		evs[i].ran = 1;
		ran++;
	}
	return ran;
}

#define RPMH_BASE 0x40000000ull
#define RPMH_SIZE 0x10000ull
#define RPMH_CB   (RPMH_BASE + 0x149c)
#define OTHER_BASE 0x50000000ull
#define OTHER_CB  (OTHER_BASE + 0x2000)
#define EV1       0x1000ull
#define EV2       0x2000ull
#define EV3       0x3000ull

static mock_event evs[3];

static void
setup(void)
{
	kebab_filter_reset();
	(void)kebab_filter_add_image(RPMH_BASE, RPMH_SIZE, 1);
	(void)kebab_filter_add_image(OTHER_BASE, RPMH_SIZE, 0);
	(void)kebab_filter_note_event(kebab_ebs_group_guid, RPMH_CB, EV1);
	memset(evs, 0, sizeof(evs));
	evs[0].group = kebab_ebs_group_guid;
	evs[0].notify = RPMH_CB;          /* the RpmhDxe callback */
	evs[0].event = EV1;
	evs[1].group = kebab_ebs_group_guid;
	evs[1].notify = OTHER_CB;         /* some other driver's EBS callback */
	evs[1].event = EV2;
	evs[2].group = kebab_other_group_guid;
	evs[2].notify = OTHER_CB;         /* not an EBS event at all */
	evs[2].event = EV3;
}

int main(void)
{
	int ran;

	printf("T1 policy A skips the identified RpmhDxe callback\n");
	setup();
	ran = dispatch(evs, 3, KEBAB_FILTER_SKIP_RPMH_CALLBACK);
	CHECK(evs[0].ran == 0, "RpmhDxe callback skipped");
	CHECK(evs[1].ran == 1, "other EBS callback still ran");
	CHECK(evs[2].ran == 1, "non-EBS group still ran");
	CHECK(ran == 2, "two callbacks executed");

	printf("T2 policy A does not skip an unregistered EBS callback\n");
	setup();
	kebab_filter_reset();
	(void)kebab_filter_add_image(RPMH_BASE, RPMH_SIZE, 1);
	/* no note_event at all: identification is incomplete */
	ran = dispatch(evs, 3, KEBAB_FILTER_SKIP_RPMH_CALLBACK);
	CHECK(evs[0].ran == 1, "unidentified callback keeps running");
	CHECK(ran == 3, "all callbacks executed");

	printf("T3 policy A does not skip a callback outside RpmhDxe\n");
	setup();
	kebab_filter_reset();
	(void)kebab_filter_add_image(RPMH_BASE, RPMH_SIZE, 1);
	/* registered, but the notify address is not inside RpmhDxe */
	(void)kebab_filter_note_event(kebab_ebs_group_guid, OTHER_CB, EV2);
	ran = dispatch(evs, 3, KEBAB_FILTER_SKIP_RPMH_CALLBACK);
	CHECK(evs[0].ran == 1 && evs[1].ran == 1 && ran == 3,
	    "foreign callback untouched");

	printf("T4 policy B skips the whole EBS group only\n");
	setup();
	ran = dispatch(evs, 3, KEBAB_FILTER_SKIP_EBS_GROUP);
	CHECK(evs[0].ran == 0 && evs[1].ran == 0, "both EBS callbacks skipped");
	CHECK(evs[2].ran == 1, "non-EBS group untouched");
	CHECK(ran == 1, "exactly one callback executed");

	printf("T5 other groups are never affected by either policy\n");
	setup();
	{
		mock_event one = { kebab_other_group_guid, OTHER_CB, EV3, 0 };

		CHECK(dispatch(&one, 1, KEBAB_FILTER_SKIP_EBS_GROUP) == 1 &&
		    one.ran == 1, "group B keeps other groups");
		one.ran = 0;
		CHECK(dispatch(&one, 1, KEBAB_FILTER_SKIP_RPMH_CALLBACK) == 1 &&
		    one.ran == 1, "group A keeps other groups");
	}

	printf("T6 with both switches off, behaviour is the original one\n");
	setup();
	ran = dispatch(evs, 3, KEBAB_FILTER_OFF);
	CHECK(ran == 3 && evs[0].ran == 1 && evs[1].ran == 1 &&
	    evs[2].ran == 1, "every callback ran");

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
