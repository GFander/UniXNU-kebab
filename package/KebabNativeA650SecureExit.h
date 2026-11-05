/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_SECURE_EXIT_H
#define KEBAB_NATIVE_A650_SECURE_EXIT_H
#include "KebabA650SecureExitCore.h"
/* Same actual namespace/CP owner as the existing render implementation.
 * No SMC, protected-low-memory access, TRUST write, reset or interrupt change. */
struct KZEEvidence {
 KZERecord record;
 uint32_t nc_verified,kicks,quarantined,snapshot_before_error,snapshot_after_error,reserved;
 uint32_t guard_before[16],guard_after[16];
 KGDiscovery vm_before,vm_after;
};
static_assert(KAR_RING_START==KZE_END,"render follows real secure-exit cursor");
static_assert(KZE_OFFSET>=KAR_DIAGNOSTIC_OFFSET+KAR_DIAGNOSTIC_SPACING+KAR_DIAGNOSTIC_COUNT*4,"secure completion separate from render diagnostic slots");
static_assert(KZE_OFFSET+64<=4096,"whole completion and guard inside existing NC fenceBO");
static_assert(sizeof(KZEEvidence)<=4072,"CAP46 bounded evidence");
static_assert(sizeof(kebab_secure_exit_observation)==4096,"CAP46 fixed4KiB");
static uint32_t kze_state;
static KZEEvidence *kze_evidence;
extern "C" int kebab_rpmh_gpu_cp_secure_exit(KebabA650CpOwnerToken *,uint32_t *);

