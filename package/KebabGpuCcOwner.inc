/* SPDX-License-Identifier: MIT */
/* Include once AFTER KebabGpuCxMxOwner.inc in the canonical VRM TU.
 * This is the real same-owner rail/CC bridge, not a caller boolean lease. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuCcOwner.h>
#else
#include "KebabGpuCcOwner.h"
#endif
static uint32_t kebab_gpu_cc_children; /* protected by the same native globalbusy */
static KebabGpuCcLedger kebab_gpu_cc_ledger;
static uint64_t kebab_gpu_pinned_rail_floor(void)
{
 return (kebab_gpu_cc_ledger.pinned_parent?1ULL:0ULL)+(uint64_t)kebab_gpu_cc_children;
}
static KebabGpuCcRecord kebab_gpu_cc_evidence;
static uint32_t kebab_gpu_cc_has_evidence;
static int kebab_gpu_cc_parent_ready(void)
{
 return !__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE) && !kebab_gpu_cxmx_ledger.quarantined &&
 kebab_gpu_cxmx_ledger.complete && kebab_gpu_cxmx_ledger.ack_mask==3 && !kebab_gpu_cxmx_ledger.uncertain_mask && kebab_gpu_cxmx_ledger.refs>0;
}
int kebab_rpmh_gpu_cc(unsigned action,const KebabGpuCcIo *io,KebabGpuCcRecord *out)
{
 KebabGpuCcContext c;int result=22,ready;
 if(!out)return 22;KebabGpuCcInit(out,action);if(action>3)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)!=0){out->result=16;return 16;}
 ready=kebab_gpu_cc_parent_ready() && !kebab_gpu_cc_ledger.quarantined;
 if((action==KebabGpuCcInspect || kebab_gpu_cc_ledger.quarantined || (action==KebabGpuCcEnableHold && kebab_gpu_cc_ledger.held)) && kebab_gpu_cc_has_evidence) {
  *out=kebab_gpu_cc_evidence;out->evidence_replayed=1;out->action=action;
 }
 if(action==KebabGpuCcInspect){result=0;goto done;}
 if(!ready){result=95;goto done;}
 if(action==KebabGpuCcEnableHold && kebab_gpu_cc_ledger.held){result=0;goto done;}
 if(action==KebabGpuCcEnableHold && kebab_gpu_cc_ledger.attempted){result=95;goto done;}
 if(action==KebabGpuCcRestore && kebab_gpu_cc_children){result=16;goto done;}
 if(action==KebabGpuCcRestore && (!kebab_gpu_cc_ledger.held || !kebab_gpu_cc_ledger.pinned_parent)){result=22;goto done;}
 if(!io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us){result=22;goto done;}
 c.io=io;c.r=out;c.ledger=&kebab_gpu_cc_ledger;c.cfg=c.reset=0;
 result=KebabGpuCcPrepare(&c);if(result)goto failed;
 if(action==KebabGpuCcObserve){result=0;goto done;}
 if(action==KebabGpuCcEnableHold) {
  if(kebab_gpu_cxmx_ledger.refs==UINT32_MAX){result=75;goto done;}
  ++kebab_gpu_cxmx_ledger.refs;kebab_gpu_cc_ledger.pinned_parent=1;
  result=KebabGpuCcEnable(&c);
 } else result=KebabGpuCcRestoreSaved(&c);
 if(result)goto failed;
 if(action==KebabGpuCcRestore) {
  --kebab_gpu_cxmx_ledger.refs;kebab_gpu_cc_ledger.pinned_parent=0;
 }
 goto done;
failed:
 /* A live CC hold or any post-pin failure cannot be unwound on guesswork.
  * Keep the extra rail ref and close the original RPMh global owner. */
 if(kebab_gpu_cc_ledger.pinned_parent || kebab_gpu_cc_ledger.held || kebab_gpu_cc_ledger.attempted) {
  kebab_gpu_cc_ledger.quarantined=1;kebab_gpu_cxmx_ledger.quarantined=1;
  __atomic_store_n(&kebab_rpmh_global_lock.dead,1,__ATOMIC_RELEASE);out->failure_result=(uint32_t)result;
 }
done:
 out->same_native_owner=1;out->parent_qualified=(uint32_t)(kebab_gpu_cc_parent_ready() && !kebab_gpu_cc_ledger.quarantined);
 out->parent_refs=kebab_gpu_cxmx_ledger.refs;out->parent_ack_mask=kebab_gpu_cxmx_ledger.ack_mask;
 out->owner_dead=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 out->held=kebab_gpu_cc_ledger.held;out->pinned_parent=kebab_gpu_cc_ledger.pinned_parent;out->quarantined=kebab_gpu_cc_ledger.quarantined;
 out->retention_added=kebab_gpu_cc_ledger.retention_added;out->result=(uint32_t)result;
 if(!out->evidence_replayed && (action==KebabGpuCcEnableHold || action==KebabGpuCcRestore || out->quarantined)) {
  kebab_gpu_cc_evidence=*out;kebab_gpu_cc_has_evidence=1;
 }
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return result;
}
