/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_FIRMWARE_OWNER_H
#define KEBAB_GMU_FIRMWARE_OWNER_H
#include "KebabGmuBootOwner.h"
#include "KebabGpuLaunchResourceOwner.h"
#include "KebabGmuIrqOwner.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_gmu_launch(const KebabGpuColdToken *,uint64_t,
 const KebabGpuLaunchResourceToken *,const KebabGmuIrqToken *,const KebabGmuBootIo *,KebabGmuBootRecord *);
int kebab_gpu_gmu_launch_active_locked(const KebabGpuColdToken *,uint64_t);
int kebab_gpu_gmu_firmware_write_stage_locked(const KebabGpuColdToken *,uint64_t,unsigned stage);
/* Before any resource allocation/write: borrow the existing real IRQ owner,
 * without acquire, copying, extra child refs or weakening its identity API. */
int kebab_rpmh_gpu_gmu_firmware_irq_preflight(const KebabGpuColdToken *,uint64_t,const KebabGmuIrqToken *);
#ifdef __cplusplus
}
#endif
#endif
