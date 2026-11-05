/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_COLD_HOLD_H
#define KEBAB_NATIVE_GMU_COLD_HOLD_H
#include <sys/errno.h>
#include "KebabNativeGmuBoot.h"
static uint32_t kebab_gmu_cold_hold_state;
static int kebab_gmu_cold_hold_result=EAGAIN;
static KebabGmuBootRecord *kebab_gmu_cold_hold_record;
extern "C" void kebab_gmu_cold_prepare(void)
{
 uint32_t expected=0;if(!__atomic_compare_exchange_n(&kebab_gmu_cold_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gmu_cold_hold_result:EBUSY;const KebabGmuBootRecord *r=expected==2?kebab_gmu_cold_hold_record:NULL;
  printf("KEBAB GMUCOLD: end; result=%d; phase=%u; state=%u; cached=1\n",e,r?r->phase:0,expected);return;
 }
 int e=kebab_gpu_gcc_worker_access(NULL);if(!e && (ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()))e=95;KebabGmuBootRecord *r=NULL;
 if(!e){r=(KebabGmuBootRecord *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else {KebabGmuBootInit(r,KebabGmuBootResetLoad);e=kebab_native_gmu_boot(KebabGmuBootResetLoad,NULL,kebab_gpu_gcc_worker_access,r);r->result=(uint32_t)e;}}
 kebab_gmu_cold_hold_record=r;kebab_gmu_cold_hold_result=e;__atomic_store_n(&kebab_gmu_cold_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUCOLD: end; result=%d; phase=%u; cached=0; RESET_HELD only\n",e,r?r->phase:0);
}
static int kebab_native_gmu_cold_hold_snapshot(KebabGmuBootRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_cold_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_cold_hold_record)memcpy(out,kebab_gmu_cold_hold_record,sizeof *out);else {KebabGmuBootInit(out,KebabGmuBootResetLoad);out->result=(uint32_t)kebab_gmu_cold_hold_result;}
 return 0;
}
#endif
