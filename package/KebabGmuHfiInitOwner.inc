/* SPDX-License-Identifier: MIT */
/* Append AFTER the unchanged HfiOwner in the ONE canonical VRM TU. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuHfiInitOwner.h>
#include <kern/KebabGmuHfiInitBinding.h>
#else
#include "KebabGmuHfiInitOwner.h"
#include "KebabGmuHfiInitBinding.h"
#endif
static KebabGmuHfiInitBook kebab_gmu_hfi_init_book;
static KebabGmuHfiInitRecord kebab_gmu_hfi_init_evidence;
static uint32_t kebab_gmu_hfi_init_called;
int kebab_rpmh_gpu_gmu_hfi_initialize(const KebabGpuColdToken *cold,uint64_t epoch,
 const KebabGpuLaunchResourceToken *resources,const KebabGmuIrqToken *irq,
 const KebabGmuBootIo *clock_io,const KebabGmuHfiInitIo *io,KebabGmuHfiInitRecord *out)
{
 KebabGmuRootContext root;KebabGmuRootRecord rr;KebabGmuRootIo rio;KebabGmuHfiInitContext c;int e=95;const uint32_t *v=NULL;
 if(!out)return 22;KebabGmuHfiInitRecordInit(out);
 if(!KebabGmuHfiInitQualified){out->io.result=95;return 95;}
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->io.result=16;return 16;}
 if(cold!=kebab_gmu_boot_identity || epoch!=kebab_gmu_boot_ledger.namespace_epoch || resources!=kebab_gmu_firmware_resources || irq!=kebab_gmu_firmware_irq)goto done;
 if(kebab_gmu_hfi_init_called){*out=kebab_gmu_hfi_init_evidence;out->replayed=1;e=(int)out->io.result;goto done;}
 if(kebab_gmu_hfi_active || !kebab_gmu_hfi_session.version_verified || kebab_gmu_hfi_session.quarantined || kebab_gmu_hfi_session.sequence!=1 ||
    kebab_gmu_hfi_evidence.result || !kebab_gmu_hfi_evidence.version_verified || kebab_gmu_hfi_evidence.returned_version!=0x20010008)goto done;
 e=kebab_gpu_gmu_boot_gate_locked(cold,epoch);if(e)goto done;
 if(kebab_gmu_boot_ledger.phase!=KGB_HFI_QUEUES_READY || !kebab_gmu_firmware_evidence.fw_init_ack || !kebab_gmu_firmware_evidence.hfi_ctrl_ack){e=95;goto done;}
 if(!clock_io || !clock_io->read32 || !clock_io->barrier || !io){e=22;goto done;}
 e=kebab_gpu_launch_resources_verify_after_fw_locked(resources,cold,epoch,KLR_NEED_RSC);if(e)goto done;
 e=kebab_gpu_gmu_irq_verify_locked(irq,cold,epoch);if(e)goto done;
 rio.cookie=clock_io->cookie;rio.read32=clock_io->read32;rio.write32=clock_io->write32;rio.barrier=clock_io->barrier;rio.delay_us=clock_io->delay_us;rio.now_us=clock_io->now_us;
 memset(&root,0,sizeof root);KebabGmuRootInit(&rr,KebabGmuRootObserve);root.io=&rio;root.ledger=&kebab_gmu_root_ledger;root.r=&rr;
 e=KebabGmuRootSnapshot(&root,0);if(e)goto done;v=rr.before[0];e=KebabGmuRootPllShape(v);if(e)goto done;
 if((v[7]&0x800000f3u)!=2 || (v[8]&0x10371fu)!=0x100602u ||
    ((v[7]^kebab_gmu_root_ledger.cmd)&KebabGmuRootInvariantMask(7)) || ((v[8]^kebab_gmu_root_ledger.cfg)&0x10371fu) ||
    !(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u){e=11;goto done;}
 memset(&kebab_gmu_hfi_clocks,0,sizeof kebab_gmu_hfi_clocks);KebabGmuBootInit(&kebab_gmu_hfi_clock_evidence,2);kebab_gmu_hfi_clock_evidence.firmware_start_requested=1;
 kebab_gmu_hfi_clocks.io=clock_io;kebab_gmu_hfi_clocks.ledger=&kebab_gmu_boot_ledger;kebab_gmu_hfi_clocks.r=&kebab_gmu_hfi_clock_evidence;
 kebab_gmu_hfi_clocks.gcc_cfg=v[0];kebab_gmu_hfi_clocks.gcc_reset=v[1];kebab_gmu_hfi_clocks.gdscr=kebab_gpu_cc_ledger.gdscr_control;
 kebab_gmu_hfi_clocks.ahb=kebab_gpu_cc_ledger.ahb_control;kebab_gmu_hfi_clocks.gx_gmu=v[6]&0x0fffffffu;
 e=clock_io->read32(clock_io->cookie,KGB_GCC,0x7100c,&kebab_gmu_hfi_clocks.memnoc);if(e)goto done;kebab_gmu_hfi_clocks.memnoc&=0x0fffffffu;
 __atomic_store_n(&kebab_gmu_hfi_active,1,__ATOMIC_RELEASE);
 kebab_gmu_hfi_init_called=1;
 memset(&c,0,sizeof c);c.io=io;c.session=&kebab_gmu_hfi_session;c.book=&kebab_gmu_hfi_init_book;c.r=out;
 e=KebabGmuHfiInitialize(&c);
 out->io.result=e; /* Includes early validation returns before Core done. */
 if(e && kebab_gmu_hfi_init_book.attempted)kebab_gmu_boot_quarantine((unsigned)e);
 kebab_gmu_hfi_init_evidence=*out;
done:
 __atomic_store_n(&kebab_gmu_hfi_active,0,__ATOMIC_RELEASE);kebab_gmu_hfi_clocks.io=NULL;
 out->io.result=e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
