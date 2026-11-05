/* SPDX-License-Identifier: MIT */
#include "KebabA650GpuBacking.h"
#include "KebabNativeGpuSmmu.h"
/* Separate graphics allocations; never reuse or mutate live GMU pools. The
 * existing native adapter allocates 16KiB CPU extents and builds 4KiB nG
 * leaves. Pools5/6 (readback/fence) are Normal-NC from allocation onward;
 * the same CPU mapping is verified WIMG6, with no WB/NC alias or later CIVAC.
 * This TU has no active owner, mapper, bind, MMIO or GPU command entry. */
static KGDomain kagpu_domain;
static KGNative kagpu_native;
static uint32_t kagpu_once,kagpu_closing;
/* Covers the release interval in which the generic engine drops its own
 * domain lock. No snapshot or token operation may race its final record. */
static uint32_t kagpu_operation;
struct KAGPUGuard {
 bool held;
 KAGPUGuard(){uint32_t expected=0;held=__atomic_compare_exchange_n(&kagpu_operation,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
 ~KAGPUGuard(){if(held)__atomic_store_n(&kagpu_operation,0,__ATOMIC_RELEASE);}
};
static int kagpu_result=KG_CLOSED;
static uint64_t kagpu_generation;
static KebabA650GpuBackingRecord kagpu_record;
struct KAGPUHolder {KebabA650GpuBackingToken *identity;uint64_t generation;uint32_t published;};
static KAGPUHolder kagpu_holder[8];
static const KGRegion kagpu_regions[6]={
 {0x90000000,0x1000,KG_READ,0,NULL},       /* ring */
 {0x90004000,0x1000,KG_READ,0,NULL},       /* IB */
 {0x90008000,0x4000,KG_READ|KG_EXEC,0,NULL}, /* future SQE, not loaded yet */
 {0x9000c000,0x4000,KG_READ|KG_EXEC,0,NULL}, /* shader/constants */
 {0x90010000,0x10000,KG_READ|KG_WRITE,0,NULL}, /* 128x128 RGBA8 readback */
 {0x90020000,0x1000,KG_READ|KG_WRITE,0,NULL} /* real hardware fence later */
};
static void kagpu_refresh(void)
{
 auto *r=&kagpu_record;r->magic=0x31424741;r->schema=1;r->bytes=sizeof *r;r->pools=r->cpu_bytes=r->holders=r->published=0;
 r->cpu_page_bytes=PAGE_SIZE;r->gpu_page_bytes=4096;r->table_bytes=65536;r->mapped_pages=kagpu_domain.receipt.mapped_pages;
 r->hardware_touched=kagpu_domain.receipt.hardware_touched;r->primary_error=kagpu_domain.receipt.primary_error;r->cleanup_error=kagpu_domain.receipt.cleanup_error;
 for(unsigned n=0;n<7;n++)if(kagpu_domain.pool[n].handle){r->pools++;r->cpu_bytes+=kagpu_domain.pool[n].bytes;}
 for(unsigned n=0;n<8;n++)if(kagpu_holder[n].identity){r->holders++;r->published|=kagpu_holder[n].published;}
 for(unsigned n=0;n<6;n++){r->region_iova[n]=kagpu_regions[n].iova;r->region_bytes[n]=kagpu_regions[n].bytes;r->region_allocation_bytes[n]=kagpu_domain.pool[n+1].bytes;r->region_permissions[n]=kagpu_regions[n].permissions;}
}
static int kagpu_pins(void)
{
 if(kagpu_closing || kagpu_record.phase!=KAGPU_PREPARED || !kagpu_record.cleaned_for_device || kagpu_domain.receipt.state!=KG_PREPARED || kagpu_domain.count!=6 || kagpu_domain.receipt.mapped_pages!=27 || kagpu_domain.touched || kagpu_domain.mapped || kagpu_domain.lease.held)return KG_STATE;
 for(unsigned n=0;n<7;n++){
  auto *p=&kagpu_domain.pool[n];uint32_t size=n?((kagpu_regions[n-1].bytes+16383u)&~16383u):65536;
  int e=KGPoolValid(&kagpu_domain,p,size);if(e)return e;
  if((p->pa&16383u) || ((uintptr_t)p->address&16383u))return KG_POOL;
 }
 return 0;
}
static int kagpu_token(const KebabA650GpuBackingToken *t)
{
 if(!t || !t->held || t->slot>=8 || !t->generation)return KG_OWNERSHIP;
 const auto *h=&kagpu_holder[t->slot];return h->identity==t&&h->generation==t->generation?0:KG_OWNERSHIP;
}
extern "C" int kebab_a650_gpu_backing_prepare(void)
{
 int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kagpu_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2)return KG_BUSY;if(kagpu_result)return kagpu_result;
  if(!KGTry(&kagpu_domain))return KG_BUSY;e=kagpu_pins();KGUnlock(&kagpu_domain);return e;
 }
 if(PAGE_SIZE!=16384 || PAGE_SHIFT!=14){kagpu_result=KG_CLOSED;goto done;}
 {
  KGOps ops=KGNativeInit(&kagpu_native,&kagpu_domain,NULL);
  e=KebabGpuSmmuPrepare(&kagpu_domain,&ops,KG_GFX_USER,kagpu_regions,6);kagpu_result=e;
  if(!e){kagpu_record.phase=KAGPU_PREPARED;kagpu_record.cleaned_for_device=1;kagpu_record.NC_native_pages=5;e=kagpu_pins();kagpu_result=e;}
 }
done:
 kagpu_record.result=(uint32_t)kagpu_result;
 if(kagpu_result){kagpu_record.phase=KAGPU_FAILED;kagpu_record.cleaned_for_device=0;}
 kagpu_refresh();__atomic_store_n(&kagpu_once,2,__ATOMIC_RELEASE);return kagpu_result;
}
extern "C" int kebab_a650_gpu_backing_snapshot(KebabA650GpuBackingRecord *out)
{
 if(!out)return KG_INVALID;KAGPUGuard operation;if(!operation.held)return KG_BUSY;if(__atomic_load_n(&kagpu_once,__ATOMIC_ACQUIRE)!=2)return KG_STATE;
 if(!KGTry(&kagpu_domain))return KG_BUSY;kagpu_refresh();*out=kagpu_record;KGUnlock(&kagpu_domain);return 0;
}
extern "C" int kebab_a650_gpu_backing_acquire(KebabA650GpuBackingToken *t,KebabA650GpuBackingDescription *out)
{
 int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;if(!t||!out)return KG_INVALID;
 if(__atomic_load_n(&kagpu_once,__ATOMIC_ACQUIRE)!=2 || kagpu_result)return KG_STATE;
 if(!KGTry(&kagpu_domain))return KG_BUSY;unsigned slot=8;
 if(t->held){KGUnlock(&kagpu_domain);return KG_OWNERSHIP;}
 for(unsigned n=0;n<8;n++){if(kagpu_holder[n].identity==t){KGUnlock(&kagpu_domain);return KG_OWNERSHIP;}if(!kagpu_holder[n].identity&&slot==8)slot=n;}
 if(slot==8 || kagpu_generation==UINT64_MAX){KGUnlock(&kagpu_domain);return KG_BUSY;}
 e=kagpu_pins();if(e){KGUnlock(&kagpu_domain);return e;}
 memset(out,0,sizeof *out);out->generation=++kagpu_generation;out->root_physical=kagpu_domain.pool[0].pa;out->table_descriptor_identity=(uintptr_t)kagpu_domain.pool[0].handle;
 out->table_bytes=65536;out->region_count=6;out->mapped_pages=27;out->cleaned_for_device=1;
 for(unsigned n=0;n<6;n++){const auto*p=&kagpu_domain.pool[n+1];const auto*q=&kagpu_regions[n];out->region[n]={q->iova,q->bytes,q->permissions,p->bytes,p->pa,(uintptr_t)p->handle,(uintptr_t)p->address};}
 *t={out->generation,slot,1};kagpu_holder[slot]={t,t->generation,0};KGUnlock(&kagpu_domain);return 0;
}
extern "C" int kebab_a650_gpu_backing_check(const KebabA650GpuBackingToken *t)
{int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;if(!KGTry(&kagpu_domain))return KG_BUSY;e=kagpu_token(t);if(!e)e=kagpu_pins();KGUnlock(&kagpu_domain);return e;}
extern "C" int kebab_a650_gpu_backing_publish(KebabA650GpuBackingToken *t)
{int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;if(!KGTry(&kagpu_domain))return KG_BUSY;e=kagpu_token(t);if(!e)e=kagpu_pins();if(!e)kagpu_holder[t->slot].published=1;KGUnlock(&kagpu_domain);return e;}
extern "C" int kebab_a650_gpu_backing_end(KebabA650GpuBackingToken *t)
{
 int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;if(!KGTry(&kagpu_domain))return KG_BUSY;e=kagpu_token(t);
 if(!e){auto*h=&kagpu_holder[t->slot];if(h->published)e=KG_BUSY;else {memset(h,0,sizeof *h);memset(t,0,sizeof *t);}}
 KGUnlock(&kagpu_domain);return e;
}
extern "C" int kebab_a650_gpu_backing_release_unpublished(void)
{
 int e=KGNativeWorker(NULL);if(e)return e;KAGPUGuard operation;if(!operation.held)return KG_BUSY;
 if(__atomic_load_n(&kagpu_once,__ATOMIC_ACQUIRE)!=2)return KG_STATE;
 if(!KGTry(&kagpu_domain))return KG_BUSY;if(kagpu_closing){KGUnlock(&kagpu_domain);return KG_STATE;}
 for(unsigned n=0;n<8;n++)if(kagpu_holder[n].identity){KGUnlock(&kagpu_domain);return KG_BUSY;}
 kagpu_closing=1;KGUnlock(&kagpu_domain);e=KebabGpuSmmuRelease(&kagpu_domain);
 kagpu_record.result=e;kagpu_record.cleaned_for_device=0;kagpu_record.phase=e?KAGPU_FAILED:KAGPU_RELEASED;return e;
}
