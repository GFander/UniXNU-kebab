/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_FIRST_ATTACH_H
#define KEBAB_NATIVE_GPU_FIRST_ATTACH_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm/machine_routines.h>
#if defined(KERNEL)
#include <kern/KebabGpuFirstAttachCore.h>
#include <kern/KebabGpuColdOwner.h>
#else
#include "KebabGpuFirstAttachCore.h"
#include "KebabGpuColdOwner.h"
#endif
#include "KebabA650GmuNativeBacking.h"
extern "C" int get_preemption_level(void);
typedef struct {
 KFFirstAttach *engine;KebabGpuColdToken cold;KebabA650GmuBackingToken backing;
 KebabA650GmuBackingDescription description;
 IOMemoryDescriptor *registers,*clock_registers;IOMemoryMap *mapping,*clock_mapping;
 uintptr_t base,clock_base;uint32_t groups;
} KFNative;
static int KFNativeWorker(void *)
{return ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()?KG_CLOSED:0;}
static int KFNativeClockRead(void *cookie,uint32_t off,uint32_t *out)
{
 KFNative *n=(KFNative *)cookie;
 if(!n || !out || !n->clock_base || !n->cold.bus.held || (off!=0x1064 && off!=0x1098))return KG_INVALID;
 int e=KFNativeWorker(n);if(e)return e;uintptr_t address=n->clock_base+off;
#if defined(__aarch64__)
 __asm__ volatile("ldr %w0, [%1]":"=r"(*out):"r"(address):"memory");
#else
 *out=*(volatile const uint32_t *)address;
#endif
 return 0;
}
static int KFNativeHold(void *cookie,uint32_t *held)
{
 KFNative *n=(KFNative *)cookie;int e=KFNativeWorker(n);if(e)return e;
 e=kebab_rpmh_gpu_bus_acquire(&n->cold.bus);*held=n->cold.bus.held;if(e)return e;
 n->clock_registers=IOMemoryDescriptor::withPhysicalAddress(0x03d90000,0x8000,kIODirectionIn);
 if(!n->clock_registers)return KG_POOL;
 n->clock_mapping=n->clock_registers->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
 if(!n->clock_mapping || n->clock_mapping->getLength()!=0x8000 || !(n->clock_base=n->clock_mapping->getVirtualAddress()))return KG_POOL;
 KebabGpuColdIo io={n,KFNativeClockRead};e=kebab_rpmh_gpu_cold_acquire(&n->cold,&io);
 *held=n->cold.bus.held || n->cold.held;return e;
}
static int KFNativeCheck(void *cookie,uint32_t flags)
{KFNative *n=(KFNative *)cookie;int e=KFNativeWorker(n);return e?e:kebab_rpmh_gpu_cold_check(&n->cold,flags);}
static int KFNativeDrain(void *cookie)
{KFNative *n=(KFNative *)cookie;int e=KFNativeWorker(n);return e?e:kebab_rpmh_gpu_cold_drain(&n->cold);}
static int KFNativeEnd(void *cookie)
{
 KFNative *n=(KFNative *)cookie;int e=0;
 if(n->cold.held){e=kebab_rpmh_gpu_cold_end(&n->cold);if(e)return e;}
 if(n->cold.bus.held){e=kebab_rpmh_gpu_bus_end(&n->cold.bus);if(e)return e;}
 n->clock_base=0;
 if(n->clock_mapping){n->clock_mapping->release();n->clock_mapping=NULL;}
 if(n->clock_registers){n->clock_registers->release();n->clock_registers=NULL;}
 return 0;
}
static void KFNativeDescription(const KebabA650GmuBackingDescription *d,KFBacking *b)
{
 memset(b,0,sizeof *b);b->generation=d->generation;b->root_pa=d->root_physical;b->table_identity=d->table_descriptor_identity;
 b->table_bytes=d->table_bytes;b->regions=d->region_count;b->pages=d->mapped_pages;b->cleaned=d->cleaned_for_device;
 for(unsigned i=0;i<6;i++){b->region_iova[i]=d->region[i].iova;b->region_bytes[i]=d->region[i].bytes;
 b->region_permissions[i]=d->region[i].permissions;b->region_pa[i]=d->region[i].physical;
 b->region_identity[i]=d->region[i].descriptor_identity;b->allocation_bytes[i]=d->region[i].allocation_bytes;}
}
static int KFNativeBackingAcquire(void *cookie,KFBacking *b,uint32_t *held)
{
 KFNative *n=(KFNative *)cookie;int e=kebab_a650_gmu_backing_acquire(&n->backing,&n->description);*held=n->backing.held;
 if(!e)KFNativeDescription(&n->description,b);return e;
}
static int KFNativeBackingCheck(void *cookie,const KFBacking *b)
{
 KFNative *n=(KFNative *)cookie;if(b!=&n->engine->backing)return KG_OWNERSHIP;
 int e=kebab_a650_gmu_backing_check(&n->backing);if(e)return e;KFBacking expected;KFNativeDescription(&n->description,&expected);
 return memcmp(b,&expected,sizeof expected)?KG_OWNERSHIP:0;
}
static int KFNativeBackingPublish(void *cookie)
{return kebab_a650_gmu_backing_publish(&((KFNative *)cookie)->backing);}
static int KFNativeBackingEnd(void *cookie)
{return kebab_a650_gmu_backing_end(&((KFNative *)cookie)->backing);}
static void KFNativeUnmap(void *cookie)
{
 KFNative *n=(KFNative *)cookie;n->base=0;n->groups=0;
 if(n->mapping){n->mapping->release();n->mapping=NULL;}if(n->registers){n->registers->release();n->registers=NULL;}
}
static int KFNativeMap(void *cookie)
{
 KFNative *n=(KFNative *)cookie;if(n->registers || n->mapping || n->base)return KG_STATE;
 int e=KFCheck(n->engine,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return e;
 n->registers=IOMemoryDescriptor::withPhysicalAddress(KG_BASE,KG_BYTES,kIODirectionInOut);if(!n->registers)return KG_POOL;
 n->mapping=n->registers->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!n->mapping || n->mapping->getLength()!=KG_BYTES || !(n->base=n->mapping->getVirtualAddress())){KFNativeUnmap(n);return KG_POOL;}
 e=KFCheck(n->engine,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)KFNativeUnmap(n);return e;
}
static int KFNativeRead32(void *cookie,uint32_t off,uint32_t *out)
{
 KFNative *n=(KFNative *)cookie;if(!n || !out || !n->base || !KGReadAllowed(off,0,31))return KG_INVALID;
 if(((off>=0x800 && off<0xa00) && (off-0x800)/4>=n->groups) || ((off>=0xc00 && off<0xe00) && (off-0xc00)/4>=n->groups))return KG_INVALID;
 int e=KFCheck(n->engine,KF_ACCESS);if(e)return e;uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("ldr %w0, [%1]":"=r"(*out):"r"(address):"memory");
#else
 *out=*(volatile const uint32_t *)address;
#endif
 if(off==0x20)n->groups=(*out&255)<=128?(*out&255):0;return 0;
}
static int KFNativeRead64(void *cookie,uint32_t off,uint64_t *out)
{
 KFNative *n=(KFNative *)cookie;if(!n || !out || !n->base || !KGReadAllowed(off,1,31))return KG_INVALID;
 int e=KFCheck(n->engine,KF_ACCESS);if(e)return e;uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("ldr %0, [%1]":"=r"(*out):"r"(address):"memory");
#else
 *out=*(volatile const uint64_t *)address;
#endif
 return 0;
}
static int KFNativeWrite32(void *cookie,uint32_t off,uint32_t value)
{
 KFNative *n=(KFNative *)cookie;if(!n || !n->base || !KFWriteAllowed(n->engine,off,0))return KG_INVALID;
 int e=KFCheck(n->engine,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return e;uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint32_t *)address=value;
#endif
 return 0;
}
static int KFNativeWrite64(void *cookie,uint32_t off,uint64_t value)
{
 KFNative *n=(KFNative *)cookie;if(!n || !n->base || !KFWriteAllowed(n->engine,off,1))return KG_INVALID;
 int e=KFCheck(n->engine,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return e;uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("str %0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint64_t *)address=value;
#endif
 return 0;
}
static void KFNativeBarrier(void *)
{
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 OSMemoryBarrier();
#endif
}
static void KFNativeDelay(void *){IODelay(1);}
static KFOps KFNativeInit(KFNative *n,KFFirstAttach *engine)
{
 n->engine=engine;KFOps o={n,KFNativeWorker,KFNativeHold,KFNativeCheck,KFNativeDrain,KFNativeEnd,
 KFNativeBackingAcquire,KFNativeBackingCheck,KFNativeBackingPublish,KFNativeBackingEnd,
 KFNativeMap,KFNativeUnmap,KFNativeRead32,KFNativeRead64,KFNativeWrite32,KFNativeWrite64,KFNativeBarrier,KFNativeDelay};return o;
}
#endif
