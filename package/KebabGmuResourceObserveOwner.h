/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_RESOURCE_OBSERVE_OWNER_H
#define KEBAB_GMU_RESOURCE_OBSERVE_OWNER_H
#include "KebabGmuHfiInitOwner.h"
#include "KebabGmuResourceObserve.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_gmu_resource_observe(const KebabGpuColdToken *,uint64_t,
 const KebabGpuLaunchResourceToken *,const KebabGmuIrqToken *,
 const KebabGmuBootIo *,const KebabGmuResourceObserveIo *,KebabGmuResourceObserveRecord *);
#ifdef __cplusplus
}
#endif
#endif
