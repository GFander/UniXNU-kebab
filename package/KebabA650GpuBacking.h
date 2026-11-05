/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_BACKING_H
#define KEBAB_A650_GPU_BACKING_H
#include <stdint.h>
#include <stddef.h>
enum {KAGPU_EMPTY=0,KAGPU_PREPARED=1,KAGPU_FAILED=2,KAGPU_RELEASED=3};
/* Kernel-only, pointer-identified token. Never accept a CAP/user reconstruction.
 * Publication permanently retains these allocations for this initial backend;
 * neither a job count nor a copied observation permits releasing DMA memory. */
typedef struct {uint64_t generation;uint32_t slot,held;} KebabA650GpuBackingToken;
typedef struct {uint32_t iova,bytes,permissions,allocation_bytes;uint64_t physical;uintptr_t descriptor_identity,cpu;} KebabA650GpuBackingRegion;
typedef struct {
 uint64_t generation,root_physical;uintptr_t table_descriptor_identity;
 uint32_t table_bytes,region_count,mapped_pages,cleaned_for_device;
 KebabA650GpuBackingRegion region[6];
} KebabA650GpuBackingDescription;
typedef struct {
 uint32_t magic,schema,bytes,result,phase,pools,cpu_bytes,mapped_pages;
 uint32_t cpu_page_bytes,gpu_page_bytes,table_bytes,cleaned_for_device;
 uint32_t NC_native_pages,hardware_touched,GPU_DMA_proven,SQE_loaded;
 uint32_t primary_error,cleanup_error,published,holders;
 uint32_t region_iova[6],region_bytes[6],region_allocation_bytes[6],region_permissions[6];
} KebabA650GpuBackingRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabA650GpuBackingRecord)==176,"GPU backing diagnostic ABI");
extern "C" {
#endif
int kebab_a650_gpu_backing_prepare(void);
int kebab_a650_gpu_backing_snapshot(KebabA650GpuBackingRecord *);
int kebab_a650_gpu_backing_acquire(KebabA650GpuBackingToken *,KebabA650GpuBackingDescription *);
int kebab_a650_gpu_backing_check(const KebabA650GpuBackingToken *);
int kebab_a650_gpu_backing_publish(KebabA650GpuBackingToken *);
int kebab_a650_gpu_backing_end(KebabA650GpuBackingToken *);
int kebab_a650_gpu_backing_release_unpublished(void);
#ifdef __cplusplus
}
#endif
#endif
