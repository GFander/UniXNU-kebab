/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabEbsProbe.h"

#include <string.h>

static kebab_ebs_record *g_rec;
static kebab_ebs_target *g_target;
static kebab_ebs_callback *g_cb;
static kebab_ebs_mirror g_mirror;
static void *g_mirror_ctx;
static int g_sealed;

#ifdef KEBAB_EBS_TESTING
static uint32_t g_block_slot = UINT32_MAX;
static uint32_t g_released;
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
kebab_ebs_set_callback(kebab_ebs_callback *cb, uint64_t notify, uint64_t event)
{
	if (cb == NULL) {
		return;
	}
	cb->notify = notify;
	cb->event = event;
	cb->valid = (notify != 0 && event != 0) ? 1u : 0u;
	g_cb = cb;
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
	    stage == KEBAB_EBS_E8;
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
	while (slot == __atomic_load_n(&g_block_slot, __ATOMIC_ACQUIRE) &&
	    __atomic_load_n(&g_released, __ATOMIC_ACQUIRE) == 0) {
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
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
	/*
	 * The mirror is handed the published newest value, not this caller's
	 * stage: when an older slot commits late, the display keeps showing
	 * the newer stage instead of repainting the old one.
	 */
	if (g_mirror != NULL) {
		g_mirror(__atomic_load_n(&rec->newest, __ATOMIC_ACQUIRE),
		    g_mirror_ctx);
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
	    stage_is_global(stage) || stage == KEBAB_EBS_E3 ||
	    stage == KEBAB_EBS_E7) {
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

int
kebab_ebs_event_add_callback(kebab_ebs_stage stage, uint64_t notify,
    uint64_t event, uint32_t status)
{
	kebab_ebs_record *rec = g_rec;
	uint32_t slot;

	if (rec == NULL || rec->magic != KEBAB_EBS_MAGIC ||
	    (stage != KEBAB_EBS_E3 && stage != KEBAB_EBS_E7)) {
		return 0;
	}
	if (g_cb == NULL || g_cb->valid == 0 || g_cb->notify != notify ||
	    (g_cb->event != 0 && g_cb->event != event)) {
		__atomic_store_n(&rec->rejected, 1u, __ATOMIC_RELAXED);
		return 0;
	}
	slot = claim_slot(rec);
	if (slot == UINT32_MAX) {
		return 0;
	}
	publish(rec, slot, stage, 0, event, status);
	return 1;
}

#ifdef KEBAB_EBS_TESTING
void
kebab_ebs_test_reset(void)
{
	g_rec = NULL;
	g_target = NULL;
	g_cb = NULL;
	g_mirror = NULL;
	g_mirror_ctx = NULL;
	g_sealed = 0;
	g_block_slot = UINT32_MAX;
	g_released = 0;
}

void
kebab_ebs_test_block_slot(uint32_t slot)
{
	g_block_slot = slot;
	g_released = 0;
}

void
kebab_ebs_test_release_slot(void)
{
	__atomic_store_n(&g_released, 1u, __ATOMIC_RELEASE);
}
#endif
