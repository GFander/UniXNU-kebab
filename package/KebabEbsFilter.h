/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * O3-C6-E5: offline model of the two EBS diagnostics.
 *
 * The real patch hooks CoreDispatchEventNotifies() (Event.c) and decides per
 * event whether to call NotifyFunction.  This module is the decision logic on
 * its own, so the six required behaviours can be tested without a firmware
 * build.
 *
 * Identity is never a hard-coded RVA: an event is "the RpmhDxe callback" only
 * when its EventGroup is EFI_EVENT_GROUP_EXIT_BOOT_SERVICES, its NotifyFunction
 * lies inside a registered RpmhDxe image, and its registration was observed
 * (event pointer + notify function).  If any of that is missing, nothing is
 * skipped.
 */
#ifndef KEBAB_EBS_FILTER_H
#define KEBAB_EBS_FILTER_H

#include <stdint.h>

typedef enum {
	KEBAB_FILTER_OFF = 0,           /* original Mu behaviour */
	KEBAB_FILTER_SKIP_RPMH_CALLBACK,/* A: only the identified callback */
	KEBAB_FILTER_SKIP_EBS_GROUP,    /* B: whole ExitBootServices group */
} kebab_filter_policy;

#define KEBAB_FILTER_MAX_IMAGES 16
#define KEBAB_FILTER_MAX_EVENTS 64

typedef struct {
	uint64_t base;
	uint64_t size;
	uint32_t is_rpmh;
} kebab_filter_image;

typedef struct {
	uint8_t  group[16];
	uint64_t notify;
	uint64_t event;
} kebab_filter_event;

void kebab_filter_reset(void);
int kebab_filter_add_image(uint64_t base, uint64_t size, int is_rpmh);
/* Called from the CreateEventEx hook; only EBS-group registrations matter. */
int kebab_filter_note_event(const uint8_t group[16], uint64_t notify,
    uint64_t event);

/* The decision CoreDispatchEventNotifies() would take. */
int kebab_filter_should_skip(kebab_filter_policy policy,
    const uint8_t group[16], uint64_t notify, uint64_t event);

/* Test helpers: the EBS group GUID as bytes (little endian, in-memory form). */
extern const uint8_t kebab_ebs_group_guid[16];
extern const uint8_t kebab_other_group_guid[16];

#endif /* KEBAB_EBS_FILTER_H */
