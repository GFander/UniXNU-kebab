/* SPDX-License-Identifier: MIT */
/* Append after actual587 OOB owner in the ONE canonical VRM unit. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabA650CpOwner.h>
#include <kern/KebabA650CpBinding.h>
#else
#include "KebabA650CpOwner.h"
#include "KebabA650CpBinding.h"
#endif
static KebabA650CpOwnerToken *kebab_gpu_cp_identity;
static uint64_t kebab_gpu_cp_generation;
static uint32_t kebab_gpu_cp_called;
static KebabA650CpBook kebab_gpu_cp_book;
static KebabA650CpRecord *kebab_gpu_cp_record;
static int kebab_gpu_cp_source_bound(void)
{unsigned any=0;for(unsigned n=0;n<32;n++)any|=KebabA650CpSourceContract[n];return KebabA650CpNativeQualified==1&&any;}
int kebab_rpmh_gpu_cp_check_locked(const KebabA650CpOwnerToken*t)
{
 if(!kebab_gpu_cp_source_bound() || !__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) ||
 !t || t!=kebab_gpu_cp_identity || !t->pinned || t->quarantined || !t->generation || t->generation!=kebab_gpu_cp_generation ||
 t->oob!=kebab_gpu_oob_identity || kebab_gmu_root_children<5 || kebab_gpu_cp_book.quarantined || !kebab_gpu_cp_record)return 95;
 int e=kebab_rpmh_gpu_oob_check_locked(t->oob);if(e)return e;
 e=kebab_a650_gpu_cp_context_ready_locked(t->oob);if(e)return e;
 return kebab_a650_gpu_cp_power_ready_locked(t->oob,kebab_gpu_cp_record->phase);
}
int kebab_rpmh_gpu_cp_first_execution(KebabA650CpOwnerToken*t,const KebabA650GpuOobOwnerToken*oob,
 const KebabA650CpOwnerIo*io,KebabA650CpRecord*out)
{
 int e=95,pinned=0;uint64_t begin=0,end=0;
 if(!out)return 22;KebabA650CpInit(out);
 if(!kebab_gpu_cp_source_bound()){out->result=95;out->phase=KCP_FAILED;return 95;}
 if(!t || !oob || !io || !io->core.gate || !io->core.read32 || !io->core.write32 || !io->core.barrier ||
 !io->core.delay_us || !io->core.now_us || !io->core.prepare_cpu || !io->core.copy_words || !io->core.publish_commands || !io->core.read_nc || !io->snapshot_vm){out->result=22;out->phase=KCP_FAILED;return 22;}
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;out->phase=KCP_FAILED;return 16;}
 if(t->pinned || t->generation || kebab_gpu_cp_identity || kebab_gpu_cp_called || kebab_gpu_cp_generation==UINT64_MAX){e=16;goto done;}
 if(oob!=kebab_gpu_oob_identity || !oob->pinned || oob->quarantined){e=95;goto done;}
 e=kebab_rpmh_gpu_oob_check_locked(oob);if(e)goto done;
 e=kebab_a650_gpu_cp_context_ready_locked(oob);if(e)goto done;
 e=kebab_a650_gpu_cp_power_ready_locked(oob,KCP_EMPTY);if(e)goto done;
 if(kebab_gmu_root_children==UINT32_MAX){e=75;goto done;}
 /* Fifth actual Root child pins the existing OOB/VM/BO chain. No new BO
  * holder, namespace or table change; GMU bytes remain unchanged.
  * The new finite contract opens only command/SQE data CPU writes while
  * the CP is freshly quiet, then cleans before first fetch. */
 t->oob=oob;t->generation=++kebab_gpu_cp_generation;t->pinned=1;t->quarantined=0;
 kebab_gpu_cp_identity=t;kebab_gpu_cp_record=out;kebab_gpu_cp_called=1;++kebab_gmu_root_children;pinned=1;
 begin=io->core.now_us(io->core.cookie);
 e=io->snapshot_vm(io->core.cookie,0);
 if(!e){KebabA650CpContext context={&io->core,&kebab_gpu_cp_book,out,0,0,0};e=KebabA650CpFirstExecution(&context,1,oob->vm->epoch);}
 {int observed=io->snapshot_vm(io->core.cookie,1);if(!e&&observed)e=observed;if(!e)out->namespace_unchanged=1;}
 if(!e)e=kebab_rpmh_gpu_cp_check_locked(t);
 end=io->core.now_us(io->core.cookie);out->elapsed_us=end>=begin?end-begin:UINT64_MAX;
 if(!e&&out->elapsed_us>=500000)e=110;
