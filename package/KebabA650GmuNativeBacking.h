/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_NATIVE_BACKING_API_H
#define KEBAB_A650_GMU_NATIVE_BACKING_API_H
#include <stdint.h>
#include <stddef.h>
#include "KebabA650GmuBackingTypes.h"
/* CPU-only stages; no state here is a live SMMU/power/reset lease. */
enum { KAGB_EMPTY=0,KAGB_PREPARED=1,KAGB_STAGED=2,KAGB_FAILED=3,KAGB_RELEASED=4 };
typedef struct {
 uint32_t magic,schema,bytes,result,phase,cleaned_for_device,buffers_retained,cleaned_pools;
 uint32_t cpu_allocated_bytes,mapped_pages,hardware_touched,firmware_started,primary_error,cleanup_error;
 KebabA650GmuBackingRecord backing;
} KebabA650GmuNativeBackingRecord;
/* Kernel-private handles. Never serialize these pointers/PAs into CAP or
 * accept a reconstructed token from a user/CAP record. No CPU RAM accessor. */
typedef struct {uint64_t generation;uint32_t slot,held;} KebabA650GmuBackingToken;
typedef struct {
 uint32_t iova,bytes,permissions,allocation_bytes;uint64_t physical;uintptr_t descriptor_identity;
} KebabA650GmuBackingRegion;
typedef struct {
 uint64_t generation,root_physical;uintptr_t table_descriptor_identity;
 uint32_t table_bytes,region_count,mapped_pages,cleaned_for_device;
 KebabA650GmuBackingRegion region[6];
} KebabA650GmuBackingDescription;
#ifdef __cplusplus
static_assert(sizeof(KebabA650GmuNativeBackingRecord)==128,"A650 native backing record v1");
extern "C" {
#endif
int kebab_a650_gmu_backing_prepare(void);
int kebab_a650_gmu_backing_stage(const void *image,size_t bytes);
int kebab_a650_gmu_backing_snapshot(KebabA650GmuNativeBackingRecord *out);
int kebab_a650_gmu_backing_release_unpublished(void);
int kebab_a650_gmu_backing_acquire(KebabA650GmuBackingToken *,KebabA650GmuBackingDescription *);
int kebab_a650_gmu_backing_check(const KebabA650GmuBackingToken *);
/* Trusted fixed-count RO snapshot, only with the actual published token. */
int kebab_a650_gmu_backing_owned_ranges(const KebabA650GmuBackingToken *,uint64_t (*)[2],uint32_t);
/* Call before the first hardware publication. This first version pins for
 * the boot lifetime; no generic boolean/callback "drained" can undo it. */
int kebab_a650_gmu_backing_publish(KebabA650GmuBackingToken *);
int kebab_a650_gmu_backing_end(KebabA650GmuBackingToken *);
#ifdef __cplusplus
}
#endif
#endif
