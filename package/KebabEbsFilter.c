/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabEbsFilter.h"

#include <string.h>

/* EFI_EVENT_GROUP_EXIT_BOOT_SERVICES = 27ABF055-B1B8-4C26-8048-748F37BAA2DF,
 * stored the way EFI_GUID is compared in memory. */
const uint8_t kebab_ebs_group_guid[16] = {
	0x55, 0xf0, 0xab, 0x27, 0xb8, 0xb1, 0x26, 0x4c,
	0x80, 0x48, 0x74, 0x8f, 0x37, 0xba, 0xa2, 0xdf
};

/* A made-up group used by the tests to prove other groups are untouched. */
const uint8_t kebab_other_group_guid[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01
};

const uint8_t kebab_rpmh_ffs_guid[16] = {
	0xd1, 0xf4, 0x29, 0xcb, 0x37, 0x7f, 0x92, 0x46,
	0xa4, 0x16, 0x93, 0xe8, 0x2e, 0x21, 0x97, 0x66
};

static kebab_filter_image g_images[KEBAB_FILTER_MAX_IMAGES];
static uint32_t g_image_count;
static int g_capacity_hit;
static kebab_filter_event g_events[KEBAB_FILTER_MAX_EVENTS];
static uint32_t g_event_count;

void
kebab_filter_reset(void)
{
	memset(g_images, 0, sizeof(g_images));
	g_image_count = 0;
	g_capacity_hit = 0;
	memset(g_events, 0, sizeof(g_events));
	g_event_count = 0;
}

int
kebab_filter_add_image(uint64_t base, uint64_t size, int is_rpmh)
{
	if (size == 0) {
		return 0;
	}
	if (g_image_count >= KEBAB_FILTER_MAX_IMAGES) {
		g_capacity_hit = 1;
		return 0;
	}
	g_images[g_image_count].base = base;
	g_images[g_image_count].size = size;
	g_images[g_image_count].is_rpmh = is_rpmh != 0;
	g_image_count++;
	return 1;
}

int
kebab_filter_add_rpmh_image(const uint8_t ffs_guid[16], uint64_t base,
    uint64_t size)
{
	if (ffs_guid == NULL || memcmp(ffs_guid, kebab_rpmh_ffs_guid, 16) != 0) {
		return 0;
	}
	return kebab_filter_add_image(base, size, 1);
}

int
kebab_filter_capacity_hit(void)
{
	return g_capacity_hit;
}

int
kebab_filter_note_event(const uint8_t group[16], uint64_t notify,
    uint64_t event)
{
	if (group == NULL || g_event_count >= KEBAB_FILTER_MAX_EVENTS) {
		return 0;
	}
	memcpy(g_events[g_event_count].group, group, 16);
	g_events[g_event_count].notify = notify;
	g_events[g_event_count].event = event;
	g_event_count++;
	return 1;
}

static int
in_rpmh_image(uint64_t addr)
{
	uint32_t i;

	for (i = 0; i < g_image_count; i++) {
		if (g_images[i].is_rpmh != 0 && addr >= g_images[i].base &&
		    addr - g_images[i].base < g_images[i].size) {
			return 1;
		}
	}
	return 0;
}

static int
registered(const uint8_t group[16], uint64_t notify, uint64_t event)
{
	uint32_t i;

	for (i = 0; i < g_event_count; i++) {
		if (memcmp(g_events[i].group, group, 16) == 0 &&
		    g_events[i].notify == notify && g_events[i].event == event) {
			return 1;
		}
	}
	return 0;
}

int
kebab_filter_should_skip(kebab_filter_policy policy, const uint8_t group[16],
    uint64_t notify, uint64_t event)
{
	if (group == NULL || policy == KEBAB_FILTER_OFF) {
		return 0;
	}
	/* Both policies are scoped to the ExitBootServices group. */
	if (memcmp(group, kebab_ebs_group_guid, 16) != 0) {
		return 0;
	}
	if (policy == KEBAB_FILTER_SKIP_EBS_GROUP) {
		return 1;
	}
	/* A: only a callback that was both registered and lives in RpmhDxe. */
	return registered(group, notify, event) && in_rpmh_image(notify);
}
