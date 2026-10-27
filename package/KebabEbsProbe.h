/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Kebab EBS probe record (E4b-F revision).
 *
 * Fixed, pre-allocated record written without allocation or UEFI services.
 * Three properties are enforced here rather than left to callers:
 *
 *   - identity: only events matching the recorded handle/GUID/interface are
 *     written, and the check lives inside the event entry point;
 *   - reentry: a slot is claimed with an atomic fetch_add, so concurrent
 *     writers never share an index, and a full record sets a loss flag;
 *   - commit: last_attempted_stage is set before the write and
 *     last_committed_stage only after the slot is published, so a hang tells
 *     "entered" apart from "committed".
 */
#ifndef KEBAB_EBS_PROBE_H
#define KEBAB_EBS_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* Bytes K R S C in memory (little endian), not the old 0x4b525343. */
#define KEBAB_EBS_MAGIC   0x4353524bu
#define KEBAB_EBS_VERSION 2u
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
	uint32_t seq;              /* 1-based; 0 means the slot is not committed */
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
	uint32_t claimed;          /* slots claimed, may include unfinished */
	uint32_t committed;        /* slots fully published */
	uint32_t lost;             /* record full or identity rejected */
	uint32_t last_attempted_stage;
	uint32_t last_committed_stage;
	kebab_ebs_event ev[KEBAB_EBS_EVENTS];
} kebab_ebs_record;

typedef struct {
	uint64_t handle;
	uint64_t guid_ptr;
	uint64_t guid_lo;
	uint64_t guid_hi;
	uint64_t interface;
	uint32_t valid;
} kebab_ebs_target;

typedef void (*kebab_ebs_mirror)(uint32_t stage, void *ctx);

/* One record and one mirror for the boot; re-init after EBS is refused. */
void kebab_ebs_init(kebab_ebs_record *rec, kebab_ebs_target *target,
    kebab_ebs_mirror mirror, void *ctx);
int kebab_ebs_sealed(void);

#ifdef KEBAB_EBS_TESTING
/* Host tests only; never linked into firmware. */
void kebab_ebs_test_reset(void);
#endif

void kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, uint64_t guid_lo, uint64_t guid_hi, uint64_t interface);
int kebab_ebs_target_valid(const kebab_ebs_target *t);
int kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid_ptr, uint64_t prot_interface);

/* Returns 1 when the event was committed, 0 when it was refused (identity,
 * uninitialised record) or lost to a full record. */
int kebab_ebs_event_add(kebab_ebs_stage stage, uint64_t handle,
    uint64_t guid_ptr, uint64_t prot_interface, uint32_t context,
    uint64_t object, uint32_t status);

#endif /* KEBAB_EBS_PROBE_H */
