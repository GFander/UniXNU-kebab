/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_BOOT_H
#define KEBAB_NATIVE_GMU_BOOT_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm/machine_routines.h>
#include <kern/clock.h>
#if defined(KERNEL)
#include <kern/KebabGmuBootOwner.h>
#include <kern/KebabGmuRootCore.h>
#include <kern/KebabGmuBootBinding.h>
#include <kern/KebabGpuFirstAttachNative.h>
#else
#include "KebabGmuBootOwner.h"
#include "KebabGmuRootCore.h"
#include "KebabGmuBootBinding.h"
#include "KebabGpuFirstAttachNative.h"
#endif
extern "C" int get_preemption_level(void);
struct KebabNativeGmuBootAccess {
 IOMemoryDescriptor *memory[6];IOMemoryMap *mapping[6];uintptr_t address[6];
 void *cookie;int (*bus_check)(void *);const KebabGpuColdToken *cold;uint64_t epoch;
 KebabA650GmuBackingToken source;KebabA650GmuTcmManifest manifest;
 uint32_t gmu_open,reset_requested,reset_observed,own_reads,own_writes;
};
static KebabNativeGmuBootAccess kebab_native_gmu_boot_access;
static uint32_t kebab_native_gmu_boot_once;
static KebabGmuBootRecord kebab_native_gmu_boot_result;
static int kebab_native_gmu_boot_worker(KebabNativeGmuBootAccess *a)
{
 if(ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return !a || !a->bus_check || a->bus_check(a->cookie)?95:0;
}
static int kebab_native_gmu_boot_check(KebabNativeGmuBootAccess *a)
{
 int e=kebab_native_gmu_boot_worker(a);if(e)return e;
 return kebab_gpu_gmu_boot_preflight_locked(a->cold,a->epoch);
}
static int kebab_native_gmu_boot_map(KebabNativeGmuBootAccess *a,unsigned i,uint64_t pa,uint32_t bytes)
{
 const unsigned rw=i==0 || i>=3;
 a->memory[i]=IOMemoryDescriptor::withPhysicalAddress(pa,bytes,rw?kIODirectionInOut:kIODirectionIn);if(!a->memory[i])return 12;
 a->mapping[i]=a->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(rw?0:kIOMapReadOnly));
 if(!a->mapping[i] || a->mapping[i]->getLength()!=bytes || !(a->address[i]=a->mapping[i]->getVirtualAddress()))return 12;
 return 0;
}
static void kebab_native_gmu_boot_unmap(KebabNativeGmuBootAccess *a)
{
 a->gmu_open=0;a->reset_requested=a->reset_observed=0;
 for(unsigned n=6;n;){--n;a->address[n]=0;if(a->mapping[n]){a->mapping[n]->release();a->mapping[n]=NULL;}if(a->memory[n]){a->memory[n]->release();a->memory[n]=NULL;}}
}
static int kebab_native_gmu_boot_address(KebabNativeGmuBootAccess *a,unsigned region,uint32_t offset,uintptr_t *address)
{
 if(!a || !address || (offset&3))return 22;unsigned index;uint32_t base;
 if(region<=1){
  if(KebabGmuRootAllowed(region,offset)){
   if(region==1){index=3;base=0;}else if(offset<0x1000){index=1;base=0;}
   else if(offset>=0x52000 && offset<0x53000){index=2;base=0x52000;}else{index=0;base=0x70000;}
  }else if(region==0 && (offset==0x71154 || offset==0x7100c)){index=0;base=0x70000;}
  else if(region==1 && offset==0x109c){index=3;base=0;}else return 22;
 }else if(region==2){
  if(!a->gmu_open || kebab_gpu_gmu_boot_gate_locked(a->cold,a->epoch))return 95;
  if(offset>=0x3000 && offset<0xb000){
   if(!a->reset_requested || !a->reset_observed)return 95;index=5;base=0x3000;
  }else if(offset==0x14000 || (offset==0x14730 && a->reset_requested && a->reset_observed)){index=4;base=0x12000;}
  else return 22;
 }else return 22;
 if(!a->address[index])return 95;*address=a->address[index]+offset-base;return 0;
}
static int kebab_native_gmu_boot_read(void *cookie,unsigned region,uint32_t offset,uint32_t *out)
{
 KebabNativeGmuBootAccess *a=(KebabNativeGmuBootAccess *)cookie;uintptr_t address;int e;
 if(!out)return 22;if((e=kebab_native_gmu_boot_check(a)))return e;
 if((e=kebab_native_gmu_boot_address(a,region,offset,&address)))return e;
 *out=*(volatile const uint32_t *)address;a->own_reads++;
 if(region==2 && offset==0x14000 && a->reset_requested)a->reset_observed=(*out&1)!=0;
 return 0;
}
static int kebab_native_gmu_boot_write(void *cookie,unsigned region,uint32_t offset,uint32_t mask,uint32_t bits,uint32_t expected)
{
 KebabNativeGmuBootAccess *a=(KebabNativeGmuBootAccess *)cookie;uintptr_t address;int e;
 if((e=kebab_native_gmu_boot_check(a)))return e;
 if(region==0 || region==1){
  if(mask!=1 || bits!=1 || !((region==0 && offset==0x71154) || (region==1 && (offset==0x109c || offset==0x1098))))return 22;
  if(kebab_gpu_gmu_boot_gate_locked(a->cold,a->epoch))return 95;
  if(offset==0x1098 && !a->cold->started)return 95;
  if((e=kebab_native_gmu_boot_address(a,region,offset,&address)))return e;
  uint32_t current=*(volatile const uint32_t *)address;
  if(((current^expected)&0x0ffffffeu)!=0)return 11;
  *(volatile uint32_t *)address=current|1u;a->own_writes++;return 0;
 }
 if(region!=2 || mask!=UINT32_MAX)return 22;
 if(offset==0x14000 || offset==0x14730){if(bits!=1)return 95;} /* no SYSRESET0 route */
 else if(offset<0x3000 || offset>=0xb000)return 22;
 if((e=kebab_native_gmu_boot_address(a,region,offset,&address)))return e;
 *(volatile uint32_t *)address=bits;a->own_writes++;
 if(offset==0x14000)a->reset_requested=1;
 return 0;
}
static int kebab_native_gmu_boot_barrier(void *cookie)
{
 int e=kebab_native_gmu_boot_check((KebabNativeGmuBootAccess *)cookie);if(e)return e;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_gmu_boot_delay(void *cookie,uint32_t us)
{int e=kebab_native_gmu_boot_check((KebabNativeGmuBootAccess *)cookie);if(e)return e;if(us!=1 && us!=100)return 22;IODelay(us);return 0;}
static uint64_t kebab_native_gmu_boot_now(void *)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int kebab_native_gmu_boot_source_check(void *cookie)
{KebabNativeGmuBootAccess *a=(KebabNativeGmuBootAccess *)cookie;return kebab_a650_gmu_tcm_check(&a->source);}
static int kebab_native_gmu_boot_word(void *cookie,uint32_t ordinal,uint32_t *offset,uint32_t *word)
{KebabNativeGmuBootAccess *a=(KebabNativeGmuBootAccess *)cookie;return kebab_a650_gmu_tcm_word(&a->source,ordinal,offset,word);}
static int kebab_native_gmu_boot_open(void *cookie)
{
 KebabNativeGmuBootAccess *a=(KebabNativeGmuBootAccess *)cookie;uint32_t v;int e;
 if(a->gmu_open || kebab_gpu_gmu_boot_gate_locked(a->cold,a->epoch))return 95;
 const unsigned region[6]={0,0,1,0,1,1};const uint32_t off[6]={0x71004,0x71000,0x1540,0x71154,0x109c,0x1098};
 for(unsigned i=0;i<6;i++){
  if((e=kebab_native_gmu_boot_read(a,region[i],off[i],&v)))return e;
  if(i==0 && (!(v&1) || (v&0x80000000u)))return 95;if(i==1 && (v&1))return 95;if(i==2 && !(v&0x80000000u))return 95;
  if(i>=3 && (!(v&1) || !KebabGmuBootRunning(v)))return 95;
 }
 e=kebab_native_gmu_boot_map(a,4,0x03d7c000,0x4000);if(e)return e;
 e=kebab_native_gmu_boot_map(a,5,0x03d6d000,0x8000);if(e)return e;
 a->gmu_open=1;return 0;
}
/* Normal-worker explicit one-shot. Replays only retained execution evidence;
 * no callback can enable GFX/GX, program PLL/RSC, unreset or start firmware. */
static int kebab_native_gmu_boot(unsigned action,void *cookie,int (*normal_worker_bus_check)(void *),KebabGmuBootRecord *out)
{
 KebabNativeGmuBootAccess *a=&kebab_native_gmu_boot_access;int e=95;uint32_t expected=0;
 if(!out || action>2)return 22;KebabGmuBootInit(out,action);
 if(action==KebabGmuBootInspect)return kebab_rpmh_gpu_gmu_boot(action,NULL,0,NULL,NULL,out);
 if(!KebabGmuBootResetLoadQualified || !normal_worker_bus_check || ml_at_interrupt_context() ||
    get_preemption_level() || !ml_get_interrupts_enabled() || normal_worker_bus_check(cookie)){out->result=95;return 95;}
 if(action==KebabGmuBootLaunch)return kebab_rpmh_gpu_gmu_boot(action,NULL,0,NULL,NULL,out);
 if(!__atomic_compare_exchange_n(&kebab_native_gmu_boot_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2){out->result=16;return 16;}
  *out=kebab_native_gmu_boot_result;out->action=action;out->evidence_replayed=1;return (int)out->result;
 }
 a->cookie=cookie;a->bus_check=normal_worker_bus_check;
 e=kebab_gpu_first_allow_run();if(e)goto failed;
 a->cold=kebab_gpu_first_run_lease(&a->epoch);if(!a->cold){e=95;goto failed;}
 {
  KebabA650GmuBackingDescription description;e=kebab_a650_gmu_backing_acquire(&a->source,&description);if(e)goto failed;
  e=kebab_a650_gmu_tcm_begin(&a->source,&a->manifest);if(e)goto failed;
  e=kebab_a650_gmu_backing_publish(&a->source);if(e)goto failed;
 }
 {
  const uint32_t pa[4]={0x00170000,0x00100000,0x00152000,0x03d90000},bytes[4]={0x4000,0x1000,0x1000,0x4000};
  for(unsigned i=0;i<4;i++){e=kebab_native_gmu_boot_map(a,i,pa[i],bytes[i]);if(e)goto failed;}
  KebabGmuBootIo io={a,kebab_native_gmu_boot_read,kebab_native_gmu_boot_write,kebab_native_gmu_boot_barrier,
   kebab_native_gmu_boot_delay,kebab_native_gmu_boot_now,NULL,kebab_native_gmu_boot_open,kebab_native_gmu_boot_source_check,kebab_native_gmu_boot_word};
  e=kebab_rpmh_gpu_gmu_boot(action,a->cold,a->epoch,&a->manifest,&io,out);
 }
 goto done;
failed:out->result=e;out->primary_error=e;
done:out->reads=a->own_reads;out->writes=a->own_writes;kebab_native_gmu_boot_unmap(a);kebab_native_gmu_boot_result=*out;__atomic_store_n(&kebab_native_gmu_boot_once,2,__ATOMIC_RELEASE);return e;
}
#endif
