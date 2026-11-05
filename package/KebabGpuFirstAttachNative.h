/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_FIRST_ATTACH_NATIVE_API_H
#define KEBAB_GPU_FIRST_ATTACH_NATIVE_API_H
#include "KebabGpuFirstAttachCore.h"
#include "KebabGpuColdOwner.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_gpu_first_attach_cold(void);
/* Namespace permission only; caller must separately own HFI NC/coherency,
 * GMU IRQ/fault monitoring, clocks/reset and firmware execution lifecycle. */
int kebab_gpu_first_allow_run(void);
const KebabGpuColdToken *kebab_gpu_first_run_lease(uint64_t *epoch);
int kebab_gpu_first_ready_locked(const KebabGpuColdToken *,uint64_t epoch);
int kebab_gpu_first_fault(void);
int kebab_gpu_first_stop_restore(void);
int kebab_gpu_first_snapshot(KFReceipt *);
/* Call only at reviewed external mutator sites, before SCM/VRM/VFS locks.
 * INFO/Root-QSEE/separate PAS13/owned CM3 reset yield cookie0/no epoch change.
 * outcome0 means known unchanged; outcome1 changed, outcome2 uncertain. */
int kebab_gpu_namespace_change_begin(unsigned operation,uint64_t *cookie);
int kebab_gpu_namespace_change_end(uint64_t cookie,unsigned outcome);
#ifdef __cplusplus
}
#endif
#endif
