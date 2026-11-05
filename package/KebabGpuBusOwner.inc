/* SPDX-License-Identifier: MIT */
/* Include once AFTER the paired CC/CXMX owner incs in the same canonical VRM
 * TU. The shared floor and CC.children gate must be integrated with this file. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuBusOwner.h>
#include <kern/KebabGpuBusNativeBinding.h>
#else
#include "KebabGpuBusOwner.h"
#include "KebabGpuBusNativeBinding.h"
#endif
static KebabGpuBusLedger kebab_gpu_bus_ledger;
static KebabGpuBusRecord kebab_gpu_bus_evidence;
static int kebab_gpu_bus_parent_ready(void)
{
 return KebabGpuBusBound(&KebabGpuBusNativeBinding) && kebab_gpu_cc_parent_ready() &&
  !kebab_gpu_cc_ledger.quarantined && kebab_gpu_cc_ledger.held && kebab_gpu_cc_ledger.pinned_parent &&
  (uint64_t)kebab_gpu_cxmx_ledger.refs>=kebab_gpu_pinned_rail_floor() &&
  !kebab_gpu_bus_ledger.quarantined;
}
static void kebab_gpu_bus_quarantine(unsigned error)
{
 kebab_gpu_bus_ledger.quarantined=1;kebab_gpu_cc_ledger.quarantined=1;kebab_gpu_cxmx_ledger.quarantined=1;
 kebab_gpu_bus_evidence.primary_error=error;__atomic_store_n(&kebab_rpmh_global_lock.dead,1,__ATOMIC_RELEASE);
}
static int kebab_gpu_bus_verify_locked(void)
{
 KebabGpuBusContext c;int e;if(!kebab_gpu_bus_parent_ready() || !kebab_gpu_bus_ledger.held)return 95;
 c.ledger=&kebab_gpu_bus_ledger;c.r=&kebab_gpu_bus_evidence;c.io=&kebab_gpu_bus_ledger.io;
 e=KebabGpuBusVerify(&c);if(e)kebab_gpu_bus_quarantine((unsigned)e);return e;
}
static void kebab_gpu_bus_record(KebabGpuBusRecord *r,unsigned result)
{
 KebabGpuBusLedger *l=&kebab_gpu_bus_ledger;r->result=result;r->same_native_owner=1;
 r->owner_dead=__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 r->held=l->held;r->attempted=l->attempted;r->quarantined=l->quarantined;r->pinned_parent=l->pinned_parent;
 r->parent_refs=kebab_gpu_cxmx_ledger.refs;r->parent_children=kebab_gpu_cc_children;r->readers=l->readers;
 r->generation=l->generation;r->read_cb_mask=KebabGpuBusNativeBinding.read_cb_mask;
 r->parent_gdscr_control_mask=l->parent_gdscr_mask;
 r->can_read=kebab_gpu_bus_parent_ready() && l->held && l->pinned_parent;
 r->dma_bandwidth_qualified=0;
}
int kebab_rpmh_gpu_bus_gate(void)
{
 int e;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 e=kebab_gpu_bus_parent_ready()?0:95;
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_bus(unsigned action,const KebabGpuBusIo *io,KebabGpuBusRecord *out)
{
 KebabGpuBusContext c;int e=95;KebabGpuBusLedger *l=&kebab_gpu_bus_ledger;
 if(!out || action>2)return 22;KebabGpuBusInit(out,action);
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;return 16;}
 if(action==KebabGpuBusInspect){if(kebab_gpu_bus_evidence.magic)*out=kebab_gpu_bus_evidence;out->action=action;out->evidence_replayed=1;e=0;goto done;}
 if(!kebab_gpu_bus_parent_ready())goto done;
 if(action==KebabGpuBusEnableHold && l->held){
  *out=kebab_gpu_bus_evidence;out->action=action;out->evidence_replayed=1;
  c.ledger=l;c.r=out;c.io=&l->io;e=KebabGpuBusVerify(&c);
  if(e){out->primary_error=e;kebab_gpu_bus_quarantine((unsigned)e);}goto done;
 }
 if(l->attempted || l->pinned_parent)goto done;
 if(!io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us){e=22;goto done;}
 c.ledger=l;c.r=out;c.io=io;l->parent_gdscr=kebab_gpu_cc_ledger.gdscr_control;
 l->parent_gdscr_mask=KebabGpuCcControlMask(2);l->parent_ahb=kebab_gpu_cc_ledger.ahb_control;
 if(action==KebabGpuBusEnableHold) {
  if(kebab_gpu_cxmx_ledger.refs==UINT32_MAX || kebab_gpu_cc_children==UINT32_MAX){e=75;goto done;}
  ++kebab_gpu_cxmx_ledger.refs;++kebab_gpu_cc_children;l->pinned_parent=1;
  l->io=*io;l->generation=1; /* This one-shot owner has no reset/retry API. */
 }
 e=KebabGpuBusRun(&c,action);
 if(e && l->pinned_parent){out->primary_error=e;kebab_gpu_bus_quarantine((unsigned)e);}
done:
 kebab_gpu_bus_record(out,(unsigned)e);
 if(action!=KebabGpuBusInspect)kebab_gpu_bus_evidence=*out;
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
static int kebab_gpu_bus_token_locked(KebabGpuBusToken *t)
{
 if(!t || !t->held || t->slot>=4 || t->generation!=kebab_gpu_bus_ledger.generation ||
    kebab_gpu_bus_ledger.token[t->slot]!=t || !t->sequence ||
    t->sequence!=kebab_gpu_bus_ledger.token_sequence[t->slot] ||
    t->read_cb_mask!=KebabGpuBusNativeBinding.read_cb_mask)return 22;
 return 0;
}
int kebab_rpmh_gpu_bus_acquire(KebabGpuBusToken *t)
{
 unsigned i;int e=95;KebabGpuBusLedger *l=&kebab_gpu_bus_ledger;
 if(!t)return 22;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 if(!kebab_gpu_bus_parent_ready() || !l->held || !l->pinned_parent)goto done;
 if(t->held){e=16;goto done;}if(l->sequence==UINT64_MAX){e=75;goto done;}
 e=kebab_gpu_bus_verify_locked();if(e)goto done;
 for(i=0;i<4;i++)if(!l->token[i])break;
 if(i==4){e=16;goto done;}memset(t,0,sizeof *t);t->generation=l->generation;t->sequence=++l->sequence;t->slot=i;t->held=1;t->read_cb_mask=KebabGpuBusNativeBinding.read_cb_mask;
 l->token[i]=t;l->token_sequence[i]=t->sequence;++l->readers;e=0;
done:__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_bus_check(KebabGpuBusToken *t)
{
 int e;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 e=kebab_gpu_bus_token_locked(t);if(!e)e=kebab_gpu_bus_verify_locked();
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_bus_end(KebabGpuBusToken *t)
{
 int e;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 e=kebab_gpu_bus_token_locked(t);if(!e && !kebab_gpu_bus_parent_ready())e=95;
 if(!e){kebab_gpu_bus_ledger.token[t->slot]=NULL;kebab_gpu_bus_ledger.token_sequence[t->slot]=0;--kebab_gpu_bus_ledger.readers;t->held=0;}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
