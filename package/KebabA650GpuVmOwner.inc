/* SPDX-License-Identifier: MIT */
/* Append after existing Init/ResourceObserve owners in canonical VRM TU. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabA650GpuVmOwner.h>
#include <kern/KebabA650GpuVmBinding.h>
#else
#include "KebabA650GpuVmOwner.h"
#include "KebabA650GpuVmBinding.h"
#endif
static KebabA650GpuVmOwnerToken *kebab_gpu_vm_identity;
static uint64_t kebab_gpu_vm_generation;
static KebabGmuBootIo kebab_gpu_vm_io;
static KebabGmuBootContext kebab_gpu_vm_clocks;
static KebabGmuBootRecord kebab_gpu_vm_clock_evidence;
static int kebab_gpu_vm_parent_locked(const KebabGpuColdToken*cold,uint64_t epoch)
{
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) || !KebabA650GpuVmNativeQualified ||
 cold!=kebab_gmu_boot_identity || epoch!=kebab_gmu_boot_ledger.namespace_epoch ||
 kebab_gmu_hfi_active || !kebab_gmu_hfi_init_book.complete || kebab_gmu_hfi_init_book.quarantined ||
 kebab_gmu_hfi_session.quarantined || kebab_gmu_hfi_session.sequence<5 || !kebab_gmu_hfi_session.version_verified ||
 kebab_gmu_boot_ledger.phase!=KGB_HFI_QUEUES_READY)return 95;
 int e=kebab_gpu_gmu_boot_gate_locked(cold,epoch);if(e)return e;
 e=kebab_gpu_launch_resources_verify_after_fw_locked(kebab_gmu_firmware_resources,cold,epoch,KLR_NEED_RSC);if(e)return e;
 return kebab_gpu_gmu_irq_verify_locked(kebab_gmu_firmware_irq,cold,epoch);
}
static int kebab_gpu_vm_token_locked(const KebabA650GpuVmOwnerToken*t)
{
 if(!t || t!=kebab_gpu_vm_identity || !t->held || !t->generation || t->generation!=kebab_gpu_vm_generation || kebab_gmu_root_children<3)return 95;
 int e=kebab_a650_gpu_vm_ready_locked(t);if(e)return e;return kebab_gpu_vm_parent_locked(t->cold,t->epoch);
}
int kebab_rpmh_gpu_vm_acquire(KebabA650GpuVmOwnerToken*t,const KebabGpuColdToken*cold,uint64_t epoch,const KebabGmuBootIo*io)
{
 KebabGmuRootContext root;KebabGmuRootRecord rr;KebabGmuRootIo rio;const uint32_t*v=NULL;int e=95,configured=0;
 if(!t || !io || !io->read32 || !io->barrier || !io->now_us)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 if(t->held || t->generation || kebab_gpu_vm_identity || kebab_gpu_vm_generation==UINT64_MAX){e=16;goto done;}
 e=kebab_gpu_vm_parent_locked(cold,epoch);if(e)goto done;
 /* Native helper checks the already-reserved graphics namespace object. */
 e=kebab_a650_gpu_vm_ready_locked(t);if(e)goto done;
 rio.cookie=io->cookie;rio.read32=io->read32;rio.write32=NULL;rio.barrier=io->barrier;rio.delay_us=NULL;rio.now_us=io->now_us;
 memset(&root,0,sizeof root);KebabGmuRootInit(&rr,KebabGmuRootObserve);root.io=&rio;root.ledger=&kebab_gmu_root_ledger;root.r=&rr;
 e=KebabGmuRootSnapshot(&root,0);if(e)goto done;v=rr.before[0];e=KebabGmuRootPllShape(v);if(e)goto done;
 if((v[7]&0x800000f3u)!=2 || (v[8]&0x10371fu)!=0x100602u || ((v[7]^kebab_gmu_root_ledger.cmd)&KebabGmuRootInvariantMask(7)) ||
 ((v[8]^kebab_gmu_root_ledger.cfg)&0x10371fu) || !(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u){e=11;goto done;}
 if(kebab_gmu_root_children==UINT32_MAX){e=75;goto done;}
 configured=1;kebab_gpu_vm_io=*io;memset(&kebab_gpu_vm_clocks,0,sizeof kebab_gpu_vm_clocks);KebabGmuBootInit(&kebab_gpu_vm_clock_evidence,2);kebab_gpu_vm_clock_evidence.firmware_start_requested=1;
 kebab_gpu_vm_clocks.io=&kebab_gpu_vm_io;kebab_gpu_vm_clocks.ledger=&kebab_gmu_boot_ledger;kebab_gpu_vm_clocks.r=&kebab_gpu_vm_clock_evidence;
 kebab_gpu_vm_clocks.gcc_cfg=v[0];kebab_gpu_vm_clocks.gcc_reset=v[1];kebab_gpu_vm_clocks.gdscr=kebab_gpu_cc_ledger.gdscr_control;
 kebab_gpu_vm_clocks.ahb=kebab_gpu_cc_ledger.ahb_control;kebab_gpu_vm_clocks.gx_gmu=v[6]&0x0fffffffu;
 e=io->read32(io->cookie,KGB_GCC,0x7100c,&kebab_gpu_vm_clocks.memnoc);if(e)goto done;kebab_gpu_vm_clocks.memnoc&=0x0fffffffu;
 e=KebabGmuBootClockGates(&kebab_gpu_vm_clocks,1);if(e)goto done;
 t->cold=cold;t->epoch=epoch;t->generation=++kebab_gpu_vm_generation;t->held=1;t->published=0;kebab_gpu_vm_identity=t;++kebab_gmu_root_children;e=0;
done:
 if(e&&!t->held&&configured){memset(&kebab_gpu_vm_io,0,sizeof kebab_gpu_vm_io);kebab_gpu_vm_clocks.io=NULL;}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_vm_check(const KebabA650GpuVmOwnerToken*t)
{
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=kebab_gpu_vm_token_locked(t);if(!e)e=KebabGmuBootClockGates(&kebab_gpu_vm_clocks,1);
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_vm_publish(KebabA650GpuVmOwnerToken*t)
{
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=kebab_gpu_vm_token_locked(t);if(!e)e=KebabGmuBootClockGates(&kebab_gpu_vm_clocks,1);if(!e)t->published=1;
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_vm_end(KebabA650GpuVmOwnerToken*t)
{
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=kebab_gpu_vm_token_locked(t);if(!e&&t->published)e=16;
 if(!e){--kebab_gmu_root_children;memset(t,0,sizeof *t);kebab_gpu_vm_identity=NULL;memset(&kebab_gpu_vm_io,0,sizeof kebab_gpu_vm_io);kebab_gpu_vm_clocks.io=NULL;}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
