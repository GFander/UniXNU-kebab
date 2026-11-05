/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_FIRST_ATTACH_HOLD_H
#define KEBAB_NATIVE_GPU_FIRST_ATTACH_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
#include "KebabGpuFirstAttachObservation.h"
extern "C" int get_preemption_level(void);
static uint32_t kebab_gpu_first_hold_state;
static int kebab_gpu_first_hold_result=EAGAIN;
static KebabGpuFirstAttachObservationRecord *kebab_gpu_first_hold_record;
/* GMUMAP 7B / UD54. Include once after existing normal GCC worker gate. */
extern "C" void kebab_gpu_first_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gpu_first_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gpu_first_hold_result:EBUSY;
  printf("KEBAB GMUMAP: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int e=kebab_gpu_gcc_worker_access(NULL);KebabGpuFirstAttachObservationRecord *r=NULL;
 if(!e && (ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()))e=95;
 if(!e){r=(KebabGpuFirstAttachObservationRecord *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;
  else {KebabGpuFirstAttachObservationInit(r,95);r->state=1;e=kebab_gpu_first_attach_cold();
   int copy=kebab_gpu_first_observation_copy(r);r->snapshot_result=copy;if(!e && copy)e=copy;r->result=e;r->state=2;}
 }
 kebab_gpu_first_hold_record=r;kebab_gpu_first_hold_result=e;__atomic_store_n(&kebab_gpu_first_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUMAP: end; result=%d; state=2; cached=0; first attach only, producer held\n",e);
}
/* CAP25 copies one immutable attempt, including failure; no live check/IO. */
static int kebab_native_gpu_first_hold_snapshot(KebabGpuFirstAttachObservationRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gpu_first_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gpu_first_hold_record)memcpy(out,kebab_gpu_first_hold_record,sizeof *out);
 else KebabGpuFirstAttachObservationInit(out,(uint32_t)kebab_gpu_first_hold_result);return 0;
}
#endif