static int KZEAuthReady(void)
{
 if(__atomic_load_n(&kza2_state,__ATOMIC_ACQUIRE)!=2 || !kza2_evidence || !kf_pas13_frozen)return 95;
 const auto *r=&kza2_evidence->receipt;
 if(r->result || r->primary_error || r->end_error || r->phase!=KZA2_DONE ||
  !r->completed || !r->authenticated || !r->prepared || !r->cp_ready || !r->final_checked ||
  !r->metadata_frozen || !r->image_frozen || !kza2_evidence->parent_pinned ||
  !kza2_evidence->namespace_released || kza2_evidence->quarantine_guard ||
  r->after.epoch!=1 || r->after.gmu_generation!=kf_native.backing.generation ||
  r->after.gpu_generation!=kvg_native.bo.generation || r->after.cp_generation!=kcp_native.owner.generation)return 95;
 for(unsigned i=0;i<3;i++)if(!r->operation[i].attempted || r->operation[i].guard ||
  !KebabPas13OperationSucceeded(&r->operation[i].raw))return 95;
 return 0;
}
static int KZANativeSecureExitReady(void)
{
 if(__atomic_load_n(&kze_state,__ATOMIC_ACQUIRE)!=2 || !kze_evidence || KZEAuthReady())return 95;
 const auto *e=kze_evidence;
 return e->record.result || e->record.primary_error || !e->record.completed ||
  e->record.phase!=KZE_COMPLETE || !e->record.touched || !e->record.prepared || !e->record.submitted ||
  e->quarantined || e->kicks!=1 || !e->nc_verified || e->snapshot_before_error || e->snapshot_after_error ||
  e->record.parent.cp_generation!=kcp_native.owner.generation?95:0;
}
static int kze_gate(void *)
{return panic_active() || KFNativeWorker(NULL) || !kze_evidence || KZEAuthReady()?95:KCPNativeGate(&kcp_native);}
static int kze_authority(void *,KZEAuthProof *out)
{
 int error=kze_gate(NULL);if(error)return error;
 if(out!=&kze_evidence->record.parent || __atomic_load_n(&kcp_native_once,__ATOMIC_ACQUIRE)!=2 ||
  kcp_native_result || !kcp_native.observation || !kcp_native.observation->receipt.NC_verified)return 95;
 const auto *p=&kza2_evidence->receipt.after;
 *out={p->epoch,p->gmu_generation,p->gpu_generation,p->cp_generation,1,1,1,1};return 0;
}
static int kze_sample(void *,KZESample *out)
{
 int error=kze_gate(NULL);if(error)return error;
 KARSample s={};error=KARSampleHardware(&s,0);
 const auto *f=(volatile const uint32_t *)kvg_engine.backing.region[5].cpu;
 KARAcquireNC();const uint32_t completion=f[KZE_OFFSET/4];
 *out={s.wptr,s.rptr,s.shadow,s.fence,completion,s.marker,s.status,s.rbbm_irq,s.irq_mask,
  s.cp_fault,s.cp_irq,s.cp_protect,s.ahb_fence,s.gfsr,s.fsr,s.sqe};
 return error;
}
static uint32_t kze_guard_word(unsigned i)
{return !i?KZE_SENTINEL:i==1?0:0xa650fe00u+i;}
static int kze_prepare(void *,const uint32_t *words,uint32_t count)
{
 int error=kze_gate(NULL);if(error)return error;
 uint32_t expected[KZE_WORDS];KZEWords(expected);
 auto *e=kze_evidence;
 if(count!=KZE_WORDS || !words || memcmp(words,expected,sizeof expected) ||
  e->record.phase!=KZE_PREPARING || !e->record.touched || e->record.prepared || e->kicks)return 95;
 const auto &ring=kvg_engine.backing.region[0],&completion=kvg_engine.backing.region[5];
 if(ring.iova!=KCP_RING_IOVA || ring.bytes!=4096 || !ring.cpu ||
  completion.iova!=KCP_FENCE_IOVA || completion.bytes!=4096 || !completion.cpu)return 95;
 for(uint32_t off=0;off<completion.allocation_bytes;off+=PAGE_SIZE){
  const auto page=(ppnum_t)((completion.physical+off)>>PAGE_SHIFT);
  if((pmap_cache_attributes(page)&VM_WIMG_MASK)!=VM_WIMG_WCOMB)return EACCES;
 }
 e->nc_verified=1;
 auto *f=(volatile uint32_t *)(completion.cpu+KZE_OFFSET);
 KARAcquireNC();for(unsigned i=0;i<16;i++)e->guard_before[i]=f[i];
 for(unsigned i=0;i<16;i++)f[i]=kze_guard_word(i);
 KCPNativeBarrier(&kcp_native);
 for(unsigned i=0;i<16;i++)if(f[i]!=kze_guard_word(i))return EIO;
 memcpy((void *)(ring.cpu+KZE_START*4),words,sizeof expected);
 flush_dcache((vm_offset_t)ring.cpu,4096,0);KCPNativeBarrier(&kcp_native);
 return kze_gate(NULL);
}
static int kze_publish(void *,uint32_t end)
{
 int error=kze_gate(NULL);if(error)return error;auto *e=kze_evidence;
 if(end!=KZE_END || e->record.phase!=KZE_SUBMITTING || !e->record.touched ||
  !e->record.prepared || !e->record.submitted || e->kicks || !e->nc_verified)return 95;
 /* Sole GPU MMIO store: fixed existing RB_WPTR, after the original owner,
  * exact9 words, owned CPU copy, cache clean and DSB. No CP reinitialization. */
 ++e->kicks;*(volatile uint32_t *)(kcp_native.base+KCP_CP_RB_WPTR*4)=KZE_END;
 KCPNativeBarrier(&kcp_native);return 0;
}
static int kze_complete(void *)
{
 int error=kze_gate(NULL);if(error)return error;
 const auto *f=(volatile const uint32_t *)(kvg_engine.backing.region[5].cpu+KZE_OFFSET);
 KARAcquireNC();for(unsigned i=0;i<16;i++)kze_evidence->guard_after[i]=f[i];
 for(unsigned i=0;i<16;i++)if(kze_evidence->guard_after[i]!=(i?kze_guard_word(i):KZE_SEQUENCE))return EIO;
 return 0;
}
static void kze_delay(void *,unsigned us){if(us==100)IODelay(100);}
extern "C" int kebab_a650_gpu_secure_exit_locked(uint32_t *touched)
{
 if(!touched || *touched || !kze_evidence || __atomic_load_n(&kze_state,__ATOMIC_ACQUIRE)!=1)return 95;
 auto *e=kze_evidence;int error=kze_gate(NULL);if(error)return error;
 e->snapshot_before_error=KHO_VM_Snapshot(&kvg_native.owner,&e->vm_before);
 if(!e->snapshot_before_error)e->snapshot_before_error=kza2_unfaulted(&kza2_evidence->smmu[5],&e->vm_before);
 if(e->snapshot_before_error){e->record.result=e->snapshot_before_error;return e->record.result;}
 const KZEOps ops={NULL,KVGNativeNow,kze_gate,kze_authority,kze_sample,kze_prepare,kze_publish,kze_complete,kze_delay};
 error=KZERun(&ops,&e->record);*touched=e->record.touched;
 e->snapshot_after_error=KHO_VM_Snapshot(&kvg_native.owner,&e->vm_after);
 if(!e->snapshot_after_error)e->snapshot_after_error=kza2_unfaulted(&e->vm_before,&e->vm_after);
 if(!error && e->snapshot_after_error){error=e->snapshot_after_error;e->record.result=error;e->record.completed=0;}
 return error;
}
extern "C" int kebab_secure_exit_prepare(void)
{
 if(panic_active() || KFNativeWorker(NULL))return 2;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kze_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return expected==2?0:16;
 kze_evidence=(KZEEvidence *)IOMalloc(sizeof(KZEEvidence));
 if(kze_evidence){
  memset(kze_evidence,0,sizeof(*kze_evidence));uint32_t touched=0;
  int error=kebab_rpmh_gpu_cp_secure_exit(&kcp_native.owner,&touched);
  kze_evidence->record.result=error;kze_evidence->quarantined=error && touched;
  printf("KEBAB ZAPEXIT: terminal; result=%d phase=%u kicks=%u complete=%u; no SMC, TRUST or retry\n",
   error,kze_evidence->record.phase,kze_evidence->kicks,kze_evidence->record.completed);
 }
 __atomic_store_n(&kze_state,2,__ATOMIC_RELEASE);return 0;
}
extern "C" int kebab_secure_exit_snapshot(kebab_secure_exit_observation *out,uint32_t bytes)
{
 if(!out || bytes!=sizeof(*out) || (uintptr_t)out>UINTPTR_MAX-bytes)return 22;
 if(__atomic_load_n(&kze_state,__ATOMIC_ACQUIRE)!=2)return 35;
 if(kze_evidence && (uintptr_t)out<(uintptr_t)kze_evidence+sizeof(*kze_evidence) &&
  (uintptr_t)kze_evidence<(uintptr_t)out+bytes)return 22;
 memset(out,0,sizeof(*out));out->magic=KEBAB_SECURE_EXIT_MAGIC;out->schema=1;out->bytes=sizeof(*out);out->state=2;
 if(kze_evidence){out->result=kze_evidence->record.result;memcpy(out->payload,kze_evidence,sizeof(*kze_evidence));}
 else out->result=12;
 return 0;
}
#endif
