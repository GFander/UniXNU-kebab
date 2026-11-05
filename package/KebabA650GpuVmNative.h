/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_VM_NATIVE_API_H
#define KEBAB_A650_GPU_VM_NATIVE_API_H
#include "KebabA650GpuVmCore.h"
typedef struct {KVGRecord receipt;KGDiscovery before,after;} KebabA650GpuVmObservation;
static_assert(sizeof(KVGRecord)==136,"GPU VM receipt v1");
static_assert(sizeof(KebabA650GpuVmObservation)==3400,"GPU VM observation v1");
extern "C" int kebab_a650_gpu_vm_attach(void);
extern "C" int kebab_a650_gpu_vm_snapshot(KVGRecord*,KGDiscovery*,KGDiscovery*);
#endif