done:
 if(e){out->primary_error=(uint32_t)e;out->phase=pinned?KCP_QUARANTINED:KCP_FAILED;
  if(pinned){t->quarantined=1;kebab_gpu_cp_book.quarantined=1;out->quarantined=1;out->held=0;
   kebab_a650_gpu_cp_quarantine_locked(oob);kebab_gmu_boot_quarantine((unsigned)e);}}
 out->result=(uint32_t)e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}

/* The completed CP observation is immutable. A render failure after the
 * first owned mutation quarantines the live chain without editing CAP37. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabA650RenderBinding.h>
#else
#include "KebabA650RenderBinding.h"
#endif

extern int kebab_a650_gpu_render_locked(uint32_t *);
extern int kebab_a650_zap_render_gate(void);

int
kebab_rpmh_gpu_cp_first_render(KebabA650CpOwnerToken *token, uint32_t *touched)
{
    if (!touched || *touched || !KARSourceBound() || kebab_a650_zap_render_gate()) {
        return 95;
    }
    if (__atomic_exchange_n(&kebab_rpmh_global_lock.busy, 1, __ATOMIC_ACQ_REL)) {
        return 16;
    }
    int error = kebab_rpmh_gpu_cp_check_locked(token);
    if (!error && (!kebab_gpu_cp_book.complete || !kebab_gpu_cp_record->held ||
        kebab_gpu_cp_record->phase != KCP_EXECUTED || kebab_gpu_cp_record->result ||
        kebab_gpu_cp_record->epoch != 1 || kebab_gpu_cp_record->final_fence != 2 ||
        kebab_gpu_cp_record->final_rptr != KCP_FINAL_WPTR ||
        !kebab_gpu_cp_record->GPU_DMA_observed || !kebab_gpu_cp_record->namespace_unchanged)) {
        error = 95;
    }
    if (!error) {
        error = kebab_a650_gpu_render_locked(touched);
    }
    if (error && *touched) {
        token->quarantined = 1;
        kebab_gpu_cp_book.quarantined = 1;
        kebab_a650_gpu_cp_quarantine_locked(token->oob);
        kebab_gmu_boot_quarantine((unsigned)error);
    }
    __atomic_store_n(&kebab_rpmh_global_lock.busy, 0, __ATOMIC_RELEASE);
    return error;
}

/* Read-only, bounded normal-context inventory. This does not open the
 * existing PAS13 namespace mutation gate and cannot issue a secure call. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/kebab_low_dma.h>
#else
#include "kebab_low_dma.h"
#endif
extern int kebab_a650_pas13_resources_locked(struct kebab_pas13_resources_observation *);
int kebab_rpmh_gpu_cp_pas13_resources(KebabA650CpOwnerToken *token,struct kebab_pas13_resources_observation *out)
{
 if(!out)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int error=kebab_rpmh_gpu_cp_check_locked(token);
 if(!error && (!kebab_gpu_cp_book.complete || !kebab_gpu_cp_record->held ||
  kebab_gpu_cp_record->phase!=KCP_EXECUTED || kebab_gpu_cp_record->result ||
  kebab_gpu_cp_record->epoch!=1 || kebab_gpu_cp_record->final_fence!=2 ||
  kebab_gpu_cp_record->final_rptr!=KCP_FINAL_WPTR || !kebab_gpu_cp_record->GPU_DMA_observed ||
  !kebab_gpu_cp_record->namespace_unchanged))error=95;
 if(!error)error=kebab_a650_pas13_resources_locked(out);
 /* No write occurred. A refusal does not revoke or free the live GPU chain. */
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return error;
}

/* Extra retained child of the real GMU root. It prevents parent teardown
 * across the PAS lifecycle without fabricating a VM or CP token. */
