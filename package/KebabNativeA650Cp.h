/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_CP_H
#define KEBAB_NATIVE_A650_CP_H
/* Include once at end of FirstAttach, after the actual OOB adapter. These
 * are the existing private BO/VM/OOB objects, never a CAP-derived lease. */
#include "KebabA650CpNative.h"
#include "KebabA650GmuPmapCache.h"
#include <pexpert/device_tree.h>
#include <libkern/crypto/sha2.h>
#if defined(KERNEL)
#include <kern/KebabA650CpOwner.h>
#include <kern/KebabA650CpBinding.h>
#else
#include "KebabA650CpOwner.h"
#include "KebabA650CpBinding.h"
#endif
struct KCPAccess {
 KebabA650CpOwnerToken owner;IOMemoryDescriptor *memory;IOMemoryMap *mapping;uintptr_t base;
 KebabA650CpObservation *observation;uint8_t *firmware;
 uint32_t firmware_verified,cpu_ready,write_index,copy_index,publish_index;
};
static KCPAccess kcp_native;
static uint32_t kcp_native_once;
static int kcp_native_result=EAGAIN;
static int KCPNativeBound(void)
{unsigned any=0;for(unsigned n=0;n<32;n++)any|=KebabA650CpSourceContract[n];return KebabA650CpNativeQualified==1&&any;}
extern "C" int kebab_a650_gpu_cp_context_ready_locked(const KebabA650GpuOobOwnerToken*oob)
{
 if(oob!=&kho_native.owner || __atomic_load_n(&kho_native_once,__ATOMIC_ACQUIRE)!=2 || kho_native_result ||
 !kho_native.base || !kho_native.observation || !kho_native.observation->receipt.held ||
 !oob->pinned || oob->quarantined || KVGBackingValid(&kvg_engine.backing) ||
 !kvg_native.bo.held || kvg_native.bo.generation!=kvg_engine.backing.generation)return 95;
 int e=kebab_a650_gpu_oob_vm_ready_locked(&kvg_native.owner);if(e)return e;
 return kebab_a650_gpu_backing_check(&kvg_native.bo);
}
extern "C" int kebab_a650_gpu_cp_power_ready_locked(const KebabA650GpuOobOwnerToken*oob,uint32_t phase)
{
 if(oob!=&kho_native.owner || !kho_native.base || !kvg_native.clock[3])return 95;
 uintptr_t g=kho_native.base,c=kvg_native.clock[3];
 uint32_t sp=*(volatile const uint32_t*)(g+0x14340),ao=*(volatile const uint32_t*)(g+0x24c14);
 uint32_t info=*(volatile const uint32_t*)(g+0x14648),cfg=*(volatile const uint32_t*)(c+0x1010),cx=*(volatile const uint32_t*)(c+0x106c);
 if((sp&0x40u) || ((sp&0x80u)&&phase!=KCP_HW_INIT) || (ao&0x29u) || info || !(cfg&0x10000u) || !(cx&0x80000000u))return 11;
 return 0;
}
extern "C" void kebab_a650_gpu_cp_quarantine_locked(const KebabA650GpuOobOwnerToken*oob)
{if(oob==&kho_native.owner)kebab_a650_gpu_oob_vm_quarantine_locked(&kvg_native.owner);}
static int KCPNativeGate(void*cookie)
{auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || KFNativeWorker(NULL))return 95;return kebab_rpmh_gpu_cp_check_locked(&n->owner);}
static int KCPNativeRead(void*cookie,unsigned space,uint32_t off,uint32_t*out)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !out)return 22;int e=KCPNativeGate(n);if(e)return e;
 uintptr_t base=0;
 if(space==1 && (off==0x200 || off==0x14340 || off==0x24c48))base=kho_native.base;
 if(!space && !(off&3u)){
  static const uint32_t words[]={KCP_RBBM_STATUS,KCP_RBBM_STATUS3,KCP_RBBM_INT_0_STATUS,KCP_RBBM_INT_0_MASK,KCP_RBBM_CLOCK_CNTL,
   KCP_CP_SQE_CNTL,KCP_CP_RB_RPTR,KCP_CP_RB_WPTR,KCP_CP_HW_FAULT,KCP_CP_INTERRUPT_STATUS,KCP_CP_PROTECT_STATUS,
   KCP_CP_SQE_INSTR_BASE,KCP_CP_SQE_INSTR_BASE+1,KCP_CP_APRIV_CNTL,KCP_CP_RB_CNTL,KCP_CP_RB_BASE,KCP_CP_RB_BASE+1,
   KCP_CP_RB_RPTR_ADDR,KCP_CP_RB_RPTR_ADDR+1,KCP_GBIF_HALT,KCP_RBBM_GBIF_HALT,KCP_RBBM_SECVID_TSB_CNTL,
   KCP_RBBM_SECVID_TSB_TRUSTED_SIZE,KCP_RBBM_SECVID_TSB_TRUSTED_BASE,KCP_RBBM_SECVID_TSB_TRUSTED_BASE+1};
  for(unsigned i=0;i<sizeof words/sizeof words[0];i++)if(off==words[i]*4)base=n->base;
 }
 if(!base)return 22;*out=*(volatile const uint32_t*)(base+off);return 0;
}
static void KCPNativeBarrier(void*)
{
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 OSMemoryBarrier();
#endif
}
/* This sequence is independently bounded in the adapter as well as the core:
 * exactly148 stores, including two distinct WPTR values, never WPTR0/retry. */
