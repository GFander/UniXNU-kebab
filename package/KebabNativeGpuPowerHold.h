/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_POWER_HOLD_H
#define KEBAB_NATIVE_GPU_POWER_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
#include "KebabNativeGpuCxMx.h"
/* Include once in the normal framebuffer diagnostic TU, after the existing
 * kebab_gpu_gcc_worker_access definition. No user-supplied action/binding.
 * The one acquired reference, partial/quarantined reference and receipt
 * remain owned for this boot. There is deliberately no release/reset API. */
extern "C" int get_preemption_level(void);
static_assert(sizeof(KebabGpuCxMxRecord)==11100,"fixed GPU power hold receipt");
enum {KebabGpuPowerHoldNotRun=0,KebabGpuPowerHoldBusy=1,KebabGpuPowerHoldDone=2};
static uint32_t kebab_gpu_power_hold_state;
static int kebab_gpu_power_hold_result=EAGAIN;
static KebabGpuCxMxRecord *kebab_gpu_power_hold_record;

static int kebab_gpu_power_worker_check(void *cookie)
{
 int error=kebab_gpu_gcc_worker_access(cookie);
 if(error)return error;
 if(ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return 0;
}

/* GPUPOWER only: claiming even a refused/ENOMEM attempt prevents retries.
 * Only pointers/scalars are on the kernel stack, never the 11100B record. */
extern "C" void kebab_gpu_power_prepare(void)
{
 uint32_t expected=KebabGpuPowerHoldNotRun;
 if(!__atomic_compare_exchange_n(&kebab_gpu_power_hold_state,&expected,
    KebabGpuPowerHoldBusy,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  int result=expected==KebabGpuPowerHoldDone?kebab_gpu_power_hold_result:EBUSY;
  printf("KEBAB GPUPOWER: end; result=%d; state=%u; cached=1\n",result,expected);
  return;
 }
 int result=kebab_gpu_power_worker_check(NULL);
 KebabGpuCxMxRecord *record=NULL;
 if(!result) {
  record=(KebabGpuCxMxRecord *)IOMalloc(sizeof(*record));
  if(!record)result=ENOMEM;
  else {
   KebabGpuCxMxInitRecord(record);
   result=kebab_native_gpu_cxmx(KebabGpuCxMxAcquire,NULL,kebab_gpu_power_worker_check,record);
  }
 }
 /* Publish only after native Acquire has returned. Never free on error:
  * a partial ACK/quarantine may still own a reference or uncertain vote. */
 kebab_gpu_power_hold_record=record;
 kebab_gpu_power_hold_result=result;
 __atomic_store_n(&kebab_gpu_power_hold_state,KebabGpuPowerHoldDone,__ATOMIC_RELEASE);
 printf("KEBAB GPUPOWER: end; result=%d; state=%u; cached=0\n",result,(unsigned)KebabGpuPowerHoldDone);
}

/* CAP18 copy only. Return 0 means a complete immutable receipt was copied,
 * including native failures; out->result is never converted to success.
 * Before completion return EAGAIN without touching out. If context/heap
 * failed before any native call, reconstruct the fixed zero/error receipt
 * from the immutable scalar terminal result, without allocating or IO. */
static int kebab_native_gpu_power_hold_snapshot(KebabGpuCxMxRecord *out)
{
 if(!out)return EINVAL;
 if(__atomic_load_n(&kebab_gpu_power_hold_state,__ATOMIC_ACQUIRE)!=KebabGpuPowerHoldDone)return EAGAIN;
 if(kebab_gpu_power_hold_record)memcpy(out,kebab_gpu_power_hold_record,sizeof(*out));
 else {KebabGpuCxMxInitRecord(out);out->result=(uint32_t)kebab_gpu_power_hold_result;}
 return 0;
}
#endif
