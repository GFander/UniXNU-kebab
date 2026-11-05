/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_HFI_INIT_HOLD_H
#define KEBAB_NATIVE_GMU_HFI_INIT_HOLD_H
/* Same TU after the unchanged FirmwareHold. Publication cache only; never
 * an authority for live clocks, namespace, IRQ or indirect FW power votes. */
#include "KebabNativeGmuFirmwareHold.h"
#include "KebabNativeGmuHfiInit.h"
static uint32_t kebab_gmu_init_hold_state;
static int kebab_gmu_init_hold_result=EAGAIN;
static KebabGmuHfiInitRecord *kebab_gmu_init_hold_cache;
static int kebab_gmu_init_hold_worker(void *cookie)
{return kebab_gmu_fw_hold_worker(cookie);}
extern "C" void kebab_gmu_initialize_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gmu_init_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gmu_init_hold_result:EBUSY;
  printf("KEBAB GMUINIT: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int e=kebab_gmu_init_hold_worker(NULL);KebabGmuHfiInitRecord *r=NULL;
 if(!e){r=(KebabGmuHfiInitRecord *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else {
  KebabGmuHfiInitRecordInit(r);
  e=kebab_gmu_init_hold_worker(NULL);
  /* Both the old private worker completion and the actual native VERSION
   * owner must be healthy. No CAP cache bytes or copied token grant entry. */
  if(!e && (__atomic_load_n(&kebab_gmu_hfi_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_hfi_hold_result ||
     __atomic_load_n(&kebab_native_gmu_hfi_once,__ATOMIC_ACQUIRE)!=2 || kebab_native_gmu_hfi_result.result ||
     !kebab_native_gmu_hfi_result.version_verified || kebab_native_gmu_hfi_result.returned_version!=0x20010008 ||
     __atomic_load_n(&kebab_gmu_irq_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_irq_hold_result ||
     kebab_native_gmu_firmware.irq!=&kebab_gmu_irq_hold_token || kebab_gpu_gmu_irq_healthy(&kebab_gmu_irq_hold_token)))e=95;
  if(!e)e=kebab_native_gmu_hfi_initialize(NULL,kebab_gmu_init_hold_worker,r);
  r->io.result=e;
 }}
 /* Publish failures too. Old CAP28/29/30 caches and private live books are
  * never replaced by this once cache; a repeat cannot execute another CORE. */
 kebab_gmu_init_hold_cache=r;kebab_gmu_init_hold_result=e;
 __atomic_store_n(&kebab_gmu_init_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUINIT: end; result=%d; phase=%u; cached=0\n",e,r?r->phase:0);
}
static int kebab_native_gmu_init_hold_snapshot(KebabGmuHfiInitRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_init_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_init_hold_cache)memcpy(out,kebab_gmu_init_hold_cache,sizeof *out);
 else {KebabGmuHfiInitRecordInit(out);out->io.result=kebab_gmu_init_hold_result;}return 0;
}
#endif
