/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_COLD_OWNER_H
#define KEBAB_GPU_COLD_OWNER_H
#include "KebabGpuBusOwner.h"
/* Owned by first-attach TU; bus token keeps exact pointer identity. */
typedef struct {KebabGpuBusToken bus;uint64_t generation;uint32_t held,started;} KebabGpuColdToken;
typedef struct {void *cookie;int (*read32)(void *,uint32_t,uint32_t *);} KebabGpuColdIo;
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_cold_acquire(KebabGpuColdToken *,const KebabGpuColdIo *);
int kebab_rpmh_gpu_cold_check(KebabGpuColdToken *,unsigned flags);
int kebab_rpmh_gpu_cold_drain(KebabGpuColdToken *);
int kebab_rpmh_gpu_cold_end(KebabGpuColdToken *);
/* Called by the future GMU clock owner inside canonical globalbusy, after its
 * own finite clock/reset/IRQ requirements and namespace private ready check. */
int kebab_gpu_cold_token_locked(const KebabGpuColdToken *,unsigned cold);
int kebab_gpu_cold_mark_started_locked(KebabGpuColdToken *);
#ifdef __cplusplus
}
#endif
#endif
