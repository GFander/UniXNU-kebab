/* SPDX-License-Identifier: MIT */
/* AFTER Root(child-aware), Bus and Cold Owner.inc in the canonical VRM TU.
 * Same globalbusy/dead and same real parent ledgers, no second RSC owner. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuBootOwner.h>
#include <kern/KebabGmuBootBinding.h>
#include <kern/KebabGpuFirstAttachNative.h>
#else
#include "KebabGmuBootOwner.h"
#include "KebabGmuBootBinding.h"
#include "KebabGpuFirstAttachNative.h"
#endif
static KebabGmuBootLedger kebab_gmu_boot_ledger;
static KebabGmuBootRecord kebab_gmu_boot_evidence;
static const KebabGpuColdToken *kebab_gmu_boot_identity;
static int kebab_gmu_boot_parent_locked(void)
{
 return !__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE) &&
  kebab_gmu_root_parent_ready() && kebab_gmu_root_ledger.held && kebab_gmu_root_ledger.pinned &&
  !kebab_gmu_root_ledger.quarantined && kebab_gpu_bus_parent_ready() && kebab_gpu_bus_ledger.held &&
  !kebab_gmu_boot_ledger.quarantined;
}
int kebab_gpu_gmu_boot_preflight_locked(const KebabGpuColdToken *t,uint64_t epoch)
{
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) || !kebab_gmu_boot_parent_locked())return 95;
 int e=kebab_gpu_cold_token_locked(t,0);return e?e:kebab_gpu_first_ready_locked(t,epoch);
}
int kebab_gpu_gmu_boot_gate_locked(const KebabGpuColdToken *t,uint64_t epoch)
{
 if(t!=kebab_gmu_boot_identity ||
    epoch!=kebab_gmu_boot_ledger.namespace_epoch || !kebab_gmu_boot_ledger.pinned || !kebab_gmu_root_children)return 95;
 return kebab_gpu_gmu_boot_preflight_locked(t,epoch);
}
int kebab_gpu_gmu_boot_reset_held_locked(const KebabGpuColdToken *t,uint64_t epoch)
{
 int e=kebab_gpu_gmu_boot_gate_locked(t,epoch);if(e)return e;
 return kebab_gmu_boot_ledger.phase==KGB_RESET_HELD_TCM && kebab_gmu_boot_ledger.reset_issued &&
  kebab_gmu_boot_ledger.tcm_verified?0:95;
}
static int kebab_gmu_boot_mark_started(void *unused)
{(void)unused;return kebab_gpu_cold_mark_started_locked((KebabGpuColdToken *)kebab_gmu_boot_identity);}
static void kebab_gmu_boot_quarantine(unsigned error)
{
 kebab_gmu_boot_ledger.quarantined=1;kebab_gmu_boot_ledger.phase=KGB_QUARANTINED;
 kebab_gmu_root_ledger.quarantined=1;kebab_gpu_cc_ledger.quarantined=1;kebab_gpu_cxmx_ledger.quarantined=1;
 kebab_gmu_boot_evidence.primary_error=error;__atomic_store_n(&kebab_rpmh_global_lock.dead,1,__ATOMIC_RELEASE);
}
int kebab_rpmh_gpu_gmu_boot(unsigned action,const KebabGpuColdToken *token,uint64_t epoch,
 const KebabA650GmuTcmManifest *manifest,const KebabGmuBootIo *io,KebabGmuBootRecord *out)
{
 KebabGmuBootContext c;KebabGmuRootContext root;KebabGmuRootRecord root_record;KebabGmuRootIo root_io;KebabGmuBootIo local;
 int e=95;uint64_t begin=0;const uint32_t *v=NULL;
 if(!out || action>2)return 22;KebabGmuBootInit(out,action);
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;return 16;}
 if(action==KebabGmuBootInspect){
  if(kebab_gmu_boot_evidence.magic){*out=kebab_gmu_boot_evidence;out->action=action;out->evidence_replayed=1;}
  out->root_children=kebab_gmu_root_children;out->parent_refs=kebab_gpu_cxmx_ledger.refs;
  out->owner_dead=__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
  __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return 0;
 }
 if(!KebabGmuBootResetLoadQualified || !kebab_gmu_boot_parent_locked())goto done;
 if(action==KebabGmuBootLaunch){
  /* Actual native providers, not an asserted readiness vector, must replace
   * this closed policy before Launch can call the fully bounded core path. */
  if(kebab_gmu_boot_evidence.magic){*out=kebab_gmu_boot_evidence;out->action=action;out->evidence_replayed=1;}
  out->missing_owners=7u&~KebabGmuBootNativeLaunchOwners;goto done;
 }
 if(kebab_gmu_boot_ledger.attempted || kebab_gmu_boot_ledger.pinned){e=95;goto done;}
 if(!io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us ||
    !io->open_gmu || !io->source_check || !io->tcm_word || !manifest){e=22;goto done;}
 e=kebab_gpu_cold_token_locked(token,1);if(e)goto done;
 e=kebab_gpu_first_ready_locked(token,epoch);if(e)goto done;
 e=kebab_gpu_bus_verify_locked();if(e)goto done;
 if(manifest->result || manifest->word_count!=8192 || manifest->payload_words!=4601 || manifest->tcm_bytes!=32768 ||
    manifest->image_bytes!=41548 || manifest->core_version!=0x20010008 || manifest->hfi_version!=0x20000001){e=22;goto done;}
 e=io->source_check(io->cookie);if(e)goto done;out->source_qualified=1;
 root_io.cookie=io->cookie;root_io.read32=io->read32;root_io.write32=io->write32;root_io.barrier=io->barrier;root_io.delay_us=io->delay_us;root_io.now_us=io->now_us;
 memset(&root,0,sizeof root);KebabGmuRootInit(&root_record,KebabGmuRootHold200);root.io=&root_io;root.ledger=&kebab_gmu_root_ledger;root.r=&root_record;
 e=KebabGmuRootPrepare(&root);if(e)goto done;v=root_record.before[0];
 if((v[2]&KebabGpuCcControlMask(2))!=kebab_gpu_cc_ledger.gdscr_control ||
    (v[4]&0x0fffffffu)!=kebab_gpu_cc_ledger.ahb_control || (v[7]&0x800000f3u)!=2 ||
    (v[8]&0x10371fu)!=0x100602u || !(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u ||
    ((v[8]^kebab_gmu_root_ledger.cfg)&0x10371fu) || ((v[7]^kebab_gmu_root_ledger.cmd)&KebabGmuRootInvariantMask(7))){e=11;goto done;}
 out->root_qualified=1;
 if(kebab_gmu_root_children==UINT32_MAX){e=75;goto done;}
 ++kebab_gmu_root_children;kebab_gmu_boot_ledger.pinned=1;kebab_gmu_boot_identity=token;kebab_gmu_boot_ledger.namespace_epoch=epoch;
 local=*io;local.mark_started=kebab_gmu_boot_mark_started;
 memset(&c,0,sizeof c);c.io=&local;c.ledger=&kebab_gmu_boot_ledger;c.r=out;c.gcc_cfg=v[0];c.gcc_reset=v[1];
 c.gdscr=v[2]&KebabGpuCcControlMask(2);c.ahb=v[4]&0x0fffffffu;c.gx_gmu=v[6]&0x0fffffffu;
 e=KebabGmuBootRaw(&c,0,0x7100c,&c.memnoc);if(e)goto failed;c.memnoc&=0x0fffffffu;
 begin=c.started_us=io->now_us(io->cookie);e=KebabGmuBootResetAndLoad(&c);
 if(e)goto failed;
 out->elapsed_us=io->now_us(io->cookie)-begin;goto done;
failed:
 out->primary_error=e;kebab_gmu_boot_quarantine((unsigned)e);
done:
 out->phase=kebab_gmu_boot_ledger.phase;out->child_pinned=kebab_gmu_boot_ledger.pinned;out->root_children=kebab_gmu_root_children;
 out->parent_refs=kebab_gpu_cxmx_ledger.refs;out->owner_dead=__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 out->quarantined=kebab_gmu_boot_ledger.quarantined;out->namespace_epoch=kebab_gmu_boot_ledger.namespace_epoch;out->result=e;
 if(action!=KebabGmuBootInspect)kebab_gmu_boot_evidence=*out;
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