static const KebabGpuColdToken *kebab_gpu_zap_identity;
static uint64_t kebab_gpu_zap_epoch;
extern int kebab_a650_zap_prevm_locked(void);
extern int kebab_a650_zap_postcp_locked(unsigned);
int kebab_rpmh_gpu_zap_prevm(const KebabGpuColdToken *cold,uint64_t epoch,unsigned pin)
{
 if(!cold || epoch!=1 || pin>1)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=95;
 if(kebab_gpu_vm_identity || kebab_gpu_cp_identity ||
  (pin?(kebab_gpu_zap_identity!=NULL):(kebab_gpu_zap_identity!=cold || kebab_gpu_zap_epoch!=epoch)))goto done;
 e=kebab_gpu_vm_parent_locked(cold,epoch);if(e)goto done;
 if(pin && kebab_gmu_root_children==UINT32_MAX){e=75;goto done;}
 e=kebab_a650_zap_prevm_locked();if(e)goto done;
 if(pin){kebab_gpu_zap_identity=cold;kebab_gpu_zap_epoch=epoch;++kebab_gmu_root_children;}
done:
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_zap_postcp(const KebabGpuColdToken *cold,uint64_t epoch,unsigned final)
{
 if(!cold || epoch!=1 || final>1)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=95;
 if(kebab_gpu_zap_identity!=cold || kebab_gpu_zap_epoch!=epoch)goto done;
 e=kebab_rpmh_gpu_cp_check_locked(kebab_gpu_cp_identity);if(e)goto done;
 if(!kebab_gpu_cp_book.complete || !kebab_gpu_cp_record->held ||
  kebab_gpu_cp_record->phase!=KCP_EXECUTED || kebab_gpu_cp_record->result ||
  kebab_gpu_cp_record->epoch!=1 || kebab_gpu_cp_record->final_fence!=2 ||
  kebab_gpu_cp_record->final_rptr!=KCP_FINAL_WPTR || !kebab_gpu_cp_record->GPU_DMA_observed ||
  !kebab_gpu_cp_record->namespace_unchanged){e=95;goto done;}
 e=kebab_a650_zap_postcp_locked(final);
done:
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_zap_quarantine(unsigned error)
{
 if(!error)return 22;
 /* Canonical busy is only held across bounded checks by these owners. Do
  * not spin or fabricate quarantine success if another owner holds it. */
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 if(kebab_gpu_zap_identity){
  if(kebab_gpu_cp_identity){kebab_gpu_cp_identity->quarantined=1;kebab_gpu_cp_book.quarantined=1;
   kebab_a650_gpu_cp_quarantine_locked(kebab_gpu_cp_identity->oob);}
  kebab_gmu_boot_quarantine(error);
 }
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return 0;
}

/* Distinct secure-exit submission under the original completed CP owner.
 * Any mutation failure quarantines the live chain without rewriting CAP37
 * or CAP45 and without reclaiming protected PAS memory. */
extern int kebab_a650_gpu_secure_exit_locked(uint32_t *);
int kebab_rpmh_gpu_cp_secure_exit(KebabA650CpOwnerToken *token,uint32_t *touched)
{
 if(!touched || *touched || !KARSourceBound())return 95;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int error=kebab_rpmh_gpu_cp_check_locked(token);
 if(!error && (!kebab_gpu_cp_book.complete || !kebab_gpu_cp_record->held ||
  kebab_gpu_cp_record->phase!=KCP_EXECUTED || kebab_gpu_cp_record->result ||
  kebab_gpu_cp_record->epoch!=1 || kebab_gpu_cp_record->final_fence!=2 ||
  kebab_gpu_cp_record->final_rptr!=KCP_FINAL_WPTR || !kebab_gpu_cp_record->GPU_DMA_observed ||
  !kebab_gpu_cp_record->namespace_unchanged))error=95;
 if(!error)error=kebab_a650_gpu_secure_exit_locked(touched);
 if(error && *touched){token->quarantined=1;kebab_gpu_cp_book.quarantined=1;
  kebab_a650_gpu_cp_quarantine_locked(token->oob);kebab_gmu_boot_quarantine((unsigned)error);}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return error;
}
