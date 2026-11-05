/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_DMA_POOL_H
#define KEBAB_NATIVE_DMA_POOL_H
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "KebabSmmu4K.h"
extern "C" void flush_dcache(vm_offset_t,unsigned,int);
/* Explicit normal-worker preparation only. CAP reads never allocate or bind.
 * This owner retains two prepared descriptors for the kernel lifetime. No
 * SMMU/clock/device register references them yet. An active driver must add
 * ownership, quiesce, TLBI and stop-before-release before publishing either PA.
 */
struct KebabDmaResource {
 IOBufferMemoryDescriptor *memory;void *address;uint64_t physical;bool prepared;
 uint32_t prepare_result,complete_result;
};
struct KebabNativeDmaPoolReceipt {
 uint32_t magic,schema,bytes,flags,result,state,attempts,hardware_bound;
 uint32_t table_prepare,table_complete,data_prepare,data_complete;
 uint32_t table_low,table_high,data_low,data_high,iova_low,iova_high,span_low,span_high;
 uint32_t root_low,root_high,used_tables,mapped_pages,table_crc,data_crc,granule,reserved;
 uint32_t future[20];uint64_t tables[8192];
};
static_assert(sizeof(KebabNativeDmaPoolReceipt)==65728,"retained DMA pool receipt schema1");
static KebabDmaResource kebab_dma_table={},kebab_dma_data={};
static KebabNativeDmaPoolReceipt kebab_dma_receipt={};
static volatile uint32_t kebab_dma_busy=0;
static int kebab_dma_allocate(KebabDmaResource *r)
{
 auto memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
  kIODirectionOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,65536,0xfffffffffULL);
 if(!memory)return 12;
#if defined(IOKIT_ENABLE_SHARED_PTR)
 r->memory=memory.detach();
#else
 r->memory=memory;
#endif
 if(r->memory->getLength()!=65536)return 75;
 r->address=r->memory->getBytesNoCopy();
 if(!r->address || ((uintptr_t)r->address&4095))return 14;
 r->prepare_result=r->memory->prepare(kIODirectionOut);
 if(r->prepare_result)return 5;
 r->prepared=true;IOByteCount extent=0;
 r->physical=r->memory->getPhysicalSegment(0,&extent,kIOMemoryMapperNone);
 if(r->physical<0x20000 || (r->physical&4095) || r->physical>0x1000000000ULL-65536 || extent<65536)return 5;
 for(unsigned offset=0;offset<65536;offset+=4096) {
  extent=0;
  if(r->memory->getPhysicalSegment(offset,&extent,kIOMemoryMapperNone)!=r->physical+offset || extent<4096)return 5;
 }
 return 0;
}
/* Only unused, unpublished allocations may enter this cleanup. A failed
 * complete retains the descriptor, with its prepare state, in quarantine. */