static int KCPNativeExpectedWrite(KCPAccess*n,unsigned*space,uint32_t*off,uint32_t*value)
{
 unsigned i=n->write_index;*space=0;
 if(i<11){*off=KCPHwPrefix[i].word*4;*value=KCPHwPrefix[i].value;return 0;}i-=11;
 if(!i){*space=1;*off=0x200;*value=n->observation->receipt.SPT_control_before&~1u;return 0;}--i;
 if(i<49){*off=KCPHwcg[i].word*4;*value=KCPHwcg[i].value;return 0;}i-=49;
 if(!i){*space=1;*off=0x200;*value=n->observation->receipt.SPT_control_before|1u;return 0;}--i;
 if(!i){*off=KCP_RBBM_CLOCK_CNTL*4;*value=0x8aa8aa82;return 0;}--i;
 if(i<74){*off=KCPHwPostClock[i].word*4;*value=KCPHwPostClock[i].value;return 0;}i-=74;
 static const KCPReg tail[]={
  {KCP_CP_SQE_INSTR_BASE,KCP_SQE_IOVA},{KCP_CP_SQE_INSTR_BASE+1,0},
  {KCP_CP_RB_BASE,KCP_RING_IOVA},{KCP_CP_RB_BASE+1,0},{KCP_CP_RB_CNTL,0x209},
  {KCP_CP_RB_RPTR_ADDR,KCP_FENCE_IOVA},{KCP_CP_RB_RPTR_ADDR+1,0},{KCP_CP_SCRATCH2,0},
  {KCP_CP_SQE_CNTL,1},{KCP_CP_RB_WPTR,KCP_INIT_WPTR},{KCP_CP_RB_WPTR,KCP_FINAL_WPTR}};
 if(i>=sizeof tail/sizeof tail[0])return 95;*off=tail[i].word*4;*value=tail[i].value;return 0;
}
static int KCPNativeWrite(void*cookie,unsigned space,uint32_t off,uint32_t value)
{
 auto*n=(KCPAccess*)cookie;
 if(n!=&kcp_native || !KCPNativeBound() || !n->base || !n->cpu_ready || !n->observation)return 95;
 int e=KCPNativeGate(n);if(e)return e;unsigned s=0;uint32_t o=0,v=0;
 e=KCPNativeExpectedWrite(n,&s,&o,&v);if(e || s!=space || o!=off || v!=value)return 95;
 if(n->write_index==147 && (n->copy_index!=3 || n->publish_index!=2 || !n->observation->receipt.CP_ME_INIT_completed))return 95;
 uintptr_t address=(space?kho_native.base:n->base)+off;++n->write_index;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint32_t*)address=value;
