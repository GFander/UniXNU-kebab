/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_LAUNCH_RESOURCES_H
#define KEBAB_NATIVE_GPU_LAUNCH_RESOURCES_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm/machine_routines.h>
#include <kern/clock.h>
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuLaunchResourceOwner.h>
#else
#include "KebabGpuLaunchResourceOwner.h"
#endif
extern "C" int get_preemption_level(void);
struct KLRNative {
 IOMemoryDescriptor *memory[5];IOMemoryMap *mapping[5];uintptr_t address[5];
 void *worker_cookie;int (*worker_check)(void *);
 uint32_t hidden_raw[6],hidden_valid,hidden_non_echo;
};
static KLRNative *klr_native;
static uint32_t klr_native_once;
static KebabGpuLaunchResourceRecord klr_native_result;
static const KebabGpuColdToken *klr_native_cold;
static KebabGpuLaunchResourceToken *klr_native_token;
static uint64_t klr_native_epoch;
static int KLRNativeWorker(KLRNative *a)
{
 if(!a || !a->worker_check || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return a->worker_check(a->worker_cookie);
}
static int KLRNativeAddress(KLRNative *a,unsigned region,uint32_t off,uintptr_t *address)
{
 const uint32_t base[5]={0,0,0,0,0x71000};if(!a || region>=5 || !address || !a->address[region] || !KLRReadAllowed(region,off))return 22;
 int e=KLRNativeWorker(a);if(e)return e;*address=a->address[region]+off-base[region];return 0;
}
static int KLRNativeRead(void *cookie,unsigned region,uint32_t off,uint32_t *out)
{
 if(!out)return 22;uintptr_t address;int e=KLRNativeAddress((KLRNative *)cookie,region,off,&address);if(e)return e;
#if defined(__aarch64__)
 __asm__ volatile("ldr %w0, [%1]":"=r"(*out):"r"(address):"memory");
#else
 *out=*(volatile const uint32_t *)address;
#endif
 if(region==KLR_RSC){int i=KLRHiddenCommandIndex(off);if(i>=0){
  KLRNative *a=(KLRNative *)cookie;a->hidden_raw[i]=*out;a->hidden_valid|=1u<<(unsigned)i;
  if(*out!=KLRHiddenCommandExpected((unsigned)i))a->hidden_non_echo|=1u<<(unsigned)i;
 }}
 return 0;
}
static int KLRNativeWrite(void *cookie,unsigned region,uint32_t off,uint32_t value)
{
 if(!KLRWriteAllowed(region,off,value))return 22;uintptr_t address;int e=KLRNativeAddress((KLRNative *)cookie,region,off,&address);if(e)return e;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]"::"r"(value),"r"(address):"memory");
#else
 *(volatile uint32_t *)address=value;
#endif
 return 0;
}
static int KLRNativeBarrier(void *cookie)
{int e=KLRNativeWorker((KLRNative *)cookie);if(e)return e;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#endif
 return 0;}
static int KLRNativeDelay(void *cookie,unsigned us)
{int e=KLRNativeWorker((KLRNative *)cookie);if(e)return e;if(us!=100)return 22;IODelay(us);return 0;}
static uint64_t KLRNativeNow(void *)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static void KLRNativeHiddenDiagnostic(const KLRNative *a)
{
 /* CPU-only record publication after the canonical transaction returns.
  * No MMIO, callback, IRQ/RSC lock, implicit retry or success claim. */
 if(!a || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return;
 const uint32_t offsets[6]={0x38,0x34,0x40,0x3c,0x48,0x44};
 for(unsigned i=0;i<6;i++)if(a->hidden_valid&(1u<<i))
  printf("KEBAB GMURSC: hidden-diagnostic; offset=0x%02x; raw=0x%08x; written=0x%08x; non-echo=%u; not-ACK\n",
   offsets[i],a->hidden_raw[i],KLRHiddenCommandExpected(i),(a->hidden_non_echo>>i)&1);
}
/* Kernel worker only. Map allocations happen BEFORE canonical busy. Every
 * successful/uncertain access and all RootChild refs stay pinned for the boot;
 * no release/restore/reset/standalone userspace/CAP trigger is exposed. */
static int kebab_native_gpu_launch_resources_prepare(const KebabGpuColdToken *cold,uint64_t epoch,
 KebabGpuLaunchResourceToken *token,KebabGpuLaunchResourceRecord *out,void *worker_cookie,int (*worker_check)(void *))
{
 if(!out || !token || !cold || !worker_check)return 22;KLRInit(out);uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&klr_native_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2){out->result=16;return 16;}
  if(cold!=klr_native_cold || token!=klr_native_token || epoch!=klr_native_epoch){out->result=95;return 95;}
  *out=klr_native_result;return (int)out->result;
 }
 klr_native_cold=cold;klr_native_token=token;klr_native_epoch=epoch;
 int e=95;KLRNative probe={};probe.worker_cookie=worker_cookie;probe.worker_check=worker_check;
 if((e=KLRNativeWorker(&probe)))goto done;
 klr_native=(KLRNative *)IOMalloc(sizeof *klr_native);if(!klr_native){e=12;goto done;}memset(klr_native,0,sizeof *klr_native);
 klr_native->worker_cookie=worker_cookie;klr_native->worker_check=worker_check;
 {
  const uint64_t pa[5]={0x03de0000,0x0b290000,0x03d6a000,0x03d90000,0x00171000};
  const uint32_t bytes[5]={0x10000,0x10000,0x30000,0x8000,0x1000};
  for(unsigned i=0;i<5;i++){
   klr_native->memory[i]=IOMemoryDescriptor::withPhysicalAddress(pa[i],bytes[i],i<3?kIODirectionInOut:kIODirectionIn);
   if(!klr_native->memory[i]){e=12;goto done;}
   klr_native->mapping[i]=klr_native->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(i>=3?kIOMapReadOnly:0));
   if(!klr_native->mapping[i] || klr_native->mapping[i]->getLength()!=bytes[i] || !(klr_native->address[i]=klr_native->mapping[i]->getVirtualAddress())){e=12;goto done;}
  }
  KebabGpuLaunchResourceIo io={klr_native,NULL,KLRNativeRead,KLRNativeWrite,KLRNativeBarrier,KLRNativeDelay,KLRNativeNow};
  e=kebab_rpmh_gpu_launch_resources_prepare(cold,epoch,token,&io,out);
 }
done:
 out->result=e;klr_native_result=*out;__atomic_store_n(&klr_native_once,2,__ATOMIC_RELEASE);
 KLRNativeHiddenDiagnostic(klr_native);return e;
}
#endif
