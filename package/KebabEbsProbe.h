/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Kebab EBS probe: a fixed, pre-allocated record of the ExitBootServices
 * stages, written without allocation and without UEFI services.
 *
 * The record is published once (configuration table / ADT) and only written
 * afterwards.  Identity matters more than the stage code: a pool free in some
 * other driver must never be attributed to the RpmhDxe uninstall, so each
 * event carries a context id and the object pointer, and the match helper
 * compares the interface pointer rather than the internal PROTOCOL_INTERFACE.
 */
#ifndef KEBAB_EBS_PROBE_H
#define KEBAB_EBS_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define KEBAB_EBS_MAGIC 0x4b525343u     /* "KRSC" */
#define KEBAB_EBS_EVENTS 64u

typedef enum {
	KEBAB_EBS_E1 = 1,   /* CoreTerminateMemoryMap returned */
	KEBAB_EBS_E2,       /* EBS notification starts */
	KEBAB_EBS_E3,       /* target callback about to run */
	KEBAB_EBS_E4,       /* CoreUninstallProtocolInterface entered */
	KEBAB_EBS_E4A,      /* CoreFindProtocolInterface returned */
	KEBAB_EBS_E4B,      /* disconnect before/after */
	KEBAB_EBS_E4C,      /* CoreFreePool(OpenData) */
	KEBAB_EBS_E5,       /* CoreFreePool(Prot) */
	KEBAB_EBS_E6,       /* CoreFreePool(Prot) returned */
	KEBAB_EBS_E7,       /* target callback returned */
	KEBAB_EBS_E8,       /* DarwinBoot saw EBS success */
} kebab_ebs_stage;

typedef struct {
	uint32_t seq;
	uint32_t stage;
	uint32_t context;
	uint32_t status;
	uint64_t object;
} kebab_ebs_event;

typedef struct {
	uint32_t        magic;
	uint32_t        overflow;
	uint32_t        count;
	uint32_t        last_stage;      /* for the framebuffer path */
	kebab_ebs_event ev[KEBAB_EBS_EVENTS];
} kebab_ebs_record;

/* Optional framebuffer mirror; set once, before EBS, and never called with
 * UEFI services from inside the record path. */
typedef void (*kebab_ebs_mirror)(uint32_t stage, void *ctx);

void kebab_ebs_init(kebab_ebs_record *rec, kebab_ebs_mirror mirror, void *ctx);
void kebab_ebs_event_add(kebab_ebs_record *rec, kebab_ebs_stage stage,
    uint32_t context, uint64_t object, uint32_t status);

/* The uninstall the callback performs.  All four fields must match for an
 * event to be attributed to this probe. */
typedef struct {
	uint64_t handle;
	uint64_t guid;
	uint64_t interface;
} kebab_ebs_target;

void kebab_ebs_target_set(kebab_ebs_target *t, uint64_t handle,
    uint64_t guid, uint64_t interface);
int kebab_ebs_target_matches(const kebab_ebs_target *t, uint64_t handle,
    uint64_t guid, uint64_t prot_interface);

#endif /* KEBAB_EBS_PROBE_H */
