/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_OOB_NATIVE_API_H
#define KEBAB_A650_GPU_OOB_NATIVE_API_H
#include <sys/errno.h>
#include "KebabA650GpuVmNative.h"
#if defined(KERNEL)
#include <kern/KebabA650GpuOobCore.h>
#else
#include "KebabA650GpuOobCore.h"
#endif
typedef struct {
 KebabA650GpuOobRecord receipt;
 KGDiscovery vm_before,vm_after;
} KebabA650GpuOobObservation;
static_assert(sizeof(KebabA650GpuOobObservation)==3424,"GPU OOB observation v1");
extern "C" int kebab_a650_gpu_oob_prepare(void);
extern "C" int kebab_a650_gpu_oob_snapshot(KebabA650GpuOobObservation*);
#endif
