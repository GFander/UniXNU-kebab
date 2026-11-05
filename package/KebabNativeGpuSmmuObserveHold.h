/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_SMMU_OBSERVE_HOLD_H
#define KEBAB_NATIVE_GPU_SMMU_OBSERVE_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
#include "KebabGpuSmmuObservation.h"
extern "C" int get_preemption_level(void);
static uint32_t kebab_gpu_smmu_observe_state;
static int kebab_gpu_smmu_observe_result=EAGAIN;
static KebabGpuSmmuObservationRecord *kebab_gpu_smmu_observe_record;
static int kebab_gpu_smmu_observe_worker_check(void)
{
 int e=kebab_gpu_gcc_worker_access(NULL);if(e)return e;
 if(ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return 0;
}
/* Only GPUSMMU calls this. One attempt per boot, including refused/context/
 * allocation failures. No domain prepare/bind/PAS/ATS/reset entry is exposed. */
extern "C" void kebab_gpu_smmu_observe(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gpu_smmu_observe_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  int e=expected==2?kebab_gpu_smmu_observe_result:EBUSY;
  printf("KEBAB GPUSMMU: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int result=kebab_gpu_smmu_observe_worker_check();KebabGpuSmmuObservationRecord *r=NULL;
 if(!result) {
  r=(KebabGpuSmmuObservationRecord *)IOMalloc(sizeof *r);
  if(!r)result=ENOMEM;
  else {
   KebabGpuSmmuObservationInit(r,95);r->state=1;
   result=kebab_gpu_smmu_candidate_observe();
   int copied=kebab_gpu_smmu_candidate_observation_copy(r);r->snapshot_result=(uint32_t)copied;
   if(!result && copied)result=copied;r->result=(uint32_t)result;r->state=2;
  }
 }
 kebab_gpu_smmu_observe_record=r;kebab_gpu_smmu_observe_result=result;
 __atomic_store_n(&kebab_gpu_smmu_observe_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GPUSMMU: end; result=%d; state=2; cached=0; finite NS observation only\n",result);
}
/* CAP23 copy success is separate from record.result. Before completion do
 * not touch out; failures before allocation are reconstructed without IO. */
static int kebab_native_gpu_smmu_observe_snapshot(KebabGpuSmmuObservationRecord *out)
{
 if(!out)return EINVAL;
 if(__atomic_load_n(&kebab_gpu_smmu_observe_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gpu_smmu_observe_record)memcpy(out,kebab_gpu_smmu_observe_record,sizeof *out);
 else KebabGpuSmmuObservationInit(out,(uint32_t)kebab_gpu_smmu_observe_result);
 return 0;
}
#endif
