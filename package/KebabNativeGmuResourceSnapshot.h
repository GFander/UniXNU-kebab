/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_RESOURCE_SNAPSHOT_H
#define KEBAB_NATIVE_GMU_RESOURCE_SNAPSHOT_H
#include "KebabNativeGmuHfiInitHold.h"
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuResourceObserveOwner.h>
#else
#include "KebabGmuResourceObserveOwner.h"
#endif
struct KebabNativeGmuResourceAccess {
 KebabNativeGmuBootAccess clock;
 IOMemoryDescriptor *memory[4];IOMemoryMap *mapping[4];uintptr_t address[4];
};
static int kebab_native_gmu_resource_gate(void *cookie)
{
 auto *a=(KebabNativeGmuResourceAccess *)cookie;if(!a)return 22;
 int e=kebab_native_gmu_boot_worker(&a->clock);return e?e:kebab_gpu_gmu_hfi_gate_locked(a->clock.cold,a->clock.epoch);
}
static int kebab_native_gmu_resource_read(void *cookie,uint32_t region,uint32_t off,uint32_t *out)
{
 auto *a=(KebabNativeGmuResourceAccess *)cookie;if(!out)return 22;unsigned index=0;bool allowed=false;
 switch(region){
 case KGRO_RSCC:index=0;allowed=off==0xd18||off==0xfb8||off==0x1258||off==0x14f8;break;
 case KGRO_DCVS:index=1;allowed=off==0xff4||off==0xff8||off==0xffc;break;
 case KGRO_HFI:index=2;allowed=off==0x23b0||off==0x2648;break;
 case KGRO_CC:allowed=off==0x100c||off==0x1010||off==0x106c||off==0x1508;break;
 case KGRO_AO:index=3;allowed=off==0xc14;break;
 default:return 22;
 }
 if(!allowed)return 22;int e=kebab_native_gmu_resource_gate(a);if(e)return e;
 uintptr_t base=region==KGRO_CC?a->clock.address[3]:a->address[index];if(!base)return 95;
 *out=*(volatile const uint32_t *)(base+off);return kebab_native_gmu_resource_gate(a);
}
/* Each OPEN obtains two new readonly status passes. No channel queue access,
 * command, IRQ acquire, register store, retained mapping or copied token. */
static int kebab_native_gmu_resource_snapshot(KebabGmuResourceObserveRecord *out)
{
 if(!out)return 22;KebabGmuResourceObserveInit(out);KebabNativeGmuResourceAccess a={};int e=95;
 auto *f=&kebab_native_gmu_firmware;
 if(kebab_gmu_fw_hold_worker(NULL) || __atomic_load_n(&kebab_native_gmu_hfi_init_once,__ATOMIC_ACQUIRE)!=2 || kebab_native_gmu_hfi_init_result.io.result || !f->boot || !f->irq)goto done;
 a.clock.cold=f->boot->cold;a.clock.epoch=f->boot->epoch;a.clock.bus_check=kebab_gmu_fw_hold_worker;
 {
  const uint64_t pa[8]={0x00170000,0x00100000,0x00152000,0x03d90000,0x03de0000,0x03d72000,0x03d7c000,0x03d8e000};
  const uint32_t bytes[8]={0x4000,0x1000,0x1000,0x4000,0x10000,0x1000,0x4000,0x1000};
  for(unsigned n=0;n<8;n++){
   if(kebab_gmu_fw_hold_worker(NULL)){e=95;goto done;}
   auto **memory=n<4?&a.clock.memory[n]:&a.memory[n-4];auto **mapping=n<4?&a.clock.mapping[n]:&a.mapping[n-4];auto *address=n<4?&a.clock.address[n]:&a.address[n-4];
   *memory=IOMemoryDescriptor::withPhysicalAddress(pa[n],bytes[n],kIODirectionIn);if(!*memory){e=12;goto done;}
   if(kebab_gmu_fw_hold_worker(NULL)){e=95;goto done;}
   *mapping=(*memory)->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
   if(!*mapping || (*mapping)->getLength()!=bytes[n] || !(*address=(*mapping)->getVirtualAddress())){e=12;goto done;}
  }
  KebabGmuBootIo clocks={&a.clock,kebab_native_gmu_boot_read,NULL,kebab_native_gmu_boot_barrier,NULL,kebab_native_gmu_boot_now,NULL,NULL,NULL,NULL};
  KebabGmuResourceObserveIo io={&a,kebab_native_gmu_resource_gate,kebab_native_gmu_resource_read,kebab_native_gmu_boot_now};
  e=kebab_rpmh_gpu_gmu_resource_observe(a.clock.cold,a.clock.epoch,&f->resources,f->irq,&clocks,&io,out);
 }
done:
 for(unsigned n=4;n;){--n;if(a.mapping[n])a.mapping[n]->release();if(a.memory[n])a.memory[n]->release();}
 kebab_native_gmu_boot_unmap(&a.clock);
 if(kebab_gmu_fw_hold_worker(NULL))e=95;out->result=(uint32_t)e;return 0;
}
static_assert(sizeof(KebabGmuResourceObserveRecord)==152,"CAP33 fixed ABI");
#endif
