/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_GPU_OOB_HOLD_H
#define KEBAB_NATIVE_A650_GPU_OOB_HOLD_H
#include "KebabA650GpuOobNative.h"
static uint32_t kebab_gpu_oob_hold_state;
static int kebab_gpu_oob_hold_result=EAGAIN;
extern "C" void kebab_a650_gpu_oob_hold_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gpu_oob_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  printf("KEBAB GPUOOB: end; result=%d; cached=1\n",expected==2?kebab_gpu_oob_hold_result:EBUSY);return;
 }
 int e=kebab_a650_gpu_oob_prepare();kebab_gpu_oob_hold_result=e;
 __atomic_store_n(&kebab_gpu_oob_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GPUOOB: end; result=%d; cached=0\n",e);
}
#endif
