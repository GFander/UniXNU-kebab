/* SPDX-License-Identifier: MIT */
/* Include once inside the SAME canonical VRM translation unit, after its
 * existing static global lock. Never expose a caller-supplied lock. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuCxMxOwner.h>
#else
#include "KebabGpuCxMxOwner.h"
#endif
static KebabGpuCxMxLedger kebab_gpu_cxmx_ledger;
/* Defined after the CC ledger in this same canonical TU. Caller already
 * holds globalbusy; totalCCchildren each retain one real CX/MX reference. */
static uint64_t kebab_gpu_pinned_rail_floor(void);
static KebabGpuCxMxRecord kebab_gpu_cxmx_last_failure;
static uint32_t kebab_gpu_cxmx_has_failure;
int kebab_rpmh_gpu_cxmx(unsigned action,const KebabGpuCxMxIo *io,const KebabGpuCxMxBinding *binding,KebabGpuCxMxRecord *out)
{
 uint32_t before,dead,used_mask;int result=22;
 if(!out)return 22;KebabGpuCxMxInitRecord(out);
 if(action>2)return 22;
 if(action==KebabGpuCxMxAcquire && !KebabGpuCxMxBound(binding)){out->result=95;out->gate_reason=1;return 95;}
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)!=0){out->result=16;return 16;}
 dead=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 used_mask=__atomic_load_n(&kebab_rpmh_global_lock.used_mask,__ATOMIC_ACQUIRE);before=kebab_gpu_cxmx_ledger.refs;
 if((dead || kebab_gpu_cxmx_ledger.quarantined) && kebab_gpu_cxmx_has_failure) {*out=kebab_gpu_cxmx_last_failure;out->evidence_replayed=1;}
 if(action==KebabGpuCxMxInspect)result=0;
 else if(dead || kebab_gpu_cxmx_ledger.quarantined)result=5;
 else if(action==KebabGpuCxMxRelease) {
  if(!kebab_gpu_cxmx_ledger.complete || !before)result=22;
  else if((uint64_t)before<=kebab_gpu_pinned_rail_floor())result=16;
  else {--kebab_gpu_cxmx_ledger.refs;result=0;}
 } else if(kebab_gpu_cxmx_ledger.complete) {
  if(before==UINT32_MAX)result=75;else {++kebab_gpu_cxmx_ledger.refs;result=0;}
 } else {
  result=KebabGpuCxMxRun(io,binding,(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.used,__ATOMIC_ACQUIRE),used_mask,&kebab_gpu_cxmx_ledger,out);
 }
 if(kebab_gpu_cxmx_ledger.quarantined)__atomic_store_n(&kebab_rpmh_global_lock.dead,1,__ATOMIC_RELEASE);
 out->same_native_owner=1;out->owner_dead=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);out->owner_used_mask=used_mask;
 out->refs_before=before;out->refs_after=kebab_gpu_cxmx_ledger.refs;out->ack_mask=kebab_gpu_cxmx_ledger.ack_mask;out->uncertain_mask=kebab_gpu_cxmx_ledger.uncertain_mask;
 out->quarantined=kebab_gpu_cxmx_ledger.quarantined;out->lease_mask=!out->owner_dead && !out->quarantined && kebab_gpu_cxmx_ledger.complete && kebab_gpu_cxmx_ledger.refs?3:0;out->result=(uint32_t)result;
 if(out->quarantined && !out->evidence_replayed) {kebab_gpu_cxmx_last_failure=*out;kebab_gpu_cxmx_has_failure=1;}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return result;
}
