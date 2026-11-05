/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_CP_OWNER_H
#define KEBAB_A650_CP_OWNER_H
#include "KebabA650GpuOobOwner.h"
#include "KebabA650CpCore.h"
typedef struct {
 const KebabA650GpuOobOwnerToken *oob;
 uint64_t generation;
 uint32_t pinned,quarantined;
} KebabA650CpOwnerToken;
typedef struct {
 KebabA650CpIo core;
 int (*snapshot_vm)(void*,unsigned after);
} KebabA650CpOwnerIo;
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_cp_first_execution(KebabA650CpOwnerToken*,const KebabA650GpuOobOwnerToken*,const KebabA650CpOwnerIo*,KebabA650CpRecord*);
int kebab_rpmh_gpu_cp_check_locked(const KebabA650CpOwnerToken*);
int kebab_a650_gpu_cp_context_ready_locked(const KebabA650GpuOobOwnerToken*);
int kebab_a650_gpu_cp_power_ready_locked(const KebabA650GpuOobOwnerToken*,uint32_t phase);
void kebab_a650_gpu_cp_quarantine_locked(const KebabA650GpuOobOwnerToken*);
#ifdef __cplusplus
}
#endif
#endif
