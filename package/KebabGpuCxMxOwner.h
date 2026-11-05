/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_CXMX_OWNER_H
#define KEBAB_GPU_CXMX_OWNER_H
#include "KebabGpuCxMxReassert.h"
enum {KebabGpuCxMxRelease=0,KebabGpuCxMxAcquire=1,KebabGpuCxMxInspect=2};
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_gpu_cxmx(unsigned,const KebabGpuCxMxIo *,const KebabGpuCxMxBinding *,KebabGpuCxMxRecord *);
#ifdef __cplusplus
}
#endif
#endif
