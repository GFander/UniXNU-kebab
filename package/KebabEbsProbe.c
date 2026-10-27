/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabEbsProbe.h"

#include <string.h>

static kebab_ebs_record *g_rec;
static kebab_ebs_target *g_target;
static kebab_ebs_mirror g_mirror;
static void *g_mirror_ctx;
static int g_sealed;

#ifdef KEBAB_EBS_TESTING
static uint32_t g_delay_slot;
static uint32_t g_delay_spins;
#endif

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
	g_sealed = 1;
}

void
kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle, uint64_t guid_ptr,
    uint64_t interface)
{
	if (t == NULL) {
		return;
	}
	memset(t, 0, sizeof(*t));
	t->handle = handle;
	t->guid_ptr = guid_ptr;
	t->interface = interface;
	t->valid = (handle != 0 && interface != 0) ? 1u : 0u;
}

void
kebab_ebs_target_set_guid(kebab_ebs_target *t, const uint8_t guid[16])
{
	if (t == NULL || guid == NULL) {
		return;
	}
	memcpy(t->guid, guid, 16);
	t->have_guid = 1;
}

int
kebab_ebs_target_valid(const kebab_ebs_target *t)
{
	return t != NULL && t->valid != 0 && t->handle != 0 &&
	    t->interface != 0;
}

int
kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, const uint8_t guid[16], uint64_t prot_interface)
{
	if (!kebab_ebs_target_valid(t)) {
		return 0;
	}
	if (t->handle != handle || t->interface != prot_interface) {
		return 0;
	}
	if (t->guid_ptr != 0 && t->guid_ptr != guid_ptr) {
		return 0;
	}
	/*
	 * When the exact GUID bytes were recorded, the caller must present the
	 * same 16 bytes; a pointer match alone would accept another GUID that
	 * happens to live at a reused address.
	 */
	if (t->have_guid != 0) {
		if (guid == NULL || memcmp(t->guid, guid, 16) != 0) {
			return 0;
		}
	}
	return 1;
}

static int
stage_is_global(kebab_ebs_stage stage)
{
	return stage == KEBAB_EBS_E1 || stage == KEBAB_EBS_E2 ||
	    stage == KEBAB_EBS_E7 || stage == KEBAB_EBS_E8;
}

static uint32_t
claim_slot(kebab_ebs_record *rec)
{
	uint32_t slot;

	slot = __atomic_fetch_add(&rec->claimed, 1u, __ATOMIC_ACQ_REL);
	if (slot >= KEBAB_EBS_EVENTS) {
		__atomic_store_n(&rec->overflow, 1u, __ATOMIC_RELAXED);
		return UINT32_MAX;
	}
	return slot;
}

static void
publish(kebab_ebs_record *rec, uint32_t slot, kebab_ebs_stage stage,
    uint32_t context, uint64_t object, uint32_t status)
{
	kebab_ebs_event *ev = &rec->ev[slot];
	uint64_t cur, want;

	ev->stage = (uint32_t)stage;
	ev->context = context;
	ev->status = status;
	ev->object = object;

#ifdef KEBAB_EBS_TESTING
	if (slot == __atomic_load_n(&g_delay_slot, __ATOMIC_ACQUIRE)) {
		uint32_t n = __atomic_load_n(&g_delay_spins, __ATOMIC_ACQUIRE);
		uint32_t d;

		for (d = 0; d < n; d++) {
			__atomic_thread_fence(__ATOMIC_SEQ_CST);
		}
	}
#endif

	/*
	 * seq is published last: until it is non-zero the slot is "claimed but
	 * not committed", which is exactly what a hang before this point looks
	 * like.
	 */
	__atomic_store_n(&ev->seq, slot + 1u, __ATOMIC_RELEASE);

	/*
	 * The newest stage is one 64-bit value, so the stage and the sequence
	 * can never disagree, and a late commit of an older slot cannot make
	 * the display go backwards.
	 */
	want = ((uint64_t)(slot + 1u) << 32) | (uint32_t)stage;
	cur = __atomic_load_n(&rec->newest, __ATOMIC_ACQUIRE);
	while (want > cur) {
		if (__atomic_compare_exchange_n(&rec->newest, &cur, want, 0,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			break;
		}
	}
	if (g_mirror != NULL) {
		g_mirror((uint32_t)stage, g_mirror_ctx);
	}
}

int
kebab_ebs_event_add_global(kebab_ebs_stage stage, uint64_t object,
    uint32_t status)
{
	kebab_ebs_record *rec = g_rec;
	uint32_t slot;

	if (rec == NULL || rec->magic != KEBAB_EBS_MAGIC ||
	    !stage_is_global(stage)) {
		return 0;
	}
	slot = claim_slot(rec);
	if (slot == UINT32_MAX) {
		return 0;
	}
	publish(rec, slot, stage, 0, object, status);
	return 1;
}

int
kebab_ebs_event_add_target(kebab_ebs_stage stage, uint64_t handle,
    uint64_t guid_ptr, const uint8_t guid[16], uint64_t prot_interface,
    uint32_t context, uint64_t object, uint32_t status)
{
	kebab_ebs_record *rec = g_rec;
	uint32_t slot;

	if (rec == NULL || rec->magic != KEBAB_EBS_MAGIC ||
	    stage_is_global(stage)) {
		return 0;
	}
	if (!kebab_ebs_target_matches(g_target, handle, guid_ptr, guid,
	    prot_interface)) {
		__atomic_store_n(&rec->rejected, 1u, __ATOMIC_RELAXED);
		return 0;
	}
	slot = claim_slot(rec);
	if (slot == UINT32_MAX) {
		return 0;
	}
	publish(rec, slot, stage, context, object, status);
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
	g_delay_slot = 0;
	g_delay_spins = 0;
}

void
kebab_ebs_test_delay_slot(uint32_t slot, uint32_t spins)
{
	g_delay_slot = slot;
	g_delay_spins = spins;
}
#endif
