/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_GPU_VM_H
#define KEBAB_NATIVE_A650_GPU_VM_H
/* Included at END of the real FirstAttach TU, sharing its exact namespace,
 * parent, native cold token and external mutation gates. No second namespace. */
#include "KebabA650GpuVmCore.h"
#if defined(KERNEL)
#include <kern/KebabA650GpuVmOwner.h>
#include <kern/KebabA650GpuVmBinding.h>
#include <kern/KebabGmuRootCore.h>
#else
#include "KebabA650GpuVmOwner.h"
#include "KebabA650GpuVmBinding.h"
#include "KebabGmuRootCore.h"
#endif
#include <kern/clock.h>
static KVGEngine kvg_engine;
struct KVGNative {
 KebabA650GpuVmOwnerToken owner;KebabA650GpuBackingToken bo;
 IOMemoryDescriptor *clock_memory[4],*registers;IOMemoryMap *clock_mapping[4],*mapping;
 uintptr_t clock[4],base;
};
static KVGNative kvg_native;
static uint32_t kvg_native_once;
extern "C" int kebab_a650_gpu_vm_ready_locked(const KebabA650GpuVmOwnerToken*t)
{
 if(t!=&kvg_native.owner || kvg_engine.space!=&kf_namespace || kvg_engine.parent!=&kf_attachment ||
 KFNamespaceCheckPrivate(&kf_namespace,&kvg_engine.token) || KVGParent(&kvg_engine))return 95;
 if(t->held && (t->cold!=&kf_native.cold || t->epoch!=kvg_engine.token.epoch))return 95;
 return 0;
}
static void KVGNativeClockUnmap(KVGNative*n)
{
 for(unsigned i=4;i;){--i;n->clock[i]=0;if(n->clock_mapping[i]){n->clock_mapping[i]->release();n->clock_mapping[i]=NULL;}if(n->clock_memory[i]){n->clock_memory[i]->release();n->clock_memory[i]=NULL;}}
}
static int KVGNativeClockRead(void*cookie,unsigned region,uint32_t off,uint32_t*out)
{
 auto*n=(KVGNative*)cookie;if(!out || (off&3) || KFNativeWorker(NULL))return 95;
 unsigned i;uint32_t base;
 if(region==KGB_CC){if(!KebabGmuRootAllowed(region,off)&&off!=0x109c)return 22;i=3;base=0;}
 else if(region==KGB_GCC){
  if(!KebabGmuRootAllowed(region,off)&&off!=0x7100c&&off!=0x71154)return 22;
  if(off<0x1000){i=1;base=0;}else if(off>=0x52000&&off<0x53000){i=2;base=0x52000;}else {i=0;base=0x70000;}
 }else return 22;
 if(!n || !n->clock[i])return 95;*out=*(volatile const uint32_t*)(n->clock[i]+off-base);return 0;
}
static int KVGNativeClockBarrier(void*)
{
 if(KFNativeWorker(NULL))return 95;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static uint64_t KVGNativeNow(void*)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int KVGNativeHold(void*cookie,uint32_t*held)
{
 auto*n=(KVGNative*)cookie;*held=0;if(!KebabA650GpuVmNativeQualified || KFNativeWorker(NULL))return KG_CLOSED;
 uint64_t epoch=0;auto*cold=kebab_gpu_first_run_lease(&epoch);if(cold!=&kf_native.cold || epoch!=kvg_engine.token.epoch)return KG_OWNERSHIP;
 const uint64_t pa[4]={0x170000,0x100000,0x152000,0x3d90000};const uint32_t bytes[4]={0x4000,0x1000,0x1000,0x4000};int e=0;
 for(unsigned i=0;i<4;i++){
  n->clock_memory[i]=IOMemoryDescriptor::withPhysicalAddress(pa[i],bytes[i],kIODirectionIn);
  if(!n->clock_memory[i]){e=KG_POOL;break;}n->clock_mapping[i]=n->clock_memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
  if(!n->clock_mapping[i] || n->clock_mapping[i]->getLength()!=bytes[i] || !(n->clock[i]=n->clock_mapping[i]->getVirtualAddress())){e=KG_POOL;break;}
 }
 if(!e){KebabGmuBootIo io={n,KVGNativeClockRead,NULL,KVGNativeClockBarrier,NULL,KVGNativeNow,NULL,NULL,NULL,NULL};e=kebab_rpmh_gpu_vm_acquire(&n->owner,cold,epoch,&io);}
 *held=n->owner.held;if(e&&!*held)KVGNativeClockUnmap(n);return e;
}
static int KVGNativeCheck(void*cookie)
{auto*n=(KVGNative*)cookie;return kebab_rpmh_gpu_vm_check(&n->owner);}
static int KVGNativeEnd(void*cookie)
{
 auto*n=(KVGNative*)cookie;
 /* All new allocations are made AFTER namespace reservation. Before dropping
  * that reservation, dispose only proven unpublished buffers. Retained failed
  * prepare/complete keeps the namespace and parent pinned, blocking PAS13. */
 KebabA650GpuBackingRecord r={};int e=kebab_a650_gpu_backing_snapshot(&r);
 if(!e&&r.pools){if(r.phase!=KAGPU_PREPARED)return KG_QUARANTINE;e=kebab_a650_gpu_backing_release_unpublished();if(e)return e;}
 else if(e!=KG_STATE&&e)return e;
 e=kebab_rpmh_gpu_vm_end(&n->owner);if(!e)KVGNativeClockUnmap(n);return e;
}
static int KVGNativeBackingAcquire(void*cookie,KebabA650GpuBackingDescription*d,uint32_t*held)
{auto*n=(KVGNative*)cookie;*held=0;int e=kebab_a650_gpu_backing_prepare();if(e)return e;e=kebab_a650_gpu_backing_acquire(&n->bo,d);*held=n->bo.held;return e;}
static int KVGNativeBackingCheck(void*cookie)
{return kebab_a650_gpu_backing_check(&((KVGNative*)cookie)->bo);}
static int KVGNativeBackingPublish(void*cookie)
{auto*n=(KVGNative*)cookie;int e=kebab_rpmh_gpu_vm_publish(&n->owner);return e?e:kebab_a650_gpu_backing_publish(&n->bo);}
static int KVGNativeBackingEnd(void*cookie)
{return kebab_a650_gpu_backing_end(&((KVGNative*)cookie)->bo);}
static int KVGNativeMap(void*cookie)
{
 auto*n=(KVGNative*)cookie;int e=KVGNativeCheck(n);if(e)return e;
 n->registers=IOMemoryDescriptor::withPhysicalAddress(KG_BASE,KG_BYTES,kIODirectionInOut);if(!n->registers)return KG_POOL;
 n->mapping=n->registers->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!n->mapping || n->mapping->getLength()!=KG_BYTES || !(n->base=n->mapping->getVirtualAddress()))return KG_POOL;return 0;
}
static void KVGNativeUnmap(void*cookie)
{auto*n=(KVGNative*)cookie;n->base=0;if(n->mapping){n->mapping->release();n->mapping=NULL;}if(n->registers){n->registers->release();n->registers=NULL;}}
static int KVGNativeRead32(void*cookie,uint32_t off,uint32_t*out)
{auto*n=(KVGNative*)cookie;if(!out||!n->base||!KVGReadAllowed(off,0)||kebab_a650_gpu_vm_ready_locked(&n->owner))return KG_CLOSED;*out=*(volatile const uint32_t*)(n->base+off);return 0;}
static int KVGNativeRead64(void*cookie,uint32_t off,uint64_t*out)
{auto*n=(KVGNative*)cookie;if(!out||!n->base||!KVGReadAllowed(off,1)||kebab_a650_gpu_vm_ready_locked(&n->owner))return KG_CLOSED;*out=*(volatile const uint64_t*)(n->base+off);return 0;}
static int KVGNativeWrite32(void*cookie,uint32_t off,uint32_t value)
{
 auto*n=(KVGNative*)cookie;if(!KebabA650GpuVmNativeQualified || !n->base || !n->owner.published || !KVGWriteAllowed(off,0) || kebab_a650_gpu_vm_ready_locked(&n->owner))return KG_CLOSED;
 uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint32_t*)address=value;
