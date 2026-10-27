/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabEbsProbe.h"

#include <string.h>

static kebab_ebs_record *g_rec;
static kebab_ebs_mirror g_mirror;
static void *g_mirror_ctx;

void
kebab_ebs_init(kebab_ebs_record *rec, kebab_ebs_mirror mirror, void *ctx)
{
	if (rec == NULL) {
		return;
	}
	memset(rec, 0, sizeof(*rec));
	rec->magic = KEBAB_EBS_MAGIC;
	g_rec = rec;
	g_mirror = mirror;
	g_mirror_ctx = ctx;
}

void
kebab_ebs_event_add(kebab_ebs_record *rec, kebab_ebs_stage stage,
    uint32_t context, uint64_t object, uint32_t status)
{
	kebab_ebs_event *ev;

	if (rec == NULL || rec->magic != KEBAB_EBS_MAGIC) {
		return;
	}
	rec->last_stage = (uint32_t)stage;
	if (rec->count >= KEBAB_EBS_EVENTS) {
		rec->overflow = 1;
	} else {
		ev = &rec->ev[rec->count];
		ev->seq = rec->count;
		ev->stage = (uint32_t)stage;
		ev->context = context;
		ev->status = status;
		ev->object = object;
		rec->count++;
	}
	/* The mirror gets the stage only: a display byte must never need a
	 * UEFI service or an allocation. */
	if (g_mirror != NULL) {
		g_mirror((uint32_t)stage, g_mirror_ctx);
	}
}

void
kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle, uint64_t guid,
    uint64_t interface)
{
	if (t == NULL) {
		return;
	}
	t->handle = handle;
	t->guid = guid;
	t->interface = interface;
}

int
kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid, uint64_t prot_interface)
{
	if (t == NULL) {
		return 0;
	}
	/*
	 * prot_interface is PROTOCOL_INTERFACE->Interface, which is the value
	 * the driver installed; it is not the PROTOCOL_INTERFACE pointer.
	 */
	return t->handle == handle && t->guid == guid &&
	    t->interface == prot_interface;
}
