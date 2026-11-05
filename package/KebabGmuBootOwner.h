/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_BOOT_OWNER_H
#define KEBAB_GMU_BOOT_OWNER_H
#include "KebabGmuBootCore.h"
#include "KebabA650GmuTcm.h"
#include "KebabGpuColdOwner.h"
enum {KebabGmuBootInspect=0,KebabGmuBootResetLoad=1,KebabGmuBootLaunch=2};
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_gmu_boot(unsigned action,const KebabGpuColdToken *,uint64_t epoch,
 const KebabA650GmuTcmManifest *,const KebabGmuBootIo *,KebabGmuBootRecord *);
int kebab_gpu_gmu_boot_gate_locked(const KebabGpuColdToken *,uint64_t);
int kebab_gpu_gmu_boot_preflight_locked(const KebabGpuColdToken *,uint64_t);
int kebab_gpu_gmu_boot_reset_held_locked(const KebabGpuColdToken *,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
