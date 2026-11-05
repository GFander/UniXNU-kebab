/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_BUS_OWNER_H
#define KEBAB_GPU_BUS_OWNER_H
#include "KebabGpuBusCore.h"
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_bus(unsigned,const KebabGpuBusIo *,KebabGpuBusRecord *);
int kebab_rpmh_gpu_bus_gate(void);
int kebab_rpmh_gpu_bus_acquire(KebabGpuBusToken *);
int kebab_rpmh_gpu_bus_check(KebabGpuBusToken *);
int kebab_rpmh_gpu_bus_end(KebabGpuBusToken *);
#ifdef __cplusplus
}
#endif
#endif
