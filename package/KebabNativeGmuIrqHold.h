/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_IRQ_HOLD_H
#define KEBAB_NATIVE_GMU_IRQ_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
extern "C" int get_preemption_level(void);
#if defined(KERNEL)
#include <kern/KebabGmuIrqOwner.h>
#include <kern/KebabGmuIrqBinding.h>
#include <kern/KebabGpuFirstAttachNative.h>
#else
#include "KebabGmuIrqOwner.h"
#include "KebabGmuIrqBinding.h"
#include "KebabGpuFirstAttachNative.h"
#endif
static uint32_t kebab_gmu_irq_hold_state;
static int kebab_gmu_irq_hold_result=EAGAIN;
static KebabGmuIrqRecord *kebab_gmu_irq_hold_record;
static KebabGmuIrqToken kebab_gmu_irq_hold_token;
/* GIC RO qualification before a parent pin or any IRQ write. The native
 * owner repeats these exact live checks; RO is never substituted for lease. */
static int kebab_gmu_irq_ro_eligible(const KebabGmuIrqRecord *r)
{
 return r && (r->valid_mask&1) && (r->gic_ctlr&0x80000012u)==0x12u && 32*((r->gic_typer&31)+1)>337 &&
  !((r->enable|r->pending|r->active)&0x30000u) && !(r->config&0xau) && (r->priorities&255u) && (r->priorities&0xff00u);
}
extern "C" void kebab_gmu_irq_prepare(void)
{
 uint32_t expected=0;if(!__atomic_compare_exchange_n(&kebab_gmu_irq_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gmu_irq_hold_result:EBUSY;const KebabGmuIrqRecord *r=expected==2?kebab_gmu_irq_hold_record:NULL;
  printf("KEBAB GMUIRQ: end; result=%d; armed=%u; state=%u; cached=1\n",e,r?r->armed:0,expected);return;
 }
 int e=kebab_gpu_gcc_worker_access(NULL);if(!e && (ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()))e=95;KebabGmuIrqRecord *r=NULL;
 if(!e){r=(KebabGmuIrqRecord *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else {
  memset(r,0,sizeof *r);e=kebab_gpu_gmu_irq_snapshot(r);
  if(!e){
   KebabGmuBootRecord cold;int ready=kebab_native_gmu_cold_hold_snapshot(&cold);
   if(!KebabGmuIrqNativeQualified || !kebab_gmu_irq_ro_eligible(r) || ready || cold.result || cold.phase!=KGB_RESET_HELD_TCM)e=95;
   else {uint64_t epoch=0;const KebabGpuColdToken *identity=kebab_gpu_first_run_lease(&epoch);
    if(!identity || !epoch || epoch!=cold.namespace_epoch)e=95;
    else {e=kebab_gpu_gmu_irq_acquire(identity,epoch,&kebab_gmu_irq_hold_token);int copy=kebab_gpu_gmu_irq_snapshot(r);if(!e && copy)e=copy;}}
  }
  r->result=(uint32_t)e;
 }}
 kebab_gmu_irq_hold_record=r;kebab_gmu_irq_hold_result=e;__atomic_store_n(&kebab_gmu_irq_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUIRQ: end; result=%d; armed=%u; cached=0; no firmware release\n",e,r?r->armed:0);
}
static int kebab_native_gmu_irq_hold_snapshot(KebabGmuIrqRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_irq_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_irq_hold_record)memcpy(out,kebab_gmu_irq_hold_record,sizeof *out);else {memset(out,0,sizeof *out);out->magic=0x494d474b;out->schema=1;out->bytes=sizeof *out;out->intid_hfi=336;out->intid_ao=337;out->result=(uint32_t)kebab_gmu_irq_hold_result;}
 return 0;
}
#endif
