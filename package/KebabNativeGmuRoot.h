/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_ROOT_H
#define KEBAB_NATIVE_GMU_ROOT_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>
#include <kern/KebabGmuRootOwner.h>
#include <kern/KebabGmuRootNativeBinding.h>
/* Finite controller mappings only. The rail/CC child pin is checked by the
 * actual canonical native owner under its globalbusy, not by this callback.
 * normal_worker_bus_check must reject panic/interrupt/teardown and loss of
 * the platform CPU control-bus owner. No downstream GMU/GPU/SMMU mapping. */
struct KebabNativeGmuRootAccess {uintptr_t address[4];void *cookie;int (*check)(void *);uint32_t can_write;};
static int kebab_native_gmu_root_address(KebabNativeGmuRootAccess *a,unsigned region,uint32_t offset,uintptr_t *address)
{
 unsigned i;uint32_t base,size;
 if(!a || !address || !KebabGmuRootAllowed(region,offset))return 22;
 if(region==1){i=3;base=0;size=0x4000;}
 else if(offset<0x1000){i=1;base=0;size=0x1000;}
 else if(offset>=0x52000 && offset<0x53000){i=2;base=0x52000;size=0x1000;}
 else{i=0;base=0x70000;size=0x4000;}
 if(!a->address[i] || offset<base || offset-base>size-4)return 22;
 *address=a->address[i]+offset-base;return 0;
}
static int kebab_native_gmu_root_read(void *cookie,unsigned region,uint32_t offset,uint32_t *out)
{
 KebabNativeGmuRootAccess *a=(KebabNativeGmuRootAccess *)cookie;uintptr_t address;int e;
 if(!out)return 22;if((e=kebab_native_gmu_root_address(a,region,offset,&address)))return e;
 if(!a->check || a->check(a->cookie))return 95;
 *out=*(volatile const uint32_t *)address;return 0;
}
static int kebab_native_gmu_root_write(void *cookie,unsigned region,uint32_t offset,uint32_t mask,uint32_t bits,uint32_t expected)
{
 KebabNativeGmuRootAccess *a=(KebabNativeGmuRootAccess *)cookie;uintptr_t address;unsigned slot,r;uint32_t off,current,allowed=KebabGmuRootWriteMask(region,offset);int e;
 if(!a || !a->can_write || !mask || (mask&~allowed))return 22;
 if((e=kebab_native_gmu_root_address(a,region,offset,&address)))return e;
 if(!a->check || a->check(a->cookie))return 95;
 for(slot=0;slot<16;slot++){(void)KebabGmuRootOffset(slot,&r,&off);if(region==r && offset==off)break;}
 if(slot==16)return 22;current=*(volatile const uint32_t *)address;
 if(((current^expected)&KebabGmuRootInvariantMask(slot)&~mask)!=0)return 11;
 if(region==1 && offset==0x1120 && ((current&1) || ((mask&1) && !(bits&1))))return 16;
 if(region==0 && offset==0x52018 && !(bits&1))return 95; /* Never unvote shared PLL. */
 /* Preserve every current unowned bit, including changing HW status. */
 *(volatile uint32_t *)address=(current&~mask)|(bits&mask);
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_gmu_root_barrier(void *cookie)
{
 KebabNativeGmuRootAccess *a=(KebabNativeGmuRootAccess *)cookie;if(!a || !a->check || a->check(a->cookie))return 95;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_gmu_root_delay(void *cookie,uint32_t us)
{KebabNativeGmuRootAccess *a=(KebabNativeGmuRootAccess *)cookie;if(!a || !a->check || a->check(a->cookie))return 95;if(us!=1 && us!=10)return 22;IODelay(us);return 0;}
static uint64_t kebab_native_gmu_root_now(void *)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int kebab_native_gmu_root(unsigned action,void *cookie,int (*normal_worker_bus_check)(void *),KebabGmuRootRecord *out)
{
 static const uint32_t physical[4]={0x00170000,0x00100000,0x00152000,0x03d90000},length[4]={0x4000,0x1000,0x1000,0x4000};
 IOMemoryDescriptor *memory[4]={0,0,0,0};IOMemoryMap *map[4]={0,0,0,0};unsigned i;int result=22;
 KebabNativeGmuRootAccess a={{0,0,0,0},cookie,normal_worker_bus_check,action==KebabGmuRootHold200 || action==KebabGmuRootRestore};
 if(!out)return 22;KebabGmuRootInit(out,action);if(action>3)return 22;
 if(!normal_worker_bus_check || normal_worker_bus_check(cookie)){out->result=95;return 95;}
 result=kebab_rpmh_gpu_gmu_root(KebabGmuRootInspect,0,out);if(result)return result;out->action=action;
 if(action==KebabGmuRootInspect)return result;
 if(!out->parent_qualified || (a.can_write && !kebab_gmu_root_native_binding_qualified)){out->result=95;return 95;}
 if(action==KebabGmuRootHold200 && out->held)return kebab_rpmh_gpu_gmu_root(action,0,out);
 for(i=0;i<4;i++){
  unsigned rw=a.can_write && i>=2;
  memory[i]=IOMemoryDescriptor::withPhysicalAddress(physical[i],length[i],rw?kIODirectionInOut:kIODirectionIn);if(!memory[i])goto no_memory;
  map[i]=memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(rw?0:kIOMapReadOnly));
  if(!map[i] || map[i]->getLength()<length[i])goto no_memory;
  a.address[i]=map[i]->getVirtualAddress();if(!a.address[i])goto no_memory;
 }
 {
  KebabGmuRootIo io={&a,kebab_native_gmu_root_read,kebab_native_gmu_root_write,kebab_native_gmu_root_barrier,kebab_native_gmu_root_delay,kebab_native_gmu_root_now};
  result=kebab_rpmh_gpu_gmu_root(action,&io,out);
 }
 goto done;
no_memory:out->result=12;result=12;
done:
 for(i=4;i>0;){--i;if(map[i])map[i]->release();if(memory[i])memory[i]->release();}return result;
}
#endif
