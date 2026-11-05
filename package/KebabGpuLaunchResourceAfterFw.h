/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_LAUNCH_RESOURCE_AFTER_FW_H
#define KEBAB_GPU_LAUNCH_RESOURCE_AFTER_FW_H
#include "KebabGpuLaunchResourceOwner.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Same canonical busy, real private FW_INIT/HFI_CTRL ACKs and queues-ready
 * phase only. Not a substitute for the strict reset/prelaunch verifier. */
int kebab_gpu_launch_resources_verify_after_fw_locked(
 const KebabGpuLaunchResourceToken *,const KebabGpuColdToken *,uint64_t,unsigned required_mask);
#ifdef __cplusplus
}
#endif
#endif
