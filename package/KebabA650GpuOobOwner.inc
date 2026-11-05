/* SPDX-License-Identifier: MIT */
/* Append after actual586 VM owner in the one canonical VRM translation unit. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabA650GpuOobOwner.h>
#include <kern/KebabA650GpuOobBinding.h>
#else
#include "KebabA650GpuOobOwner.h"
#include "KebabA650GpuOobBinding.h"
#endif
static KebabA650GpuOobOwnerToken *kebab_gpu_oob_identity;
static uint64_t kebab_gpu_oob_generation;
static uint32_t kebab_gpu_oob_called;
static KebabA650GpuOobBook kebab_gpu_oob_book;
static int kebab_gpu_oob_source_bound(void)
{unsigned any=0;for(unsigned n=0;n<32;n++)any|=KebabA650GpuOobSourceContract[n];return KebabA650GpuOobNativeQualified==1&&any;}
int kebab_rpmh_gpu_oob_check_locked(const KebabA650GpuOobOwnerToken*t)
{
 if(!kebab_gpu_oob_source_bound() || !__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) ||
 !t || t!=kebab_gpu_oob_identity || !t->pinned || t->quarantined || !t->generation || t->generation!=kebab_gpu_oob_generation ||
 t->vm!=kebab_gpu_vm_identity || !t->vm->published || kebab_gmu_root_children<4 || kebab_gpu_oob_book.quarantined)return 95;
 int e=kebab_gpu_vm_token_locked(t->vm);if(e)return e;
 e=KebabGmuBootClockGates(&kebab_gpu_vm_clocks,1);if(e)return e;
 return kebab_a650_gpu_oob_vm_live_check_locked(t->vm);
}
int kebab_rpmh_gpu_oob_hold(KebabA650GpuOobOwnerToken*t,const KebabA650GpuVmOwnerToken*vm,
 const KebabA650GpuOobOwnerIo*io,KebabA650GpuOobRecord*out)
{
 int e=95,pinned=0;uint64_t begin=0,end=0;
 if(!out)return 22;KebabA650GpuOobInit(out);
 if(!kebab_gpu_oob_source_bound()){out->result=95;out->phase=KHO_FAILED;return 95;}
 if(!t || !vm || !io || !io->core.gate || !io->core.read32 || !io->core.write32 || !io->core.barrier || !io->core.delay_us || !io->core.now_us || !io->snapshot_vm){out->result=22;out->phase=KHO_FAILED;return 22;}
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;out->phase=KHO_FAILED;return 16;}
 if(t->pinned || t->generation || kebab_gpu_oob_identity || kebab_gpu_oob_called || kebab_gpu_oob_generation==UINT64_MAX){e=16;goto done;}
 if(vm!=kebab_gpu_vm_identity || !vm->published){e=95;goto done;}
 e=kebab_gpu_vm_token_locked(vm);if(e)goto done;
 e=KebabGmuBootClockGates(&kebab_gpu_vm_clocks,1);if(e)goto done;
 e=kebab_a650_gpu_oob_vm_ready_locked(vm);if(e)goto done;
 if(kebab_gmu_root_children==UINT32_MAX){e=75;goto done;}
 /* Reserve the real fourth Root child before any new physical observation.
  * Once reserved, every failure retains all Root/BO/VM/map pins. There is no
  * guessed OOB release, producer drain, reset or namespace restoration. */
 t->vm=vm;t->generation=++kebab_gpu_oob_generation;t->pinned=1;t->quarantined=0;
 kebab_gpu_oob_identity=t;++kebab_gmu_root_children;kebab_gpu_oob_called=1;pinned=1;
 begin=io->core.now_us(io->core.cookie);out->epoch=vm->epoch;
 e=io->snapshot_vm(io->core.cookie,0);
 if(!e){KebabA650GpuOobContext c={&io->core,&kebab_gpu_oob_book,out,0,0,0};e=KebabA650GpuOobHold(&c,1,vm->epoch);}
 /* Preserve a fresh namespace diagnostic even when the OOB attempt failed.
  * This is readonly and cannot replace the original result or retry writes. */
 {int observed=io->snapshot_vm(io->core.cookie,1);if(!e&&observed)e=observed;}
 end=io->core.now_us(io->core.cookie);out->elapsed_us=end>=begin?end-begin:UINT64_MAX;
 if(!e&&out->elapsed_us>=100000)e=110;
 if(!e)e=kebab_rpmh_gpu_oob_check_locked(t);
 /* Include the final real Root/clock/SMMU gate in the same wall-clock bound. */
 end=io->core.now_us(io->core.cookie);out->elapsed_us=end>=begin?end-begin:UINT64_MAX;
 if(!e&&out->elapsed_us>=100000)e=110;
done:
 if(e){out->primary_error=(uint32_t)e;out->phase=pinned?KHO_QUARANTINED:KHO_FAILED;
  if(pinned){t->quarantined=1;kebab_gpu_oob_book.quarantined=1;out->quarantined=1;out->held=0;
   kebab_a650_gpu_oob_vm_quarantine_locked(vm);kebab_gmu_boot_quarantine((unsigned)e);}}
 out->result=(uint32_t)e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
