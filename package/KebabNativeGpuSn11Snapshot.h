/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_SN11_SNAPSHOT_H
#define KEBAB_NATIVE_GPU_SN11_SNAPSHOT_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm/machine_routines.h>
#include "KebabGpuSn11CmdDb.h"
extern "C" int get_preemption_level(void);
static int kebab_gpu_sn11_worker_check(void)
{return panic_active() || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()?95:0;}
static int kebab_native_gpu_sn11_snapshot(KebabGpuSn11Record *out)
{
 if(!out)return 22;KebabGpuSn11Init(out);
 IOMemoryDescriptor *memory=NULL;IOMemoryMap *mapping=NULL;uintptr_t address=0;int e=95,first=95,second=95;KebabGpuSn11Record again;
 if((e=kebab_gpu_sn11_worker_check()))goto done;
 /* The exact immutable RAM aperture already used by CAP11/15. This mapping
  * is CPU readonly and never dereferences a discovered command address. */
 memory=IOMemoryDescriptor::withPhysicalAddress(0x80860000,0x20000,kIODirectionIn);if(!memory){e=12;goto done;}
 if((e=kebab_gpu_sn11_worker_check()))goto done;
 mapping=memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
 if(!mapping || mapping->getLength()!=0x20000 || !(address=mapping->getVirtualAddress())){e=12;goto done;}
 if((e=kebab_gpu_sn11_worker_check()))goto done;
 first=KebabGpuSn11Pass((const volatile uint8_t *)address,0x20000,out);
 if((e=kebab_gpu_sn11_worker_check()))goto done;
 second=KebabGpuSn11Pass((const volatile uint8_t *)address,0x20000,&again);
 if((e=kebab_gpu_sn11_worker_check()))goto done;
 e=KebabGpuSn11Finish(out,&again,first,second);
done:
 if(mapping)mapping->release();if(memory)memory->release();
 if(kebab_gpu_sn11_worker_check())e=95;
 out->result=(uint32_t)e;if(e)out->valid=0;if(e==12 || e==95)out->resource.result=(uint32_t)e;
 return 0; /* Typed error observation, as CAP15; no MMIO or writer lease. */
}
#endif