static bool kebab_dma_discard_unpublished(KebabDmaResource *r)
{
 if(!r->memory)return true;
 if(r->prepared) {
  memset(r->address,0,65536);flush_dcache((vm_offset_t)r->address,65536,0);OSMemoryBarrier();
  r->complete_result=r->memory->complete(kIODirectionOut);
  if(r->complete_result)return false;
  r->prepared=false;
 }
 r->memory->release();r->memory=NULL;r->address=NULL;r->physical=0;return true;
}
static void kebab_native_dma_prepare(uint64_t framebuffer)
{
 if(!__sync_bool_compare_and_swap(&kebab_dma_busy,0,1)) {
  IOLog("KEBAB IOVAPREP: end; busy, not retried\n");return;
 }
 if(kebab_dma_receipt.state) {
  IOLog("KEBAB IOVAPREP: end; already attempted state=%u result=%u\n",kebab_dma_receipt.state,kebab_dma_receipt.result);
  __sync_lock_release(&kebab_dma_busy);return;
 }
 auto &r=kebab_dma_receipt;
 r.magic=0x31504d44;r.schema=1;r.bytes=sizeof r;r.state=1;r.attempts=1;r.granule=4096;
 r.iova_low=0x70000000;r.span_low=65536;
 KebabNativeDisplaySnapshot hardware={};KebabSmmu4KInfo info={};uint64_t got=0;
 r.result=95;
 if(kebab_native_display_snapshot(framebuffer,&hardware) || hardware.smmu_result ||
    !(hardware.smmu.id2&(1u<<12)) || ((hardware.smmu.id2>>4)&15)!=1)goto failed;
 r.result=kebab_dma_allocate(&kebab_dma_table);if(r.result)goto failed;r.flags|=1;
 r.result=kebab_dma_allocate(&kebab_dma_data);if(r.result)goto failed;r.flags|=2;
 if(kebab_dma_table.physical<kebab_dma_data.physical+65536 && kebab_dma_data.physical<kebab_dma_table.physical+65536) {r.result=22;goto failed;}
 for(unsigned i=0;i<65536/4;i++)((uint32_t *)kebab_dma_data.address)[i]=0x65000000u^i;
 r.result=KebabSmmu4KBuild((uint64_t *)kebab_dma_table.address,65536,kebab_dma_table.physical,
  r.iova_low,kebab_dma_data.physical,65536,&info);
 if(r.result)goto failed;
 for(unsigned offset=0;offset<65536;offset+=4096) {
  if(KebabSmmu4KTranslate((uint64_t *)kebab_dma_table.address,65536,kebab_dma_table.physical,r.iova_low+offset,&got) ||
     got!=kebab_dma_data.physical+offset){r.result=5;goto failed;}
 }
 if(!KebabSmmu4KTranslate((uint64_t *)kebab_dma_table.address,65536,kebab_dma_table.physical,r.iova_low-1,&got) ||
    !KebabSmmu4KTranslate((uint64_t *)kebab_dma_table.address,65536,kebab_dma_table.physical,r.iova_low+65536,&got)) {r.result=5;goto failed;}
 r.flags|=4;
 flush_dcache((vm_offset_t)kebab_dma_data.address,65536,0);
 flush_dcache((vm_offset_t)kebab_dma_table.address,65536,0);OSMemoryBarrier();r.flags|=8;
 r.table_low=(uint32_t)kebab_dma_table.physical;r.table_high=(uint32_t)(kebab_dma_table.physical>>32);
 r.data_low=(uint32_t)kebab_dma_data.physical;r.data_high=(uint32_t)(kebab_dma_data.physical>>32);
 r.root_low=(uint32_t)info.root_pa;r.root_high=(uint32_t)(info.root_pa>>32);
 r.used_tables=info.pages_used;r.mapped_pages=info.mapped_pages;
 memcpy(r.tables,kebab_dma_table.address,65536);r.table_crc=kc_crc(r.tables,65536);
 r.data_crc=kc_crc(kebab_dma_data.address,65536);r.flags|=16;r.state=2;r.result=0;
 goto done;
failed:
 {
  bool data_ok=kebab_dma_discard_unpublished(&kebab_dma_data);
  bool table_ok=kebab_dma_discard_unpublished(&kebab_dma_table);
  r.state=data_ok && table_ok?3:4;
 }
done:
 r.table_prepare=kebab_dma_table.prepare_result;r.table_complete=kebab_dma_table.complete_result;
 r.data_prepare=kebab_dma_data.prepare_result;r.data_complete=kebab_dma_data.complete_result;
 IOLog("KEBAB IOVAPREP: end; state=%u result=%u flags=%x retained=%u hardware-bound=0; no DMA proof\n",r.state,r.result,r.flags,r.state==2);
 __sync_lock_release(&kebab_dma_busy);
}
static int kebab_native_dma_snapshot(KebabNativeDmaPoolReceipt *out)
{
 if(!out)return 22;
 if(!__sync_bool_compare_and_swap(&kebab_dma_busy,0,1))return 16;
 memcpy(out,&kebab_dma_receipt,sizeof *out);
 if(!out->state){out->magic=0x31504d44;out->schema=1;out->bytes=sizeof *out;out->result=2;}
 __sync_lock_release(&kebab_dma_busy);return 0;
}
#endif
