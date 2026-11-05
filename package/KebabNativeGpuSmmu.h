/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_SMMU_H
#define KEBAB_NATIVE_GPU_SMMU_H
#define KEBAB_A650_GMU_BACKING_WC_NATIVE 1
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <arm/machine_routines.h>
#include "KebabA650GmuPmapCache.h"
#include "KebabGpuSmmuCore.h"
static_assert(sizeof(KGReceipt)==120,"GPU SMMU candidate receipt v1");
static_assert(sizeof(KGDiscovery)==1632,"bounded GPU SMMU discovery v1");
extern "C" void flush_dcache(vm_offset_t,unsigned,int);
extern "C" int get_preemption_level(void);

/* Native adapter, include once in the real normal-worker TU. An integration
 * must bind the canonical CC/GMU/bus owner here at compile time. CAP copies,
 * host PAs and raw observed reset values are not owner adapters. The shipped
 * default KGNativeInit(..., NULL) is CLOSED before mapper/MMIO. No guessed
 * 570/571 receipt schema or fabricated reset/drain callback is supplied. */
struct KGNativeOwner {
 void *cookie;
 int (*acquire)(void *,uint32_t,uint32_t,KGLease *);
 int (*check)(void *,const KGLease *,uint32_t);
 int (*quiesce)(void *,KGLease *);
 int (*end)(void *,KGLease *);
};
struct KGNative {
 KGDomain *domain; const KGNativeOwner *owner; KGLease *live;
 IOMemoryDescriptor *registers; IOMemoryMap *mapping; uintptr_t base;
 uint32_t writable,active,groups;
};
static int KGNativeWorker(void *)
{
 return ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()?KG_CLOSED:KG_OK;
}
static int KGNativeAcquire(void *cookie,uint32_t role,uint32_t active,KGLease *l)
{
 KGNative *a=(KGNative *)cookie;int e=KGNativeWorker(a);if(e)return e;
 if(!a || !a->owner || !a->owner->acquire || !a->owner->check || !a->owner->quiesce || !a->owner->end)return KG_CLOSED;
 memset(l,0,sizeof(*l));e=a->owner->acquire(a->owner->cookie,role,active,l);
 if(l->held){a->live=l;a->active=active;}return e;
}
static int KGNativeCheck(void *cookie,const KGLease *l,uint32_t need)
{
 KGNative *a=(KGNative *)cookie;int e=KGNativeWorker(a);if(e)return e;
 if(!a || !a->owner || !a->owner->check || !l || a->live!=l || !l->held || l->uncertain ||
    !l->generation || (l->flags&need)!=need || (l->cb_read_mask&~31u))return KG_CLOSED;
 if((need&KG_CONTEXT) && (l->cb>=5 || l->asid!=l->cb+1))return KG_OWNERSHIP;
 return a->owner->check(a->owner->cookie,l,need);
}
static int KGNativeQuiesce(void *cookie,KGLease *l)
{
 KGNative *a=(KGNative *)cookie;int e=KGNativeCheck(a,l,KG_EXCLUSIVE);if(e)return e;
 return a->owner->quiesce(a->owner->cookie,l);
}
static int KGNativeEnd(void *cookie,KGLease *l)
{
 KGNative *a=(KGNative *)cookie;
 if(!a || !a->owner || a->live!=l)return KG_OWNERSHIP;
 int e=a->owner->end(a->owner->cookie,l);
 if(!e){a->live=NULL;a->active=0;l->held=0;}return e;
}
static void KGNativeUnmap(void *cookie)
{
 KGNative *a=(KGNative *)cookie;if(!a)return;
 a->base=0;a->writable=0;a->groups=0;
 if(a->mapping){a->mapping->release();a->mapping=NULL;}
 if(a->registers){a->registers->release();a->registers=NULL;}
}
static int KGNativeMap(void *cookie,const KGLease *l)
{
 KGNative *a=(KGNative *)cookie;int e=KGNativeCheck(a,l,KG_ACCESS);if(e)return e;
 if(a->registers || a->mapping || a->base)return KG_STATE;
 a->writable=a->active && (l->flags&KG_EXCLUSIVE)==KG_EXCLUSIVE;
 a->registers=IOMemoryDescriptor::withPhysicalAddress(KG_BASE,KG_BYTES,a->writable?kIODirectionInOut:kIODirectionIn);
 if(!a->registers)return KG_POOL;
 a->mapping=a->registers->map(kIOMapAnywhere|kIOMapInhibitCache|(a->writable?0:kIOMapReadOnly));
 if(!a->mapping || a->mapping->getLength()!=KG_BYTES || !(a->base=a->mapping->getVirtualAddress())) {
  KGNativeUnmap(a);return KG_POOL;
 }
 e=KGNativeCheck(a,l,KG_ACCESS);if(e)KGNativeUnmap(a);return e;
}
static int KGNativeRead32(void *cookie,uint32_t offset,uint32_t *out)
{
 KGNative *a=(KGNative *)cookie;
 if(!a || !out || !a->base || !a->live || !KGReadAllowed(offset,0,a->live->cb_read_mask))return KG_INVALID;
 if((offset>=0x800 && offset<0xa00) && (offset-0x800)/4>=a->groups)return KG_INVALID;
 if((offset>=0xc00 && offset<0xe00) && (offset-0xc00)/4>=a->groups)return KG_INVALID;
 int e=KGNativeCheck(a,a->live,KG_ACCESS);if(e)return e;
 uintptr_t address=a->base+offset;
#if defined(__aarch64__)
 __asm__ volatile("ldr %w0, [%1]" : "=r"(*out) : "r"(address) : "memory");
#else
 *out=*(volatile const uint32_t *)address;
#endif
 if(offset==0x20)a->groups=(*out&255)<=KG_MAX_STREAMS?(*out&255):0;
 return KG_OK;
}
static int KGNativeRead64(void *cookie,uint32_t offset,uint64_t *out)
{
 KGNative *a=(KGNative *)cookie;
 if(!a || !out || !a->base || !a->live || !KGReadAllowed(offset,1,a->live->cb_read_mask))return KG_INVALID;
 int e=KGNativeCheck(a,a->live,KG_ACCESS);if(e)return e;
 uintptr_t address=a->base+offset;
#if defined(__aarch64__)
 __asm__ volatile("ldr %0, [%1]" : "=r"(*out) : "r"(address) : "memory");
#else
 *out=*(volatile const uint64_t *)address;
#endif
 return KG_OK;
}
static int KGNativeWrite32(void *cookie,uint32_t offset,uint32_t value)
{
 KGNative *a=(KGNative *)cookie;
 if(!a || !a->base || !a->live || !a->writable || !KGWriteScope(offset,0,a->live))return KG_INVALID;
 int e=KGNativeCheck(a,a->live,KG_BIND);if(e)return e;
 uintptr_t address=a->base+offset;
#if defined(__aarch64__)
 /* Read/write operand forces a real register even for zero (QCOM HYP
  * emulation's WZR/XZR warning in vendor arm-smmu.c). */
 __asm__ volatile("str %w0, [%1]" : "+r"(value) : "r"(address) : "memory");
#else
 *(volatile uint32_t *)address=value;
#endif
 return KG_OK;
}
static int KGNativeWrite64(void *cookie,uint32_t offset,uint64_t value)
{
 KGNative *a=(KGNative *)cookie;
 if(!a || !a->base || !a->live || !a->writable || !KGWriteScope(offset,1,a->live))return KG_INVALID;
 int e=KGNativeCheck(a,a->live,KG_BIND);if(e)return e;
 uintptr_t address=a->base+offset;
#if defined(__aarch64__)
 __asm__ volatile("str %0, [%1]" : "+r"(value) : "r"(address) : "memory");
#else
 *(volatile uint64_t *)address=value;
#endif
 return KG_OK;
}
static void KGNativeBarrier(void *)
{
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
}
static void KGNativeDelay(void *) { IODelay(1); }
static int KGNativeAlloc(void *,uint32_t index,uint32_t bytes,KGPool *p)
{
 if(!p || !bytes || (bytes&16383) || bytes>1048576)return KG_INVALID;
 auto memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
  kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone|
  ((index==5 || index==6)?kIOMapWriteCombineCache:0),bytes,KG_PA_LIMIT-1);
 if(!memory)return KG_POOL;
 IOBufferMemoryDescriptor *m;
