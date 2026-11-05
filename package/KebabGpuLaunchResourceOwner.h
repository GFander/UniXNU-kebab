/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_LAUNCH_RESOURCE_OWNER_H
#define KEBAB_GPU_LAUNCH_RESOURCE_OWNER_H
#include "KebabGpuLaunchResourceCore.h"
#include "KebabGpuColdOwner.h"
/* Kernel-private identity. Never serialize/reconstruct it from a CAP, and
 * never use a caller-provided ready bit as an owner/namespace proof. */
typedef struct {
 uint64_t generation,namespace_epoch;uint32_t held,ready_mask,bandwidth_mode,reserved;
} KebabGpuLaunchResourceToken;
#ifdef __cplusplus
static_assert(sizeof(KebabGpuLaunchResourceRecord)==168,"launch resources record v1");
extern "C" {
#endif
int kebab_rpmh_gpu_launch_resources_prepare(const KebabGpuColdToken *,uint64_t,
 KebabGpuLaunchResourceToken *,const KebabGpuLaunchResourceIo *,KebabGpuLaunchResourceRecord *);
int kebab_gpu_launch_resources_verify_locked(const KebabGpuLaunchResourceToken *,const KebabGpuColdToken *,uint64_t,unsigned required_mask);
int kebab_rpmh_gpu_launch_resources_snapshot(KebabGpuLaunchResourceRecord *);
#ifdef __cplusplus
}
#endif
#endif
