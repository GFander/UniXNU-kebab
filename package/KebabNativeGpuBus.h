/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_BUS_H
#define KEBAB_NATIVE_GPU_BUS_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm/machine_routines.h>
#if defined(KEBAB_BUS_HOST)
#include "KebabGpuBusOwner.h"
#else
#include <kern/KebabGpuBusOwner.h>
#endif
static_assert(sizeof(KebabGpuBusRecord)==312,"GPU SMMU bus hold record schema1");
extern "C" int get_preemption_level(void);
/* The existing normal-worker/board CFG access check is a context/access
 * check only. Permission comes from the compiled binding AND same canonical
 * live parent ledgers, never this callback's boolean or a CAP copy. */
struct KebabNativeGpuBusAccess {
 IOMemoryDescriptor *gcc_memory,*cc_memory;IOMemoryMap *gcc_map,*cc_map;
 uintptr_t gcc,cc;void *cookie;int (*worker_check)(void *);
};
static int KebabNativeGpuBusWorker(KebabNativeGpuBusAccess *a)
{
 if(!a || !a->worker_check || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return a->worker_check(a->cookie);
}
static int KebabNativeGpuBusRead(void *cookie,unsigned region,uint32_t off,uint32_t *out)
{
 KebabNativeGpuBusAccess *a=(KebabNativeGpuBusAccess *)cookie;uintptr_t address;
 if(!a || !out || !KebabGpuBusAllowed(region,off))return 22;
 int e=KebabNativeGpuBusWorker(a);if(e)return e;
 if(region==0){if(!a->gcc || off<0x70000 || off>0x73ffc)return 22;address=a->gcc+off-0x70000;}
 else{if(!a->cc || off>0x7ffc)return 22;address=a->cc+off;}
#if defined(__aarch64__)
 __asm__ volatile("ldr %w0, [%1]" : "=r"(*out) : "r"(address) : "memory");
#else
 *out=*(volatile const uint32_t *)address;
#endif
 return 0;
}
static int KebabNativeGpuBusWrite(void *cookie,unsigned region,uint32_t off,uint32_t value)
{
 KebabNativeGpuBusAccess *a=(KebabNativeGpuBusAccess *)cookie;uint32_t current;uintptr_t address;
 if(!KebabGpuBusWriteMask(region,off) || !(value&1))return 22;
 int e=KebabNativeGpuBusRead(a,region,off,&current);if(e)return e;
 if(((value^current)&0x0ffffffeu)!=0)return 22;
 value=current|1u;address=region==0?a->gcc+off-0x70000:a->cc+off;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]" : "+r"(value) : "r"(address) : "memory");
#else
 *(volatile uint32_t *)address=value;
#endif
 return 0;
}
static int KebabNativeGpuBusBarrier(void *cookie)
{
 int e=KebabNativeGpuBusWorker((KebabNativeGpuBusAccess *)cookie);if(e)return e;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int KebabNativeGpuBusDelay(void *cookie,uint32_t us)
{
 int e=KebabNativeGpuBusWorker((KebabNativeGpuBusAccess *)cookie);if(e)return e;
 if(us!=1 && us!=10)return 22;IODelay(us);return 0;
}
static void KebabNativeGpuBusUnmap(KebabNativeGpuBusAccess *a)
{
 if(a->cc_map)a->cc_map->release();if(a->cc_memory)a->cc_memory->release();
 if(a->gcc_map)a->gcc_map->release();if(a->gcc_memory)a->gcc_memory->release();
 a->gcc=a->cc=0;a->gcc_map=a->cc_map=NULL;a->gcc_memory=a->cc_memory=NULL;
}
static int KebabNativeGpuBusMap(KebabNativeGpuBusAccess *a)
{
 int e=KebabNativeGpuBusWorker(a);if(e)return e;
 /* Compiled default is closed before even creating a mapper. */
 e=kebab_rpmh_gpu_bus_gate();if(e)return e;
 a->gcc_memory=IOMemoryDescriptor::withPhysicalAddress(0x00170000,0x4000,kIODirectionInOut);
 if(!a->gcc_memory)goto failed;
 a->gcc_map=a->gcc_memory->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!a->gcc_map || a->gcc_map->getLength()!=0x4000 || !(a->gcc=a->gcc_map->getVirtualAddress()))goto failed;
 a->cc_memory=IOMemoryDescriptor::withPhysicalAddress(0x03d90000,0x8000,kIODirectionInOut);
 if(!a->cc_memory)goto failed;
 a->cc_map=a->cc_memory->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!a->cc_map || a->cc_map->getLength()!=0x8000 || !(a->cc=a->cc_map->getVirtualAddress()))goto failed;
 return 0;
failed:KebabNativeGpuBusUnmap(a);return 12;
}
/* Static lifetime mappings/cookie: canonical check uses the same real IO
 * callbacks throughout the hold, including every later SMMU read lease check.
 * Failed post-pin operations retain these mappings and all parent references. */
static KebabNativeGpuBusAccess kebab_native_gpu_bus_access;
static KebabGpuBusRecord kebab_native_gpu_bus_record;
static uint32_t kebab_native_gpu_bus_state;
static int kebab_native_gpu_bus_prepare(void *cookie,int (*worker_check)(void *))
{
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kebab_native_gpu_bus_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
  return expected==2?(int)kebab_native_gpu_bus_record.result:16;
 KebabNativeGpuBusAccess *a=&kebab_native_gpu_bus_access;a->cookie=cookie;a->worker_check=worker_check;
 int e=KebabNativeGpuBusMap(a);
 if(!e){KebabGpuBusIo io={a,KebabNativeGpuBusRead,KebabNativeGpuBusWrite,KebabNativeGpuBusBarrier,KebabNativeGpuBusDelay};e=kebab_rpmh_gpu_bus(KebabGpuBusEnableHold,&io,&kebab_native_gpu_bus_record);}
 else{KebabGpuBusInit(&kebab_native_gpu_bus_record,KebabGpuBusEnableHold);kebab_native_gpu_bus_record.result=e;}
 if(e && !kebab_native_gpu_bus_record.pinned_parent)KebabNativeGpuBusUnmap(a);
 __atomic_store_n(&kebab_native_gpu_bus_state,2,__ATOMIC_RELEASE);return e;
}
static int kebab_native_gpu_bus_snapshot(KebabGpuBusRecord *out)
{
 if(!out)return 22;if(__atomic_load_n(&kebab_native_gpu_bus_state,__ATOMIC_ACQUIRE)!=2)return 11;
 *out=kebab_native_gpu_bus_record;return 0; /* copy success; inspect out->result */
}
#endif