#if defined(IOKIT_ENABLE_SHARED_PTR)
 m=memory.detach();
#else
 m=memory;
#endif
 p->handle=m;p->bytes=bytes;p->address=m->getBytesNoCopy();
 if(m->getLength()!=bytes || !p->address || ((uintptr_t)p->address&4095))return KG_POOL;
 p->prepare_result=m->prepare(kIODirectionInOut);if(p->prepare_result)return KG_POOL;
 p->prepared=1;IOByteCount extent=0;
 p->pa=m->getPhysicalSegment(0,&extent,kIOMemoryMapperNone);
 if(extent<bytes || p->pa<0x20000 || (p->pa&4095) || p->pa>KG_PA_LIMIT-bytes)return KG_POOL;
 return KG_OK;
}
static int KGNativeShared(const KGNative *a,const KGPool *p)
{ return a && a->domain && (p==&a->domain->pool[5] || p==&a->domain->pool[6]); }
static int KGNativePinCheck(void *cookie,const KGPool *p)
{
 if(!p || !p->handle || !p->prepared || !p->address || !p->bytes || (p->bytes&16383) ||
    p->bytes>1048576 || (p->pa&4095) || p->pa<0x20000 || p->pa>KG_PA_LIMIT-p->bytes)return KG_POOL;
 IOBufferMemoryDescriptor *m=(IOBufferMemoryDescriptor *)p->handle;
 if(m->getLength()!=p->bytes || m->getBytesNoCopy()!=p->address)return KG_POOL;
 for(uint32_t off=0;off<p->bytes;off+=4096) {
  IOByteCount extent=0;if(m->getPhysicalSegment(off,&extent,kIOMemoryMapperNone)!=p->pa+off || extent<4096)return KG_POOL;
 }
 /* The IOBuffer factory changes the VM object/pmap cache mode. Verify every
  * native page, including allocation padding, before using this same CPU VA.
  * No independent NC alias is created and no WB pointer is exported. */
 if(KGNativeShared((const KGNative *)cookie,p))
  for(uint32_t off=0;off<p->bytes;off+=PAGE_SIZE)
   if((pmap_cache_attributes((ppnum_t)((p->pa+off)>>PAGE_SHIFT))&VM_WIMG_MASK)!=VM_WIMG_WCOMB)return KG_POOL;
 return KG_OK;
}
static int KGNativeClean(void *cookie,const KGPool *p)
{
 int e=KGNativeWorker(cookie);if(e)return e;e=KGNativePinCheck(cookie,p);if(e)return e;
 /* Shared HFI/log are Normal-NC from allocation onward. Never CIVAC their
  * mixed CPU/device-owned cache lines. WB immutable RAM/table still clean. */
 if(!KGNativeShared((const KGNative *)cookie,p))flush_dcache((vm_offset_t)p->address,p->bytes,0);
 KGNativeBarrier(cookie);return KG_OK;
}
static int KGNativeFree(void *cookie,KGPool *p)
{
 if(!p || !p->handle)return KG_OK;
 int e=KGNativeWorker(cookie);if(e)return e;
 IOBufferMemoryDescriptor *m=(IOBufferMemoryDescriptor *)p->handle;
 /* Core alone decides hardware can no longer reference this resource.
  * Do not zero on a failed complete, and never release on complete failure. */
 if(p->prepared) {
  p->complete_result=m->complete(kIODirectionInOut);if(p->complete_result)return KG_POOL;
  p->prepared=0;
 }
 m->release();p->handle=NULL;p->address=NULL;p->pa=0;p->bytes=0;return KG_OK;
}
static KGOps KGNativeInit(KGNative *a,KGDomain *d,const KGNativeOwner *owner=NULL)
{
 memset(a,0,sizeof(*a));a->domain=d;a->owner=owner;
 KGOps ops={a,KGNativeWorker,KGNativeAcquire,KGNativeCheck,KGNativeQuiesce,KGNativeEnd,
  KGNativeMap,KGNativeUnmap,KGNativeRead32,KGNativeRead64,KGNativeWrite32,KGNativeWrite64,
  KGNativeBarrier,KGNativeDelay,KGNativeAlloc,KGNativePinCheck,KGNativeClean,KGNativeFree};
 return ops;
}
#endif
