/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_CXMX_H
#define KEBAB_NATIVE_GPU_CXMX_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>
#include <kern/KebabGpuCxMxOwner.h>
#include <kern/KebabGpuCxMxNativeBinding.h>
/* Actual finite MMIO adapter candidate. No hardware call is made while the
 * compiled binding is unqualified. Board/normal-worker check is mandatory;
 * it must exclude panic and teardown throughout this normal worker call.
 * RSC CPU always-on aperture comes from original SM8250 DTB.
 * There is no GPUCC/GMU/GX/SMMU, ARC-address MMIO or bus-fault recovery. */
struct KebabNativeCxMxAccess {uintptr_t rsc;void *cookie;int (*check)(void *);};
static int kebab_native_cxmx_read(void *cookie,uint32_t offset,uint32_t *out)
{
 KebabNativeCxMxAccess *a=(KebabNativeCxMxAccess *)cookie;
 if(!a || !a->rsc || !out || !KebabRscQueryAllowed(offset) || offset>0x10000-4)return 22;
 if(!a->check || a->check(a->cookie))return 95;
 *out=*(volatile const uint32_t *)(a->rsc+offset);return 0;
}
static int kebab_native_cxmx_write(void *cookie,uint32_t offset,uint32_t value)
{
 KebabNativeCxMxAccess *a=(KebabNativeCxMxAccess *)cookie;
 if(!a || !a->rsc || !KebabGpuCxMxWriteAllowed(offset) || !a->check || a->check(a->cookie))return 95;
 if(offset==KebabCxMxIrqClear && value!=2)return 22;
 if(offset==KebabCxMxControl && value!=0 && value!=0x10000 && value!=0x1010000)return 22;
 if((offset==KebabCxMxWait || offset==KebabCxMxEnable) && value>1)return 22;
 if(offset==KebabCxMxMsg && value!=0x10108)return 22;
 if(offset==KebabCxMxAddress && value!=0x30010 && value!=0x30000 && value!=0x42f04)return 22;
 if(offset==KebabCxMxData && value!=5 && value!=1)return 22;
 *(volatile uint32_t *)(a->rsc+offset)=value;
 /* Drain this actual store even if teardown is noticed by the next
  * callback. The existing native VRM writer also uses unconditional dsb. */
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_cxmx_barrier(void *cookie)
{
 KebabNativeCxMxAccess *a=(KebabNativeCxMxAccess *)cookie;
 if(!a || !a->check || a->check(a->cookie))return 95;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static uint64_t kebab_native_cxmx_now_us(void *)
{uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int kebab_native_gpu_cxmx(unsigned action,void *cookie,int (*check)(void *),KebabGpuCxMxRecord *out)
{
 IOMemoryDescriptor *rsc_memory=0;IOMemoryMap *rsc_map=0;int result=22;
 KebabNativeCxMxAccess access={0,cookie,check};
 if(!out)return 22;KebabGpuCxMxInitRecord(out);
 if(action>2)return 22;if(!check || check(cookie)){out->result=95;return 95;}
 if(action!=KebabGpuCxMxAcquire)return kebab_rpmh_gpu_cxmx(action,0,0,out);
 if(!KebabGpuCxMxBound(&KebabGpuCxMxNativeBinding)){out->gate_reason=1;out->result=95;return 95;}
 rsc_memory=IOMemoryDescriptor::withPhysicalAddress(0x18220000,0x10000,kIODirectionInOut);
 if(!rsc_memory)goto no_memory;
 rsc_map=rsc_memory->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!rsc_map || rsc_map->getLength()<0x10000)goto no_memory;
 access.rsc=rsc_map->getVirtualAddress();if(!access.rsc)goto no_memory;
 {
  KebabGpuCxMxIo io={&access,kebab_native_cxmx_read,kebab_native_cxmx_write,kebab_native_cxmx_barrier,kebab_native_cxmx_now_us};
  result=kebab_rpmh_gpu_cxmx(action,&io,&KebabGpuCxMxNativeBinding,out);
 }
 goto done;
no_memory:out->result=12;result=12;
done:
 if(rsc_map)rsc_map->release();if(rsc_memory)rsc_memory->release();return result;
}
#endif
