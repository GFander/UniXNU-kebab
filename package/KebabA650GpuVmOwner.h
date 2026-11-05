/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_VM_OWNER_H
#define KEBAB_A650_GPU_VM_OWNER_H
#include "KebabGmuBootOwner.h"
typedef struct {const KebabGpuColdToken *cold;uint64_t epoch,generation;uint32_t held,published;} KebabA650GpuVmOwnerToken;
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_vm_acquire(KebabA650GpuVmOwnerToken*,const KebabGpuColdToken*,uint64_t,const KebabGmuBootIo*);
int kebab_rpmh_gpu_vm_check(const KebabA650GpuVmOwnerToken*);
int kebab_rpmh_gpu_vm_publish(KebabA650GpuVmOwnerToken*);
int kebab_rpmh_gpu_vm_end(KebabA650GpuVmOwnerToken*);
/* Exact native object + same private namespace, called only under canonical
 * VRM busy. Pure comparisons: no recursive VRM lock, mapper or MMIO. */
int kebab_a650_gpu_vm_ready_locked(const KebabA650GpuVmOwnerToken*);
#ifdef __cplusplus
}
#endif
#endif
