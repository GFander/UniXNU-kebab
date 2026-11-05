/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_HFI_OWNER_H
#define KEBAB_GMU_HFI_OWNER_H
#include "KebabGmuFirmwareOwner.h"
#include "KebabGpuLaunchResourceAfterFw.h"
#include "KebabGmuHfiCore.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_gmu_hfi_version(const KebabGpuColdToken *,uint64_t,
 const KebabGpuLaunchResourceToken *,const KebabGmuIrqToken *,const KebabGmuBootIo *,const KebabGmuHfiIo *,KebabGmuHfiRecord *);
int kebab_gpu_gmu_hfi_gate_locked(const KebabGpuColdToken *,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
