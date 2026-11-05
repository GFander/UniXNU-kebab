/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_CP_NATIVE_API_H
#define KEBAB_A650_CP_NATIVE_API_H
#include <sys/errno.h>
#include "KebabA650GpuOobNative.h"
#if defined(KERNEL)
#include <kern/KebabA650CpCore.h>
#else
#include "KebabA650CpCore.h"
#endif
typedef struct {
 KebabA650CpRecord receipt;
 KGDiscovery vm_before,vm_after;
} KebabA650CpObservation;
static_assert(sizeof(KebabA650CpObservation)==3744,"CP observation3744");
extern "C" int kebab_a650_gpu_cp_prepare(void);
extern "C" int kebab_a650_gpu_cp_snapshot(KebabA650CpObservation*);
#endif
