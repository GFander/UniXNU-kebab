/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_GPU_VM_HOLD_H
#define KEBAB_NATIVE_A650_GPU_VM_HOLD_H
/* Same framebuffer TU after the unchanged InitHold. The cache records one
 * actual attempt; it is neither a native owner token nor a fresh fault poll. */
#include "KebabNativeGmuHfiInitHold.h"
#include "KebabA650GpuVmNative.h"
static uint32_t kebab_gpu_vm_hold_state;
static int kebab_gpu_vm_hold_result=EAGAIN;
static KebabA650GpuVmObservation *kebab_gpu_vm_hold_cache;
extern "C" void kebab_a650_gpu_vm_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gpu_vm_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gpu_vm_hold_result:EBUSY;printf("KEBAB GPUVM: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int e=kebab_gmu_init_hold_worker(NULL);KebabA650GpuVmObservation*r=NULL;
 if(!e){r=(KebabA650GpuVmObservation*)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else{
  memset(r,0,sizeof *r);r->receipt.magic=0x31564741;r->receipt.schema=1;r->receipt.bytes=sizeof r->receipt;r->receipt.phase=KVG_FAILED;
  e=kebab_gmu_init_hold_worker(NULL);
  /* This is only a normal-worker entry prerequisite. The actual native
   * namespace/canonical HFI book/Root/clocks/IRQ/resource tokens gate attach. */
  if(!e&&(__atomic_load_n(&kebab_gmu_init_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_init_hold_result ||
    __atomic_load_n(&kebab_native_gmu_hfi_init_once,__ATOMIC_ACQUIRE)!=2 || kebab_native_gmu_hfi_init_result.io.result))e=95;
  if(!e){e=kebab_a650_gpu_vm_attach();int snapshot=kebab_a650_gpu_vm_snapshot(&r->receipt,&r->before,&r->after);if(!e&&snapshot)e=snapshot;}
  r->receipt.result=(uint32_t)e;
 }}
 kebab_gpu_vm_hold_cache=r;kebab_gpu_vm_hold_result=e;__atomic_store_n(&kebab_gpu_vm_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GPUVM: end; result=%d; phase=%u; cached=0\n",e,r?r->receipt.phase:0);
}
static int kebab_native_a650_gpu_vm_hold_snapshot(KebabA650GpuVmObservation*out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gpu_vm_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gpu_vm_hold_cache)memcpy(out,kebab_gpu_vm_hold_cache,sizeof *out);
 else {memset(out,0,sizeof *out);out->receipt.magic=0x31564741;out->receipt.schema=1;out->receipt.bytes=sizeof out->receipt;out->receipt.phase=KVG_FAILED;out->receipt.result=(uint32_t)kebab_gpu_vm_hold_result;}return 0;
}
#endif
