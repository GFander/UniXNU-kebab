/* SPDX-License-Identifier: MIT */
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "KebabSmmu4K.h"
extern "C" void flush_dcache(vm_offset_t,unsigned,int);
/* Private immutable copy of a temporary native allocation. No SMMU register
 * references this pool; the native descriptor is completed and released before
 * capture publication. This is physical allocation/table construction proof,
 * not a live IOMMU domain or DMA transfer. */
struct KebabNativeIovaProbe {
 uint32_t magic,schema,bytes,flags,result,prepare_result,complete_result,table_bytes;
 uint32_t used_tables,mapped_pages,pool_low,pool_high,iova_low,iova_high,span_low,span_high;
 uint32_t root_low,root_high,first_low,first_high,last_low,last_high,table_crc,granule;
 uint64_t tables[8192];
};
static_assert(sizeof(KebabNativeIovaProbe)==65632,"fixed temporary native4K table probe");
static IOBufferMemoryDescriptor *kebab_iova_quarantined;
static int kebab_native_iova_probe(uint64_t physical,uint64_t length,KebabNativeIovaProbe *out)
{
 if(!out)return 22;
 memset(out,0,sizeof *out);out->magic=0x34564f49;out->schema=3;out->bytes=sizeof *out;out->table_bytes=65536;out->granule=4096;out->result=22;
 if(!length || length>0x1000000 || (physical&4095) || physical<0x20000 || physical>0x100000000ULL-length)return 0;
 uint64_t span=(length+4095)&~4095ULL;
 if(physical>0x100000000ULL-span)return 0;
 out->iova_low=(uint32_t)physical;out->iova_high=(uint32_t)(physical>>32);out->span_low=(uint32_t)span;out->span_high=(uint32_t)(span>>32);
 /* IOVA width is independent of the physical page-table address width.
  * The primary XNU RAM begins above8GiB; require the live board's4K/36-bit
  * SMMU capability rather than requesting an impossible sub4GiB pool. */
 KebabNativeDisplaySnapshot hardware={};
 out->result=95;
 if(kebab_native_display_snapshot(physical,&hardware) || hardware.smmu_result ||
    !(hardware.smmu.id2&(1u<<12)) || ((hardware.smmu.id2>>4)&15)!=1)return 0;
 if(kebab_iova_quarantined){out->result=5;return 0;}
 auto memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,kIODirectionOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,65536,0xfffffffffULL);
 out->result=12;if(!memory)return 0;
#if !defined(IOKIT_ENABLE_SHARED_PTR)
 struct KebabIovaRelease {IOBufferMemoryDescriptor *p;~KebabIovaRelease(){if(p)p->release();}} owner={memory};
#endif
out->result=75;if(memory->getLength()<65536)return 0;
 out->result=14;uint64_t *pool=(uint64_t *)memory->getBytesNoCopy();if(!pool || ((uintptr_t)pool&7))return 0;
 out->prepare_result=memory->prepare(kIODirectionOut);if(out->prepare_result){out->result=5;return 0;}
 {
  IOByteCount extent=0;uint64_t pa=memory->getPhysicalSegment(0,&extent,kIOMemoryMapperNone);
  out->result=5;
  if(!pa || (pa&4095) || pa>0x1000000000ULL-65536 || extent<65536)goto complete;
  for(unsigned offset=0;offset<65536;offset+=4096) {
   extent=0;uint64_t actual=memory->getPhysicalSegment(offset,&extent,kIOMemoryMapperNone);
   if(actual!=pa+offset || extent<4096)goto complete;
  }
  out->pool_low=(uint32_t)pa;out->pool_high=(uint32_t)(pa>>32);out->flags|=1;
  {
   KebabSmmu4KInfo info={};
   if(KebabSmmu4KBuild(pool,65536,pa,physical,physical,span,&info))goto complete;
   out->used_tables=info.pages_used;out->mapped_pages=info.mapped_pages;
   out->root_low=(uint32_t)info.root_pa;out->root_high=(uint32_t)(info.root_pa>>32);out->flags|=2;
   for(uint64_t offset=0;offset<span;offset+=4096) {
    uint64_t got=0;if(KebabSmmu4KTranslate(pool,65536,pa,physical+offset,&got) || got!=physical+offset)goto complete;
   }
   uint64_t first=0,last=0,unused=0;
   if(KebabSmmu4KTranslate(pool,65536,pa,physical,&first) || KebabSmmu4KTranslate(pool,65536,pa,physical+span-1,&last) ||
      !KebabSmmu4KTranslate(pool,65536,pa,physical-1,&unused) || !KebabSmmu4KTranslate(pool,65536,pa,physical+span,&unused))goto complete;
   out->first_low=(uint32_t)first;out->first_high=(uint32_t)(first>>32);out->last_low=(uint32_t)last;out->last_high=(uint32_t)(last>>32);out->flags|=4;
   flush_dcache((vm_offset_t)pool,65536,0);OSMemoryBarrier();out->flags|=8;
   memcpy(out->tables,pool,65536);out->table_crc=kc_crc(out->tables,65536);out->result=0;
  }
 }
complete:
 memset(pool,0,65536);out->complete_result=memory->complete(kIODirectionOut);
 if(out->complete_result) {
  out->result=5;
#if defined(IOKIT_ENABLE_SHARED_PTR)
  kebab_iova_quarantined=memory.detach();
#else
  kebab_iova_quarantined=memory;owner.p=NULL;
#endif
 }else out->flags|=16;
 return 0;
}
