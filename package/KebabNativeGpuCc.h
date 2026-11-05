/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_CC_H
#define KEBAB_NATIVE_GPU_CC_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>
#include <kern/KebabGpuCcOwner.h>
/* Actual finite GCC/GPUCC adapter. Source contract assumes original v2.1
 * board and the existing CPU CFG_AHB/bus access owner. Check must reject
 * panic/teardown/reconfiguration. Real rail refs are checked in the SAME
 * canonical native owner under busy, never supplied by this callback.
 * No GPUCC access without that runtime owner check; no downstream MMIO. */
struct KebabNativeGpuCcAccess {uintptr_t gcc,cc;void *cookie;int (*check)(void *);uint32_t can_write;};
static int kebab_native_gpu_cc_read(void *cookie,unsigned region,uint32_t offset,uint32_t *out)
{
 KebabNativeGpuCcAccess *a=(KebabNativeGpuCcAccess *)cookie;uintptr_t address;
 if(!a || !out || !KebabGpuCcAllowed(region,offset))return 22;
 if(!a->check || a->check(a->cookie))return 95;
 if(region==0){if(!a->gcc || offset<0x70000 || offset-0x70000>0x4000-4)return 22;address=a->gcc+offset-0x70000;}
 else {if(!a->cc || offset>0x4000-4)return 22;address=a->cc+offset;}
 *out=*(volatile const uint32_t *)address;return 0;
}
static int kebab_native_gpu_cc_write(void *cookie,unsigned region,uint32_t offset,uint32_t value)
{
 KebabNativeGpuCcAccess *a=(KebabNativeGpuCcAccess *)cookie;uint32_t mask=KebabGpuCcWriteMask(region,offset),current;
 if(!a || !a->cc || !a->can_write || !mask)return 22;
 if(!a->check || a->check(a->cookie))return 95;
 current=*(volatile const uint32_t *)(a->cc+offset);
 if(((current^value)&KebabGpuCcControlMask(offset==0x1078?4:2)&~mask)!=0)return 22;
 /* This fixed profile never seizes HW control/override or clears retention. */
 if(offset==0x106c && ((value&6) || ((current&0x800) && !(value&0x800))))return 95;
 /* Hardware may change status since the core read. Never write stale
  * unowned bits: only the fixed owned mask is merged into this fresh word. */
 value=(current&~mask)|(value&mask);
 *(volatile uint32_t *)(a->cc+offset)=value;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_gpu_cc_barrier(void *cookie)
{
 KebabNativeGpuCcAccess *a=(KebabNativeGpuCcAccess *)cookie;if(!a || !a->check || a->check(a->cookie))return 95;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_gpu_cc_delay(void *cookie,uint32_t us)
{
 KebabNativeGpuCcAccess *a=(KebabNativeGpuCcAccess *)cookie;if(!a || !a->check || a->check(a->cookie))return 95;
 if(us!=1 && us!=10 && us!=100)return 22;IODelay(us);return 0;
}
static uint64_t kebab_native_gpu_cc_now_us(void *)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int kebab_native_gpu_cc(unsigned action,void *cookie,int (*normal_worker_bus_check)(void *),KebabGpuCcRecord *out)
{
 IOMemoryDescriptor *gcc_memory=0,*cc_memory=0;IOMemoryMap *gcc_map=0,*cc_map=0;int result=22;
 KebabNativeGpuCcAccess access={0,0,cookie,normal_worker_bus_check,action==KebabGpuCcEnableHold || action==KebabGpuCcRestore};
 if(!out)return 22;KebabGpuCcInit(out,action);if(action>3)return 22;
 if(!normal_worker_bus_check || normal_worker_bus_check(cookie)){out->result=95;return 95;}
 result=kebab_rpmh_gpu_cc(KebabGpuCcInspect,0,out);if(result)return result;out->action=action;
 if(action==KebabGpuCcInspect)return result;
 if(!out->parent_qualified){out->result=95;return 95;}
 if(action==KebabGpuCcEnableHold && out->held)return kebab_rpmh_gpu_cc(action,0,out);
 gcc_memory=IOMemoryDescriptor::withPhysicalAddress(0x00170000,0x4000,kIODirectionIn);if(!gcc_memory)goto no_memory;
 gcc_map=gcc_memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
 if(!gcc_map || gcc_map->getLength()<0x4000)goto no_memory;
 access.gcc=gcc_map->getVirtualAddress();if(!access.gcc)goto no_memory;
 cc_memory=IOMemoryDescriptor::withPhysicalAddress(0x03d90000,0x4000,access.can_write?kIODirectionInOut:kIODirectionIn);if(!cc_memory)goto no_memory;
 cc_map=cc_memory->map(kIOMapAnywhere|kIOMapInhibitCache|(access.can_write?0:kIOMapReadOnly));
 if(!cc_map || cc_map->getLength()<0x4000)goto no_memory;
 access.cc=cc_map->getVirtualAddress();if(!access.cc)goto no_memory;
 {
  KebabGpuCcIo io={&access,kebab_native_gpu_cc_read,kebab_native_gpu_cc_write,kebab_native_gpu_cc_barrier,kebab_native_gpu_cc_delay,kebab_native_gpu_cc_now_us};
  result=kebab_rpmh_gpu_cc(action,&io,out);
 }
 goto done;
no_memory:out->result=12;result=12;
done:
 if(cc_map)cc_map->release();if(cc_memory)cc_memory->release();
 if(gcc_map)gcc_map->release();if(gcc_memory)gcc_memory->release();return result;
}
#endif
