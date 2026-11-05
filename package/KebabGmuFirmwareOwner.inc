/* SPDX-License-Identifier: MIT */
/* AFTER sealed BootOwner and the real resource/IRQ bridges, same canonical
 * VRM TU/globalbusy. There is no caller-supplied owned bitmask. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuFirmwareOwner.h>
#include <kern/KebabGmuFirmwareBinding.h>
#else
#include "KebabGmuFirmwareOwner.h"
#include "KebabGmuFirmwareBinding.h"
#endif
static const KebabGpuLaunchResourceToken *kebab_gmu_firmware_resources;
static const KebabGmuIrqToken *kebab_gmu_firmware_irq;
static uint32_t kebab_gmu_firmware_attempted,kebab_gmu_firmware_active;
static KebabGmuBootRecord kebab_gmu_firmware_evidence;
int kebab_rpmh_gpu_gmu_firmware_irq_preflight(const KebabGpuColdToken *cold,uint64_t epoch,const KebabGmuIrqToken *irq)
{
 if(!cold || !epoch || !irq)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=kebab_gpu_gmu_boot_reset_held_locked(cold,epoch);
 if(!e)e=kebab_gpu_gmu_irq_verify_locked(irq,cold,epoch);
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_gpu_gmu_launch_active_locked(const KebabGpuColdToken *cold,uint64_t epoch)
{
 if(!__atomic_load_n(&kebab_gmu_firmware_active,__ATOMIC_ACQUIRE) ||
    kebab_gpu_gmu_boot_gate_locked(cold,epoch))return 95;
 return kebab_gpu_gmu_irq_healthy(kebab_gmu_firmware_irq);
}
int kebab_gpu_gmu_firmware_write_stage_locked(const KebabGpuColdToken *cold,uint64_t epoch,unsigned stage)
{
 int e=kebab_gpu_gmu_launch_active_locked(cold,epoch);if(e)return e;
 if(stage==1)return kebab_gmu_boot_ledger.phase==KGB_FW_STARTING?0:95;
 if(stage==2)return kebab_gmu_boot_ledger.phase==KGB_FW_RUNNING?0:95;
 return 22;
}
int kebab_rpmh_gpu_gmu_launch(const KebabGpuColdToken *cold,uint64_t epoch,
 const KebabGpuLaunchResourceToken *resources,const KebabGmuIrqToken *irq,
 const KebabGmuBootIo *io,KebabGmuBootRecord *out)
{
 KebabGmuBootContext c;KebabGmuRootContext root;KebabGmuRootRecord rr;KebabGmuRootIo rio;
 int e=95;uint64_t start=0;const uint32_t *v=NULL;
 if(!out)return 22;KebabGmuBootInit(out,KebabGmuBootLaunch);
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;return 16;}
 if(!KebabGmuFirmwareLaunchQualified)goto done;
 if(kebab_gmu_firmware_attempted){
  if(cold!=kebab_gmu_boot_identity || epoch!=kebab_gmu_boot_ledger.namespace_epoch ||
     resources!=kebab_gmu_firmware_resources || irq!=kebab_gmu_firmware_irq){e=95;goto done;}
  *out=kebab_gmu_firmware_evidence;out->evidence_replayed=1;e=(int)out->result;goto done;}
 e=kebab_gpu_gmu_boot_reset_held_locked(cold,epoch);if(e)goto done;
 if(!resources || !irq || !io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us){e=22;goto done;}
 e=kebab_gpu_launch_resources_verify_locked(resources,cold,epoch,KLR_NEED_RSC);if(e)goto done;
 e=kebab_gpu_gmu_irq_verify_locked(irq,cold,epoch);if(e)goto done;
 if(resources->bandwidth_mode!=KLR_BW_RETAIN_INHERITED_NO_WRITE || resources->ready_mask!=KLR_NEED_RSC){e=95;goto done;}
 *out=kebab_gmu_boot_evidence;out->action=KebabGmuBootLaunch;out->evidence_replayed=0;
 out->reads=out->writes=out->barriers=out->polls=out->delay_us=0;out->primary_error=out->io_error=0;
 kebab_gmu_firmware_resources=resources;kebab_gmu_firmware_irq=irq;kebab_gmu_firmware_attempted=1;
 __atomic_store_n(&kebab_gmu_firmware_active,1,__ATOMIC_RELEASE);
 rio.cookie=io->cookie;rio.read32=io->read32;rio.write32=io->write32;rio.barrier=io->barrier;rio.delay_us=io->delay_us;rio.now_us=io->now_us;
 memset(&root,0,sizeof root);KebabGmuRootInit(&rr,KebabGmuRootObserve);root.io=&rio;root.ledger=&kebab_gmu_root_ledger;root.r=&rr;
 /* Prepare is the pre-clock root-programming API and intentionally requires
  * GMU1098 quiet. Here the same owner has already enabled it: retain the
  * double raw snapshot + PLL/rate/parent checks, without invoking Prepare's
  * incompatible pre-clock quiet predicate or changing that sealed API. */
 e=KebabGmuRootSnapshot(&root,0);if(e)goto failed;v=rr.before[0];
 e=KebabGmuRootPllShape(v);if(e)goto failed;
 if((v[7]&0x800000f3u)!=2 || (v[8]&0x10371fu)!=0x100602u ||
    ((v[7]^kebab_gmu_root_ledger.cmd)&KebabGmuRootInvariantMask(7)) || ((v[8]^kebab_gmu_root_ledger.cfg)&0x10371fu) ||
    !(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u){e=11;goto failed;}
 memset(&c,0,sizeof c);c.io=io;c.ledger=&kebab_gmu_boot_ledger;c.r=out;
 c.gcc_cfg=v[0];c.gcc_reset=v[1];c.gdscr=kebab_gpu_cc_ledger.gdscr_control;c.ahb=kebab_gpu_cc_ledger.ahb_control;
 c.gx_gmu=v[6]&0x0fffffffu;
 e=KebabGmuBootRaw(&c,KGB_GCC,0x7100c,&c.memnoc);if(e)goto failed;c.memnoc&=0x0fffffffu;
 start=c.started_us=io->now_us(io->cookie);
 /* typed resource verification, not its serialized ready_mask, established
  * RSC ownership above. Retained BW is deliberately not a bandwidth grant. */
 e=KebabGmuBootStartFirmwareWithBandwidth(&c,KGB_NEED_RSC|KGB_NEED_IRQ,0x06050200,KGB_BW_RETAIN_INHERITED_NO_WRITE);
 if(e)goto failed;
 out->elapsed_us=io->now_us(io->cookie)-start;goto final;
failed:
 out->primary_error=e;kebab_gmu_boot_quarantine((unsigned)e);
final:
 __atomic_store_n(&kebab_gmu_firmware_active,0,__ATOMIC_RELEASE);
 out->phase=kebab_gmu_boot_ledger.phase;out->root_children=kebab_gmu_root_children;out->parent_refs=kebab_gpu_cxmx_ledger.refs;
 out->quarantined=kebab_gmu_boot_ledger.quarantined;out->owner_dead=__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 out->result=e;kebab_gmu_firmware_evidence=*out;kebab_gmu_boot_evidence=*out;
done:
 out->result=e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
