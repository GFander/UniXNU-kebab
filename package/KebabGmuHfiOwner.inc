/* SPDX-License-Identifier: MIT */
/* Same canonical TU, AFTER FirmwareOwner; no extra lock or rail owner. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuHfiOwner.h>
#include <kern/KebabGmuHfiBinding.h>
#else
#include "KebabGmuHfiOwner.h"
#include "KebabGmuHfiBinding.h"
#endif
static KebabGmuHfiSession kebab_gmu_hfi_session;
static KebabGmuHfiRecord kebab_gmu_hfi_evidence;
static KebabGmuBootContext kebab_gmu_hfi_clocks;
static KebabGmuBootRecord kebab_gmu_hfi_clock_evidence;
static uint32_t kebab_gmu_hfi_active;
int kebab_gpu_gmu_hfi_gate_locked(const KebabGpuColdToken *cold,uint64_t epoch)
{
 if(!__atomic_load_n(&kebab_gmu_hfi_active,__ATOMIC_ACQUIRE) || cold!=kebab_gmu_boot_identity ||
    epoch!=kebab_gmu_boot_ledger.namespace_epoch || kebab_gpu_gmu_boot_gate_locked(cold,epoch))return 95;
 int e=kebab_gpu_gmu_irq_healthy(kebab_gmu_firmware_irq);if(e)return e;
 return KebabGmuBootClockGates(&kebab_gmu_hfi_clocks,1);
}
int kebab_rpmh_gpu_gmu_hfi_version(const KebabGpuColdToken *cold,uint64_t epoch,
 const KebabGpuLaunchResourceToken *resources,const KebabGmuIrqToken *irq,
 const KebabGmuBootIo *clock_io,const KebabGmuHfiIo *io,KebabGmuHfiRecord *out)
{
 KebabGmuRootContext root;KebabGmuRootRecord rr;KebabGmuRootIo rio;KebabGmuHfiContext c;int e=95;const uint32_t *v=NULL;
 if(!out)return 22;KebabGmuHfiInit(out);
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;return 16;}
 if(!KebabGmuHfiVersionQualified)goto done;
 if(cold!=kebab_gmu_boot_identity || epoch!=kebab_gmu_boot_ledger.namespace_epoch ||
    resources!=kebab_gmu_firmware_resources || irq!=kebab_gmu_firmware_irq)goto done;
 if(kebab_gmu_hfi_session.attempted){*out=kebab_gmu_hfi_evidence;out->evidence_replayed=1;e=(int)out->result;goto done;}
 e=kebab_gpu_gmu_boot_gate_locked(cold,epoch);if(e)goto done;
 if(kebab_gmu_boot_ledger.phase!=KGB_HFI_QUEUES_READY || !kebab_gmu_firmware_evidence.fw_init_ack || !kebab_gmu_firmware_evidence.hfi_ctrl_ack){e=95;goto done;}
 if(!clock_io || !clock_io->read32 || !clock_io->barrier || !io){e=22;goto done;}
 /* Dedicated post-FW verification preserves the owned CM3 clock request
  * without treating its running-status sample as a permanent CSR bus gate. */
 e=kebab_gpu_launch_resources_verify_after_fw_locked(resources,cold,epoch,KLR_NEED_RSC);if(e)goto done;
 e=kebab_gpu_gmu_irq_verify_locked(irq,cold,epoch);if(e)goto done;
 rio.cookie=clock_io->cookie;rio.read32=clock_io->read32;rio.write32=clock_io->write32;rio.barrier=clock_io->barrier;rio.delay_us=clock_io->delay_us;rio.now_us=clock_io->now_us;
 memset(&root,0,sizeof root);KebabGmuRootInit(&rr,KebabGmuRootObserve);root.io=&rio;root.ledger=&kebab_gmu_root_ledger;root.r=&rr;
 e=KebabGmuRootSnapshot(&root,0);if(e)goto failed;v=rr.before[0];e=KebabGmuRootPllShape(v);if(e)goto failed;
 if((v[7]&0x800000f3u)!=2 || (v[8]&0x10371fu)!=0x100602u ||
    ((v[7]^kebab_gmu_root_ledger.cmd)&KebabGmuRootInvariantMask(7)) || ((v[8]^kebab_gmu_root_ledger.cfg)&0x10371fu) ||
    !(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u){e=11;goto failed;}
 memset(&kebab_gmu_hfi_clocks,0,sizeof kebab_gmu_hfi_clocks);KebabGmuBootInit(&kebab_gmu_hfi_clock_evidence,2);kebab_gmu_hfi_clock_evidence.firmware_start_requested=1;
 kebab_gmu_hfi_clocks.io=clock_io;kebab_gmu_hfi_clocks.ledger=&kebab_gmu_boot_ledger;kebab_gmu_hfi_clocks.r=&kebab_gmu_hfi_clock_evidence;
 kebab_gmu_hfi_clocks.gcc_cfg=v[0];kebab_gmu_hfi_clocks.gcc_reset=v[1];kebab_gmu_hfi_clocks.gdscr=kebab_gpu_cc_ledger.gdscr_control;
 kebab_gmu_hfi_clocks.ahb=kebab_gpu_cc_ledger.ahb_control;kebab_gmu_hfi_clocks.gx_gmu=v[6]&0x0fffffffu;
 e=clock_io->read32(clock_io->cookie,KGB_GCC,0x7100c,&kebab_gmu_hfi_clocks.memnoc);if(e)goto failed;kebab_gmu_hfi_clocks.memnoc&=0x0fffffffu;
 __atomic_store_n(&kebab_gmu_hfi_active,1,__ATOMIC_RELEASE);
 memset(&c,0,sizeof c);c.io=io;c.session=&kebab_gmu_hfi_session;c.record=out;
 e=KebabGmuHfiVersion(&c);
 __atomic_store_n(&kebab_gmu_hfi_active,0,__ATOMIC_RELEASE);
 if(e)goto failed;kebab_gmu_hfi_evidence=*out;goto done;
failed:
 out->result=e;out->quarantined=1;kebab_gmu_hfi_session.quarantined=1;kebab_gmu_boot_quarantine((unsigned)e);kebab_gmu_hfi_evidence=*out;
done:
 kebab_gmu_hfi_clocks.io=NULL;out->result=e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
