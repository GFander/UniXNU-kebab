/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_OOB_OWNER_H
#define KEBAB_A650_GPU_OOB_OWNER_H
#include "KebabA650GpuVmOwner.h"
#include "KebabA650GpuOobCore.h"
typedef struct {
 const KebabA650GpuVmOwnerToken *vm;
 uint64_t generation;
 uint32_t pinned,quarantined;
} KebabA650GpuOobOwnerToken;
typedef struct {
 KebabA650GpuOobIo core;
 /* Actual retained namespace register observations, never CAP caches. */
 int (*snapshot_vm)(void*,unsigned after);
} KebabA650GpuOobOwnerIo;
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_oob_hold(KebabA650GpuOobOwnerToken*,const KebabA650GpuVmOwnerToken*,const KebabA650GpuOobOwnerIo*,KebabA650GpuOobRecord*);
int kebab_rpmh_gpu_oob_check_locked(const KebabA650GpuOobOwnerToken*);
/* FirstAttach TU: exact existing private namespace/BO identity only, then
 * fresh owned route/context + all ordinary fault CSRs without another lock. */
int kebab_a650_gpu_oob_vm_ready_locked(const KebabA650GpuVmOwnerToken*);
int kebab_a650_gpu_oob_vm_live_check_locked(const KebabA650GpuVmOwnerToken*);
void kebab_a650_gpu_oob_vm_quarantine_locked(const KebabA650GpuVmOwnerToken*);
#ifdef __cplusplus
}
#endif
#endif