#endif
 return 0;
}
static int KCPNativeFirmware(KCPAccess*n)
{
 DTEntry entry=NULL;const void *property=NULL;unsigned int bytes=0;
 if(SecureDTLookupEntry(NULL,"/chosen",&entry)!=kSuccess || !entry ||
 SecureDTGetProperty(entry,"kebab-gpu-sqe-firmware",&property,&bytes)!=kSuccess || !property || bytes!=31964)return 95;
 n->firmware=(uint8_t*)IOMalloc(31964);if(!n->firmware)return ENOMEM;
 /* One copy, then hash and consume only the private copy. No pointer lease
  * on SecureDT; full original ADT owner measurement includes this property. */
 memcpy(n->firmware,property,31964);uint8_t digest[32];SHA256_CTX context;
 SHA256_Init(&context);SHA256_Update(&context,n->firmware,31964);SHA256_Final(digest,&context);
 static const uint8_t expected[32]={0xc8,0x63,0xce,0xac,0x3a,0x82,0x3a,0xf2,0x15,0x20,0x5d,0x78,0x03,0x77,0x6b,0xfd,
  0xc6,0x80,0x98,0xc2,0xc2,0xa5,0xd2,0xfd,0x05,0xb4,0xaa,0x77,0xcd,0x33,0x6b,0x4b};
 uint32_t header=1,version=0;memcpy(&header,n->firmware,4);memcpy(&version,n->firmware+4,4);
 if(memcmp(digest,expected,32) || header || (version&0xfffu)!=0x112)return 13;
 n->firmware_verified=1;return 0;
}
static int KCPNativePrepareCpu(void*cookie,KebabA650CpRecord*r)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !r || !n->observation || r!=&n->observation->receipt ||
 !n->firmware || n->firmware_verified!=1 || n->cpu_ready || r->phase!=KCP_EMPTY)return 95;
 int e=KCPNativeGate(n);if(e)return e;auto*d=&kvg_engine.backing;
 /* Revalidate private descriptors and all existing native physical pages,
  * including NC attributes, before opening this finite CPU-data update. */
 for(unsigned i=0;i<6;i++){
  const auto*p=&d->region[i];if(!p->cpu || p->cpu>UINTPTR_MAX-p->allocation_bytes)return 13;
  for(unsigned j=0;j<i;j++){const auto*q=&d->region[j];if(p->cpu<q->cpu+q->allocation_bytes && q->cpu<p->cpu+p->allocation_bytes)return 13;}
  if(i>=4)for(uint32_t off=0;off<p->allocation_bytes;off+=PAGE_SIZE)
   if((pmap_cache_attributes((ppnum_t)((p->physical+off)>>PAGE_SHIFT))&VM_WIMG_MASK)!=VM_WIMG_WCOMB)return 13;
 }
 uint32_t sqe=*(volatile const uint32_t*)(n->base+KCP_CP_SQE_CNTL*4);
 if((sqe!=0 && sqe!=3) || *(volatile const uint32_t*)(n->base+KCP_CP_RB_WPTR*4) ||
 *(volatile const uint32_t*)(n->base+KCP_CP_RB_RPTR*4) || (*(volatile const uint32_t*)(n->base+KCP_RBBM_STATUS*4)&~1u))return 13;
 memcpy((void*)d->region[2].cpu,n->firmware+4,16384);
 memcpy((void*)d->region[3].cpu,n->firmware+4+16384,31960-16384);
 if(memcmp((const void*)d->region[2].cpu,n->firmware+4,16384) ||
 memcmp((const void*)d->region[3].cpu,n->firmware+4+16384,31960-16384))return 5;
 flush_dcache((vm_offset_t)d->region[2].cpu,16384,0);flush_dcache((vm_offset_t)d->region[3].cpu,16384,0);
 KCPNativeBarrier(n);
 volatile uint32_t*f=(volatile uint32_t*)d->region[5].cpu;f[0]=0;f[1]=0;f[16]=KCP_SENTINEL;KCPNativeBarrier(n);
 r->firmware_bytes=31964;r->payload_bytes=31960;r->firmware_version=0x112;r->firmware_verified=1;
 r->split_copy_verified=1;r->NC_verified=1;n->cpu_ready=1;return KCPNativeGate(n);
}
static int KCPNativeCopy(void*cookie,unsigned region,uint32_t offset,const uint32_t*words,uint32_t count)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !n->cpu_ready || !n->observation || !words)return 95;
 int e=KCPNativeGate(n);if(e)return e;auto*r=&n->observation->receipt;
 const uint32_t*expected=NULL;unsigned want_region=0,want_offset=0,want_count=0;
 if(n->copy_index==0){expected=r->ib;want_region=1;want_count=4;}
 if(n->copy_index==1){expected=r->ring_init;want_count=16;}
 if(n->copy_index==2 && r->CP_ME_INIT_completed){expected=r->ring_second;want_offset=16;want_count=11;}
 if(!expected || region!=want_region || offset!=want_offset || count!=want_count || memcmp(words,expected,count*4))return 95;
 memcpy((void*)(kvg_engine.backing.region[region].cpu+offset*4),words,count*4);++n->copy_index;return KCPNativeGate(n);
}
static int KCPNativePublish(void*cookie)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !n->cpu_ready || n->publish_index>=2 || n->copy_index!=n->publish_index+2)return 95;
 int e=KCPNativeGate(n);if(e)return e;
 for(unsigned i=0;i<2;i++)flush_dcache((vm_offset_t)kvg_engine.backing.region[i].cpu,4096,0);
 KCPNativeBarrier(n);++n->publish_index;return KCPNativeGate(n);
}
static int KCPNativeReadNc(void*cookie,uint32_t offset,uint32_t*out)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !n->cpu_ready || !out || (offset!=0 && offset!=4 && offset!=64))return 95;
 int e=KCPNativeGate(n);if(e)return e;
 *out=*(volatile const uint32_t*)(kvg_engine.backing.region[5].cpu+offset);
