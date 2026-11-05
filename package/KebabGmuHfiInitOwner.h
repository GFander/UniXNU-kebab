/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_HFI_INIT_OWNER_H
#define KEBAB_GMU_HFI_INIT_OWNER_H
#include "KebabGmuHfiOwner.h"
#include "KebabGmuHfiInitCore.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_gmu_hfi_initialize(const KebabGpuColdToken *,uint64_t,
 const KebabGpuLaunchResourceToken *,const KebabGmuIrqToken *,const KebabGmuBootIo *,const KebabGmuHfiInitIo *,KebabGmuHfiInitRecord *);
#ifdef __cplusplus
}
#endif
#endif
