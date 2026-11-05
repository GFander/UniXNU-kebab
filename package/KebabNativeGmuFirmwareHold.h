/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_FIRMWARE_HOLD_H
#define KEBAB_NATIVE_GMU_FIRMWARE_HOLD_H
/* Same TU, after the existing ColdHold and IrqHold. These are publication
 * caches, not the mutable native owners or an authorization source. */
#include "KebabNativeGmuFirmware.h"
#include "KebabNativeGmuHfi.h"
struct KebabGmuFirmwareHoldCache {
 KebabGmuBootRecord firmware;
 KebabGpuLaunchResourceRecord resource;
};
static uint32_t kebab_gmu_fw_hold_state,kebab_gmu_hfi_hold_state;
static int kebab_gmu_fw_hold_result=EAGAIN,kebab_gmu_hfi_hold_result=EAGAIN;
static KebabGmuFirmwareHoldCache *kebab_gmu_fw_hold_cache;
static KebabGmuHfiRecord *kebab_gmu_hfi_hold_cache;
static int kebab_gmu_fw_hold_worker(void *cookie)
{
 int e=kebab_gpu_gcc_worker_access(cookie);
 if(!e && (ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()))e=95;
 return e;
}
static void kebab_gmu_fw_resource_not_run(KebabGpuLaunchResourceRecord *r)
{KLRInit(r);r->result=EAGAIN;r->state=KLR_EMPTY;}
static void kebab_gmu_fw_resource_cache(KebabGpuLaunchResourceRecord *r)
{
 /* No public snapshot, lock, mapping or resource execution here. Native
  * prepare publishes its result before once=2; never copy its live book. */
 uint32_t state=__atomic_load_n(&klr_native_once,__ATOMIC_ACQUIRE);
 if(state==2)memcpy(r,&klr_native_result,sizeof *r);
 else {kebab_gmu_fw_resource_not_run(r);if(state==1){r->result=EBUSY;r->state=KLR_PREPARING;}}
}
extern "C" void kebab_gmu_firmware_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gmu_fw_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gmu_fw_hold_result:EBUSY;
  const KebabGmuFirmwareHoldCache *r=expected==2?kebab_gmu_fw_hold_cache:NULL;
  printf("KEBAB GMUFW: end; result=%d; phase=%u; state=%u; cached=1\n",e,r?r->firmware.phase:0,expected);return;
 }
 int e=kebab_gmu_fw_hold_worker(NULL);KebabGmuFirmwareHoldCache *r=NULL;
 if(!e){r=(KebabGmuFirmwareHoldCache *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else {
  KebabGmuBootInit(&r->firmware,KebabGmuBootLaunch);kebab_gmu_fw_resource_not_run(&r->resource);
  /* Borrow the registered stable object. A copied record/token, CAP result,
   * or a second acquire cannot substitute for this private IRQ identity. */
  if(__atomic_load_n(&kebab_gmu_irq_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_irq_hold_result ||
     kebab_gpu_gmu_irq_healthy(&kebab_gmu_irq_hold_token))e=95;
  else e=kebab_native_gmu_firmware_launch(&kebab_gmu_irq_hold_token,NULL,kebab_gmu_fw_hold_worker,&r->firmware);
  r->firmware.result=e;kebab_gmu_fw_resource_cache(&r->resource);
 }}
 kebab_gmu_fw_hold_cache=r;kebab_gmu_fw_hold_result=e;
 __atomic_store_n(&kebab_gmu_fw_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUFW: end; result=%d; phase=%u; cached=0\n",e,r?r->firmware.phase:0);
}
extern "C" void kebab_gmu_hfi_prepare(void)
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_gmu_hfi_hold_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  int e=expected==2?kebab_gmu_hfi_hold_result:EBUSY;
  printf("KEBAB GMUHFI: end; result=%d; state=%u; cached=1\n",e,expected);return;
 }
 int e=kebab_gmu_fw_hold_worker(NULL);KebabGmuHfiRecord *r=NULL;
 if(!e){r=(KebabGmuHfiRecord *)IOMalloc(sizeof *r);if(!r)e=ENOMEM;else {
  KebabGmuHfiInit(r);
  if(__atomic_load_n(&kebab_gmu_fw_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_fw_hold_result ||
     !kebab_gmu_fw_hold_cache || kebab_gmu_fw_hold_cache->firmware.phase!=KGB_HFI_QUEUES_READY ||
     __atomic_load_n(&kebab_gmu_irq_hold_state,__ATOMIC_ACQUIRE)!=2 || kebab_gmu_irq_hold_result ||
     kebab_gpu_gmu_irq_healthy(&kebab_gmu_irq_hold_token))e=95;
  else e=kebab_native_gmu_hfi_version(NULL,kebab_gmu_fw_hold_worker,r);
  r->result=e;
 }}
 /* The FW/resource publication is immutable even if HFI updates or faults
  * the private live firmware/resource evidence. */
 kebab_gmu_hfi_hold_cache=r;kebab_gmu_hfi_hold_result=e;
 __atomic_store_n(&kebab_gmu_hfi_hold_state,2,__ATOMIC_RELEASE);
 printf("KEBAB GMUHFI: end; result=%d; cached=0\n",e);
}
static int kebab_native_gmu_firmware_hold_snapshot(KebabGmuBootRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_fw_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_fw_hold_cache)memcpy(out,&kebab_gmu_fw_hold_cache->firmware,sizeof *out);
 else {KebabGmuBootInit(out,KebabGmuBootLaunch);out->result=kebab_gmu_fw_hold_result;}return 0;
}
static int kebab_native_gmu_resource_hold_snapshot(KebabGpuLaunchResourceRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_fw_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_fw_hold_cache)memcpy(out,&kebab_gmu_fw_hold_cache->resource,sizeof *out);
 else kebab_gmu_fw_resource_not_run(out);return 0;
}
static int kebab_native_gmu_hfi_hold_snapshot(KebabGmuHfiRecord *out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kebab_gmu_hfi_hold_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kebab_gmu_hfi_hold_cache)memcpy(out,kebab_gmu_hfi_hold_cache,sizeof *out);
 else {KebabGmuHfiInit(out);out->result=kebab_gmu_hfi_hold_result;}return 0;
}
#endif