#if defined(__aarch64__)
 __asm__ volatile("dmb oshld":::"memory");
#else
 OSMemoryBarrier();
#endif
 return KCPNativeGate(n);
}
static int KCPNativeSnapshot(void*cookie,unsigned after)
{
 auto*n=(KCPAccess*)cookie;if(n!=&kcp_native || !n->observation || after>1 || KFNativeWorker(NULL))return 95;
 return KHO_VM_Snapshot(&kvg_native.owner,after?&n->observation->vm_after:&n->observation->vm_before);
}
static void KCPNativeDelay(void*,unsigned us){if(us==100)IODelay(100);}
extern "C" int kebab_a650_gpu_cp_prepare(void)
{
 int e=KFNativeWorker(NULL);if(e)return e;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kcp_native_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return expected==2?kcp_native_result:EBUSY;
 auto*n=&kcp_native;n->observation=(KebabA650CpObservation*)IOMalloc(sizeof *n->observation);
 if(!n->observation){e=ENOMEM;goto done;}memset(n->observation,0,sizeof *n->observation);KebabA650CpInit(&n->observation->receipt);
 if(!KCPNativeBound()){e=95;goto done;}
 e=kebab_a650_gpu_cp_context_ready_locked(&kho_native.owner);if(e)goto done;
 e=KCPNativeFirmware(n);if(e)goto done;
 n->memory=IOMemoryDescriptor::withPhysicalAddress(0x03d00000,0x40000,kIODirectionInOut);if(!n->memory){e=ENOMEM;goto done;}
 n->mapping=n->memory->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!n->mapping || n->mapping->getLength()!=0x40000 || !(n->base=n->mapping->getVirtualAddress())){e=ENOMEM;goto done;}
 {KebabA650CpOwnerIo io={{n,KCPNativeGate,KCPNativeRead,KCPNativeWrite,KCPNativeBarrier,KCPNativeDelay,KVGNativeNow,
    KCPNativePrepareCpu,KCPNativeCopy,KCPNativePublish,KCPNativeReadNc},KCPNativeSnapshot};
  e=kebab_rpmh_gpu_cp_first_execution(&n->owner,&kho_native.owner,&io,&n->observation->receipt);}
done:
 if(!n->owner.pinned){n->base=0;if(n->mapping){n->mapping->release();n->mapping=NULL;}if(n->memory){n->memory->release();n->memory=NULL;}
  if(n->firmware){IOFree(n->firmware,31964);n->firmware=NULL;}}
 if(n->observation){auto*r=&n->observation->receipt;r->result=(uint32_t)e;if(e&&!r->phase)r->phase=KCP_FAILED;}
 kcp_native_result=e;__atomic_store_n(&kcp_native_once,2,__ATOMIC_RELEASE);return e;
}
extern "C" int kebab_a650_gpu_cp_snapshot(KebabA650CpObservation*out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kcp_native_once,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kcp_native.observation)memcpy(out,kcp_native.observation,sizeof *out);
 else{memset(out,0,sizeof *out);KebabA650CpInit(&out->receipt);out->receipt.result=(uint32_t)kcp_native_result;out->receipt.phase=KCP_FAILED;}
 return 0;
}
#endif
