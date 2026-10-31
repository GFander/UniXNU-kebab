/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Kebab EBS probe record (E4b-F3 revision).
 *
 * Fixed, pre-allocated record written without allocation or UEFI services.
 * Rules this revision enforces:
 *
 *   - a slot index is claimed with an atomic fetch_add, but a slot only counts
 *     as committed once its seq is released; readers must look at seq, not at
 *     a shared count that can regress when commits finish out of order;
 *   - the newest stage is tracked as (seq, stage) in one 64-bit atomic, so the
 *     display cannot go backwards or pair a stage with someone else's seq;
 *   - global lifecycle events (E1/E2/E8) and target events (E3..E7) have
 *     separate entry points, so a global event never needs a fake protocol
 *     identity and a foreign driver still cannot fake a target event;
 *   - identity compares handle, GUID pointer and, when available, the 128-bit
 *     GUID value plus the interface pointer.
 */
#ifndef KEBAB_EBS_PROBE_H
#define KEBAB_EBS_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* Bytes K R S C in memory (little endian). */
#define KEBAB_EBS_MAGIC   0x4353524bu
#define KEBAB_EBS_VERSION 4u
#define KEBAB_EBS_EVENTS  64u

typedef enum {
	KEBAB_EBS_E1 = 1,          /* CoreTerminateMemoryMap returned */
	KEBAB_EBS_E2,              /* EBS notification starts */
	KEBAB_EBS_E3,              /* target callback about to run */
	KEBAB_EBS_E4,              /* uninstall entered */
	KEBAB_EBS_E4A,             /* find returned */
	KEBAB_EBS_E4B_ENTER,       /* disconnect entered */
	KEBAB_EBS_E4B_RETURN,      /* disconnect returned */
	KEBAB_EBS_E4C_ENTER,       /* OpenData free entered */
	KEBAB_EBS_E4C_RETURN,      /* OpenData free returned */
	KEBAB_EBS_E5_ENTER,        /* Prot free entered */
	KEBAB_EBS_E6_RETURN,       /* Prot free returned */
	KEBAB_EBS_E7,              /* target callback returned */
	KEBAB_EBS_E8,              /* DarwinBoot saw EBS success */
} kebab_ebs_stage;

typedef struct {
	uint32_t seq;              /* 0 = claimed but not committed */
	uint32_t stage;
	uint32_t context;
	uint32_t status;
	uint64_t object;
} kebab_ebs_event;

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t record_size;
	uint32_t capacity;
	uint32_t claimed;
	uint32_t overflow;         /* capacity exhausted */
	uint32_t rejected;         /* identity refused */
	uint32_t pad;
	uint64_t newest;           /* (seq << 32) | stage, atomic */
	kebab_ebs_event ev[KEBAB_EBS_EVENTS];
} kebab_ebs_record;

typedef struct {
	uint64_t handle;
	uint64_t guid_ptr;
	uint8_t  guid[16];
	uint32_t have_guid;
	uint64_t interface;
	uint32_t valid;
} kebab_ebs_target;

/* The mirror receives the published newest value (seq << 32 | stage), not the
 * caller's stage, so an older slot that commits late cannot repaint an older
 * stage.  Consumers must still keep their own monotonic guard. */
typedef void (*kebab_ebs_mirror)(uint64_t newest, void *ctx);

/* E3/E7 are tied to the registered EBS callback, not to a protocol identity:
 * the DXE dispatcher sees NotifyFunction and the event object, not the
 * uninstall handle/GUID/interface. */
typedef struct {
	uint64_t notify;
	uint64_t event;
	uint32_t valid;
} kebab_ebs_callback;
void kebab_ebs_set_callback(kebab_ebs_callback *cb, uint64_t notify,
    uint64_t event);
/* One-shot bindings: the first successful call wins, later calls are refused
 * so a later driver cannot hijack the identity. */
int kebab_ebs_bind_callback(kebab_ebs_callback *cb, uint64_t notify,
    uint64_t event);
int kebab_ebs_bind_target(kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, const uint8_t guid[16], uint64_t interface);

void kebab_ebs_init(kebab_ebs_record *rec, kebab_ebs_target *target,
    kebab_ebs_mirror mirror, void *ctx);
void kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, uint64_t interface);
void kebab_ebs_target_set_guid(kebab_ebs_target *t, const uint8_t guid[16]);
int kebab_ebs_target_valid(const kebab_ebs_target *t);
int kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, const uint8_t guid[16], uint64_t prot_interface);

/* Global lifecycle events: only E1, E2 and E8 are accepted.  E7 is the
 * callback-return counterpart of E3 and goes through the callback entry. */
int kebab_ebs_event_add_global(kebab_ebs_stage stage, uint64_t object,
    uint32_t status);
/* Target events: full identity required. */
int kebab_ebs_event_add_target(kebab_ebs_stage stage, uint64_t handle,
    uint64_t guid_ptr, const uint8_t guid[16], uint64_t prot_interface,
    uint32_t context, uint64_t object, uint32_t status);
/* Callback events: stage must be E3 or E7, and both the notify function and
 * the event object must match what was recorded at registration time. */
int kebab_ebs_event_add_callback(kebab_ebs_stage stage, uint64_t notify,
    uint64_t event, uint32_t status);

#ifdef KEBAB_EBS_TESTING
void kebab_ebs_test_reset(void);
/* Block the commit of one slot until kebab_ebs_test_release_slot(). */
void kebab_ebs_test_block_slot(uint32_t slot);
void kebab_ebs_test_release_slot(void);
#endif

#endif /* KEBAB_EBS_PROBE_H */