#endif
 return 0;
}
static int KVGNativeWrite64(void*cookie,uint32_t off,uint64_t value)
{
 auto*n=(KVGNative*)cookie;if(!KebabA650GpuVmNativeQualified || !n->base || !n->owner.published || !KVGWriteAllowed(off,1) || kebab_a650_gpu_vm_ready_locked(&n->owner))return KG_CLOSED;
 uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("str %0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint64_t*)address=value;
#endif
 return 0;
}
static void KVGNativeBarrier(void*)
{
 /* The engine's void barrier must always execute, even if worker state
  * changes. A fallible clock callback must not silently skip this ordering. */
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 OSMemoryBarrier();
#endif
}
static void KVGNativeDelay(void*,unsigned us){if(us==1)IODelay(1);}
static int KVGNativeReserve(void*,KFNamespace*s,KFToken*t,const KFPolicy*p)
{
 if(s!=&kf_namespace || t!=&kvg_engine.token || KZANativeVmReservationAllowed())return KG_OWNERSHIP;
 if(!KFTry(&kf_pas13_gate))return KG_BUSY;
 int e=kf_pas13_busy?KG_BUSY:KFReserve(s,t,p);KFUnlock(&kf_pas13_gate);return e;
}
extern "C" int kebab_a650_gpu_vm_attach(void)
{
 if(!KebabA650GpuVmNativeQualified)return KG_CLOSED;
 int e=KFNativeWorker(NULL);if(e)return e;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kvg_native_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return KG_STATE;
 KFPolicy p=KebabGpuFirstNativePolicy;p.ready=KebabA650GpuVmNativeQualified;p.cb=1;p.kernel_smr=2;p.user_smr=3;memcpy(p.source_contract_sha256,KebabA650GpuVmSourceContract,32);
 KVGops o={&kvg_native,KFNativeWorker,KVGNativeHold,KVGNativeCheck,KVGNativeEnd,KVGNativeBackingAcquire,KVGNativeBackingCheck,KVGNativeBackingPublish,KVGNativeBackingEnd,
 KVGNativeMap,KVGNativeUnmap,KVGNativeRead32,KVGNativeRead64,KVGNativeWrite32,KVGNativeWrite64,KVGNativeBarrier,KVGNativeDelay,KVGNativeNow,KVGNativeReserve};
 return KVGAttach(&kvg_engine,&kf_namespace,&kf_attachment,&p,&o);
}
extern "C" int kebab_a650_gpu_vm_snapshot(KVGRecord*r,KGDiscovery*before,KGDiscovery*after)
{
 if(!r||!before||!after)return KG_INVALID;if(!KFTry(&kvg_engine.busy))return KG_BUSY;
 int e=kvg_engine.attempted?0:KG_STATE;if(!e){*r=kvg_engine.record;*before=kvg_engine.before;*after=kvg_engine.after;}KFUnlock(&kvg_engine.busy);return e;
}
#endif
