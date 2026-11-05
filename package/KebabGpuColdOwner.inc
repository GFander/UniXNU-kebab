/* SPDX-License-Identifier: MIT */
/* Include in canonical VRM TU AFTER actual CC/GMU root/Bus Owner.inc.
 * No second busy lock, child counter, rail owner or hardware writer. */
#if defined(KERNEL)
#include <kern/KebabGpuColdOwner.h>
#include <kern/KebabGpuFirstNativeBinding.h>
#else
#include "KebabGpuColdOwner.h"
#include "KebabGpuFirstNativeBinding.h"
#endif
static KebabGpuColdToken *kebab_gpu_cold_identity;
static KebabGpuColdIo kebab_gpu_cold_io;
static uint64_t kebab_gpu_cold_generation;
static uint32_t kebab_gpu_cold_quarantined;
int kebab_gpu_cold_token_locked(const KebabGpuColdToken *t,unsigned cold)
{
 if(!t || t!=kebab_gpu_cold_identity || !t->held || !t->generation ||
 t->generation!=kebab_gpu_cold_generation || kebab_gpu_cold_quarantined ||
 !KebabGpuFirstColdBootContract || !KFPolicyBound(&KebabGpuFirstNativePolicy) ||
 !kebab_gpu_bus_parent_ready() || !kebab_gpu_bus_ledger.held || kebab_gpu_bus_token_locked((KebabGpuBusToken *)&t->bus))return 95;
 if(cold && t->started)return 95;return 0;
}
static int kebab_gpu_cold_verify_locked(KebabGpuColdToken *t,unsigned cold)
{
 uint32_t a,b,c,d;int e=kebab_gpu_cold_token_locked(t,cold);if(e)return e;
 e=kebab_gpu_bus_verify_locked();if(e)return e;
 if(!cold)return 0;if(!kebab_gpu_cold_io.read32)return 95;
 e=kebab_gpu_cold_io.read32(kebab_gpu_cold_io.cookie,0x1098,&a);if(e)return e;
 e=kebab_gpu_cold_io.read32(kebab_gpu_cold_io.cookie,0x1064,&b);if(e)return e;
 e=kebab_gpu_cold_io.read32(kebab_gpu_cold_io.cookie,0x1098,&c);if(e)return e;
 e=kebab_gpu_cold_io.read32(kebab_gpu_cold_io.cookie,0x1064,&d);if(e)return e;
 /* Both GMU producer branches quiet in the supported cold lifecycle. Raw
  * reserved/control fields are not interpreted as reset or drain status. */
 if((a&0x80000001u)!=0x80000000u || (b&0x80000001u)!=0x80000000u ||
 (c&0x80000001u)!=0x80000000u || (d&0x80000001u)!=0x80000000u)return 11;
 return 0;
}
int kebab_rpmh_gpu_cold_acquire(KebabGpuColdToken *t,const KebabGpuColdIo *io)
{
 int e=95;if(!t || !io || !io->read32)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 if(!KFPolicyBound(&KebabGpuFirstNativePolicy) || !KebabGpuFirstColdBootContract || kebab_gpu_cold_quarantined)goto done;
 if(kebab_gpu_cold_identity || t->held || t->generation || kebab_gpu_cold_generation==UINT64_MAX){e=16;goto done;}
 e=kebab_gpu_bus_token_locked(&t->bus);if(e)goto done;e=kebab_gpu_bus_verify_locked();if(e)goto done;
 t->held=1;t->generation=++kebab_gpu_cold_generation;kebab_gpu_cold_identity=t;kebab_gpu_cold_io=*io;
 e=kebab_gpu_cold_verify_locked(t,1);
done:__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_cold_check(KebabGpuColdToken *t,unsigned flags)
{
 int e;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 e=kebab_gpu_cold_verify_locked(t,!!(flags&(KF_COLD|KF_DRAINED)));
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_rpmh_gpu_cold_drain(KebabGpuColdToken *t)
{
 /* Only the reviewed never-started cold lifecycle can grant this. Once a
  * branch start occurs, real GMU stop/drain must be added; no fake reset flag. */
 return kebab_rpmh_gpu_cold_check(t,KF_ACCESS|KF_COLD|KF_DRAINED);
}
int kebab_rpmh_gpu_cold_end(KebabGpuColdToken *t)
{
 int e;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 e=kebab_gpu_cold_token_locked(t,1);
 if(!e){t->held=0;kebab_gpu_cold_identity=NULL;memset(&kebab_gpu_cold_io,0,sizeof kebab_gpu_cold_io);}
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_gpu_cold_mark_started_locked(KebabGpuColdToken *t)
{
 int e=kebab_gpu_cold_token_locked(t,1);if(e)return e;t->started=1;return 0;
}
