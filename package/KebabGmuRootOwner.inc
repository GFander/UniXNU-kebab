/* SPDX-License-Identifier: MIT */
/* Include AFTER the child-aware KebabGpuCcOwner.inc, itself after the real
 * canonical VRM/CXMX owner. No second hardware owner or second busy lock. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuRootOwner.h>
#include <kern/KebabGmuRootNativeBinding.h>
#else
#include "KebabGmuRootOwner.h"
#include "KebabGmuRootNativeBinding.h"
#endif
static KebabGmuRootLedger kebab_gmu_root_ledger;
/* Frequency-source children keep this actual root/pin held. */
static uint32_t kebab_gmu_root_children;
static KebabGmuRootRecord kebab_gmu_root_evidence;
static uint32_t kebab_gmu_root_has_evidence;
static int kebab_gmu_root_parent_ready(void)
{
 return kebab_gpu_cc_parent_ready() && !kebab_gpu_cc_ledger.quarantined && kebab_gpu_cc_ledger.held && kebab_gpu_cc_ledger.pinned_parent &&
 !kebab_gmu_root_ledger.quarantined && (uint64_t)kebab_gpu_cxmx_ledger.refs>=kebab_gpu_pinned_rail_floor();
}
int kebab_rpmh_gpu_gmu_root(unsigned action,const KebabGmuRootIo *io,KebabGmuRootRecord *out)
{
 KebabGmuRootContext c;int result=22;
 if(!out)return 22;KebabGmuRootInit(out,action);if(action>3)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)!=0){out->result=16;return 16;}
 if((action==KebabGmuRootInspect || kebab_gmu_root_ledger.quarantined || (action==KebabGmuRootHold200 && kebab_gmu_root_ledger.held)) && kebab_gmu_root_has_evidence){
  *out=kebab_gmu_root_evidence;out->action=action;out->evidence_replayed=1;
 }
 if(action==KebabGmuRootInspect){result=0;goto done;}
 if(!kebab_gmu_root_parent_ready()){result=95;goto done;}
 if(action==KebabGmuRootHold200 && kebab_gmu_root_ledger.held){result=0;goto done;}
 if(action==KebabGmuRootHold200 && kebab_gmu_root_ledger.attempted){result=95;goto done;}
 if(action!=KebabGmuRootObserve && !kebab_gmu_root_native_binding_qualified){result=95;goto done;}
 if(action==KebabGmuRootRestore && kebab_gmu_root_children){result=16;goto done;}
 if(action==KebabGmuRootRestore && (!kebab_gmu_root_ledger.held || !kebab_gmu_root_ledger.pinned)){result=22;goto done;}
 if(!io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us){result=22;goto done;}
 memset(&c,0,sizeof c);c.io=io;c.ledger=&kebab_gmu_root_ledger;c.r=out;
 result=KebabGmuRootPrepare(&c);if(result)goto failed;
 /* A fresh sample cannot replace the actual parent ledger contract. */
 if((out->before[0][2]&KebabGpuCcControlMask(2))!=kebab_gpu_cc_ledger.gdscr_control ||
    (out->before[0][4]&KebabGpuCcControlMask(4))!=kebab_gpu_cc_ledger.ahb_control ||
    (out->before[0][5]&KebabGpuCcControlMask(5))!=(kebab_gpu_cc_ledger.original_gmu&KebabGpuCcControlMask(5))){result=11;goto failed;}
 if(action==KebabGmuRootObserve){result=0;goto done;}
 if(action==KebabGmuRootHold200){
  if(kebab_gpu_cxmx_ledger.refs==UINT32_MAX || kebab_gpu_cc_children==UINT32_MAX){result=75;goto done;}
  ++kebab_gpu_cxmx_ledger.refs;++kebab_gpu_cc_children;kebab_gmu_root_ledger.pinned=1;
  result=KebabGmuRootHold(&c);
 }else result=KebabGmuRootRestoreSaved(&c);
 if(result)goto failed;
 if(action==KebabGmuRootRestore){--kebab_gpu_cxmx_ledger.refs;--kebab_gpu_cc_children;kebab_gmu_root_ledger.pinned=0;}
 goto done;
failed:
 if(kebab_gmu_root_ledger.pinned || kebab_gmu_root_ledger.attempted || kebab_gmu_root_ledger.held){
  kebab_gmu_root_ledger.quarantined=1;kebab_gpu_cc_ledger.quarantined=1;kebab_gpu_cxmx_ledger.quarantined=1;
  __atomic_store_n(&kebab_rpmh_global_lock.dead,1,__ATOMIC_RELEASE);out->failure_result=(uint32_t)result;
 }
done:
 out->same_native_owner=1;out->parent_qualified=(uint32_t)kebab_gmu_root_parent_ready();out->parent_refs=kebab_gpu_cxmx_ledger.refs;
 out->cc_held=kebab_gpu_cc_ledger.held;out->cc_children=kebab_gpu_cc_children;out->held=kebab_gmu_root_ledger.held;out->pinned=kebab_gmu_root_ledger.pinned;
 out->quarantined=kebab_gmu_root_ledger.quarantined;out->owner_dead=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 out->pll_vote_added=kebab_gmu_root_ledger.vote_added;out->pll_vote_retained=(uint32_t)(kebab_gmu_root_ledger.attempted && (kebab_gmu_root_ledger.vote&1));out->result=(uint32_t)result;
 if(!out->evidence_replayed && (action==KebabGmuRootHold200 || action==KebabGmuRootRestore || out->quarantined)){kebab_gmu_root_evidence=*out;kebab_gmu_root_has_evidence=1;}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return result;
}
