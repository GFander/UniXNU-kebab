/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_STAGE_HOLD_H
#define KEBAB_NATIVE_GMU_STAGE_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
#include "KebabGmuStageObservation.h"
extern "C" int get_preemption_level(void);
static uint32_t kebab_gmu_stage_state;
static int kebab_gmu_stage_result=EAGAIN;
static KebabGmuStageObservationRecord *kebab_gmu_stage_record;
/* One attempt including context/allocation failures. There is no automatic
 * release, retry, SMMU mapping, MMIO, clock or firmware execution here. */
extern "C" void kebab_gmu_stage(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gmu_stage_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  int e=expected==2?kebab_gmu_stage_result:EBUSY;
  printf("KEBAB GMUSTAGE: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int result=kebab_gpu_gcc_worker_access(NULL);
 if(!result && (ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()))result=95;
 KebabGmuStageObservationRecord *r=NULL;
 if(!result) {
  r=(KebabGmuStageObservationRecord *)IOMalloc(sizeof *r);
  if(!r)result=ENOMEM;
  else {
   KebabGmuStageObservationInit(r,95);r->state=1;
   result=kebab_a650_gmu_backing_prepare();r->prepare_result=(uint32_t)result;
   if(!result) {
    result=kebab_a650_gmu_backing_stage_current_adt(&r->adt);r->adt_result=(uint32_t)result;
    int copied=kebab_a650_gmu_backing_snapshot(&r->backing);r->backing_snapshot_result=(uint32_t)copied;
    if(!result && copied)result=copied;
    int cache=kebab_a650_gmu_backing_cache_snapshot(&r->cache);r->cache_snapshot_result=(uint32_t)cache;
    if(!result && cache)result=cache;
   }
   r->result=(uint32_t)result;r->state=2;
  }
 }
 kebab_gmu_stage_record=r;kebab_gmu_stage_result=result;
 __atomic_store_n(&kebab_gmu_stage_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUSTAGE: end; result=%d; state=2; cached=0; CPU staging only\n",result);
}
static int kebab_native_gmu_stage_snapshot(KebabGmuStageObservationRecord *out)
{
 if(!out)return EINVAL;
 if(__atomic_load_n(&kebab_gmu_stage_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_stage_record)memcpy(out,kebab_gmu_stage_record,sizeof *out);
 else KebabGmuStageObservationInit(out,(uint32_t)kebab_gmu_stage_result);
 return 0;
}
#endif
