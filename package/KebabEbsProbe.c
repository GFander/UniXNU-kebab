/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabEbsProbe.h"

#include <string.h>

static kebab_ebs_record *g_rec;
static kebab_ebs_target *g_target;
static kebab_ebs_mirror g_mirror;
static void *g_mirror_ctx;
static int g_sealed;

void
kebab_ebs_init(kebab_ebs_record *rec, kebab_ebs_target *target,
    kebab_ebs_mirror mirror, void *ctx)
{
	if (rec == NULL || target == NULL || g_sealed != 0) {
		return;
	}
	memset(rec, 0, sizeof(*rec));
	rec->magic = KEBAB_EBS_MAGIC;
	rec->version = KEBAB_EBS_VERSION;
	rec->record_size = (uint32_t)sizeof(*rec);
	rec->capacity = KEBAB_EBS_EVENTS;
	g_rec = rec;
	g_target = target;
	g_mirror = mirror;
	g_mirror_ctx = ctx;
	g_sealed = 1;              /* one record per boot */
}

int
kebab_ebs_sealed(void)
{
	return g_sealed;
}

void
kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle, uint64_t guid_ptr,
    uint64_t guid_lo, uint64_t guid_hi, uint64_t interface)
{
	if (t == NULL) {
		return;
	}
	memset(t, 0, sizeof(*t));
	t->handle = handle;
	t->guid_ptr = guid_ptr;
	t->guid_lo = guid_lo;
	t->guid_hi = guid_hi;
	t->interface = interface;
	t->valid = (handle != 0 && interface != 0) ? 1u : 0u;
}

int
kebab_ebs_target_valid(const kebab_ebs_target *t)
{
	return t != NULL && t->valid != 0 && t->handle != 0 &&
	    t->interface != 0;
}

int
kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, uint64_t prot_interface)
{
	if (!kebab_ebs_target_valid(t)) {
		return 0;
	}
	/* prot_interface is PROTOCOL_INTERFACE->Interface, never the record. */
	return t->handle == handle && t->guid_ptr == guid_ptr &&
	    t->interface == prot_interface;
}

int
kebab_ebs_event_add(kebab_ebs_stage stage, uint64_t handle, uint64_t guid_ptr,
    uint64_t prot_interface, uint32_t context, uint64_t object,
    uint32_t status)
{
	kebab_ebs_record *rec = g_rec;
	uint32_t slot;
	kebab_ebs_event *ev;

	if (rec == NULL || rec->magic != KEBAB_EBS_MAGIC) {
		return 0;
	}
	/*
	 * Identity is checked here, before anything is written: a pool free in
	 * another driver must not even touch last_attempted_stage.
	 */
	if (!kebab_ebs_target_matches(g_target, handle, guid_ptr,
	    prot_interface)) {
		__atomic_store_n(&rec->lost, 1u, __ATOMIC_RELAXED);
		return 0;
	}
	rec->last_attempted_stage = (uint32_t)stage;
	slot = __atomic_fetch_add(&rec->claimed, 1u, __ATOMIC_ACQ_REL);
	if (slot >= KEBAB_EBS_EVENTS) {
		__atomic_store_n(&rec->lost, 1u, __ATOMIC_RELAXED);
		return 0;
	}
	ev = &rec->ev[slot];
	ev->stage = (uint32_t)stage;
	ev->context = context;
	ev->status = status;
	ev->object = object;
	/* Publishing seq last marks the slot committed; a hang before this
	 * leaves seq == 0, so "entered" is never mistaken for "committed". */
	__atomic_store_n(&ev->seq, slot + 1u, __ATOMIC_RELEASE);
	__atomic_store_n(&rec->committed, slot + 1u, __ATOMIC_RELEASE);
	rec->last_committed_stage = (uint32_t)stage;
	if (g_mirror != NULL) {
		g_mirror((uint32_t)stage, g_mirror_ctx);
	}
	return 1;
}

#ifdef KEBAB_EBS_TESTING
void
kebab_ebs_test_reset(void)
{
	g_rec = NULL;
	g_target = NULL;
	g_mirror = NULL;
	g_mirror_ctx = NULL;
	g_sealed = 0;
}
#endif
