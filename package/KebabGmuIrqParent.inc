/* SPDX-License-Identifier: MIT */
/* Include after KebabGmuBootOwner.inc in canonical kebab_rpmh_vrm.c. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuIrqOwner.h>
#include <kern/KebabGmuIrqBinding.h>
#else
#include "KebabGmuIrqOwner.h"
#include "KebabGmuIrqBinding.h"
#endif
static KebabGmuIrqToken *kebab_gmu_irq_parent_identity;
static uint64_t kebab_gmu_irq_parent_generation;
int kebab_gpu_gmu_irq_parent_pin(const KebabGpuColdToken *cold,uint64_t epoch,KebabGmuIrqToken *token)
{
 if(!cold || !epoch || !token)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 int e=95;
 if(!KebabGmuIrqNativeQualified || kebab_gmu_irq_parent_identity || token->held)goto done;
 e=kebab_gpu_gmu_boot_reset_held_locked(cold,epoch);if(e)goto done;
 if(kebab_gmu_root_children==UINT32_MAX || kebab_gmu_irq_parent_generation==UINT64_MAX){e=75;goto done;}
 /* Only a root-clock child is needed. Boot owner already has immutable
  * rail/CC/BUS/namespace pins; do not invent a second rail reference. */
 ++kebab_gmu_root_children;++kebab_gmu_irq_parent_generation;
 token->cold=cold;token->epoch=epoch;token->generation=kebab_gmu_irq_parent_generation;token->reserved=0;
 kebab_gmu_irq_parent_identity=token;__atomic_store_n(&token->held,1,__ATOMIC_RELEASE);e=0;
done:__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_gpu_gmu_irq_parent_check_locked(const KebabGmuIrqToken *token,const KebabGpuColdToken *cold,uint64_t epoch)
{
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) || !token || token!=kebab_gmu_irq_parent_identity ||
  !__atomic_load_n(&token->held,__ATOMIC_ACQUIRE) || token->reserved || token->cold!=cold || token->epoch!=epoch ||
  token->generation!=kebab_gmu_irq_parent_generation || !kebab_gmu_root_children)return 95;
 /* Launch moves RESET_HELD to FW_RUNNING, so do not require reset-held
  * forever. The permanent boot child and same private identity remain. */
 return kebab_gpu_gmu_boot_gate_locked(cold,epoch);
}
