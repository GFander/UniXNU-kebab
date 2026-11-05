/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_ROOT_HOLD_H
#define KEBAB_NATIVE_GMU_ROOT_HOLD_H
#include <sys/errno.h>
#include <arm/machine_routines.h>
#include "KebabNativeGmuRoot.h"
/* Include once in the normal framebuffer diagnostic TU, after the existing
 * kebab_gpu_gcc_worker_access definition. No user-supplied action/binding.
 * The one acquired reference, partial/quarantined reference and receipt
 * remain owned for this boot. There is deliberately no release/reset API. */
extern "C" int get_preemption_level(void);
static_assert(sizeof(KebabGmuRootRecord)==512,"fixed GPU power hold receipt");
enum {KebabGmuRootHoldNotRun=0,KebabGmuRootHoldBusy=1,KebabGmuRootHoldDone=2};
static uint32_t kebab_gmu_root_hold_state;
static int kebab_gmu_root_hold_result=EAGAIN;
static KebabGmuRootRecord *kebab_gmu_root_hold_record;

static int kebab_gmu_root_worker_check(void *cookie)
{
 int error=kebab_gpu_gcc_worker_access(cookie);
 if(error)return error;
 if(ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return 0;
}

/* GMUROOT only: claiming even a refused/ENOMEM attempt prevents retries.
 * Only pointers/scalars are on the kernel stack, never the 512B record. */
extern "C" void kebab_gmu_root_prepare(void)
{
 uint32_t expected=KebabGmuRootHoldNotRun;
 if(!__atomic_compare_exchange_n(&kebab_gmu_root_hold_state,&expected,
    KebabGmuRootHoldBusy,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  int result=expected==KebabGmuRootHoldDone?kebab_gmu_root_hold_result:EBUSY;
  printf("KEBAB GMUROOT: end; result=%d; state=%u; cached=1\n",result,expected);
  return;
 }
 int result=kebab_gmu_root_worker_check(NULL);
 KebabGmuRootRecord *record=NULL;
 if(!result) {
  record=(KebabGmuRootRecord *)IOMalloc(sizeof(*record));
  if(!record)result=ENOMEM;
  else {
   KebabGmuRootInit(record,KebabGmuRootHold200);
   result=kebab_native_gmu_root(KebabGmuRootHold200,NULL,kebab_gmu_root_worker_check,record);
  }
 }
 /* Publish only after native EnableHold has returned. Never free on error:
  * a partial ACK/quarantine may still own a reference or uncertain vote. */
 kebab_gmu_root_hold_record=record;
 kebab_gmu_root_hold_result=result;
 __atomic_store_n(&kebab_gmu_root_hold_state,KebabGmuRootHoldDone,__ATOMIC_RELEASE);
 printf("KEBAB GMUROOT: end; result=%d; state=%u; cached=0\n",result,(unsigned)KebabGmuRootHoldDone);
}

/* CAP21 copy only. Return 0 means a complete immutable receipt was copied,
 * including native failures; out->result is never converted to success.
 * Before completion return EAGAIN without touching out. If context/heap
 * failed before any native call, reconstruct the fixed zero/error receipt
 * from the immutable scalar terminal result, without allocating or IO. */
static int kebab_native_gmu_root_hold_snapshot(KebabGmuRootRecord *out)
{
 if(!out)return EINVAL;
 if(__atomic_load_n(&kebab_gmu_root_hold_state,__ATOMIC_ACQUIRE)!=KebabGmuRootHoldDone)return EAGAIN;
 if(kebab_gmu_root_hold_record)memcpy(out,kebab_gmu_root_hold_record,sizeof(*out));
 else {KebabGmuRootInit(out,KebabGmuRootHold200);out->result=(uint32_t)kebab_gmu_root_hold_result;}
 return 0;
}
#endif
