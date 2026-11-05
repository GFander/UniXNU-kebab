/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_HFI_H
#define KEBAB_NATIVE_GMU_HFI_H
/* Same normal-worker TU after NativeGmuFirmware. No CPU shared pointer or
 * user-supplied queue byte is exported or accepted. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuHfiOwner.h>
#include <kern/KebabGmuHfiBinding.h>
#else
#include "KebabGmuHfiOwner.h"
#include "KebabGmuHfiBinding.h"
#endif
#include "KebabA650GmuShared.h"
struct KebabNativeGmuHfiAccess {KebabNativeGmuFirmwareAccess *firmware;IOMemoryDescriptor *memory[2];IOMemoryMap *mapping[2];uintptr_t address[2];};
static KebabNativeGmuHfiAccess kebab_native_gmu_hfi;
static uint32_t kebab_native_gmu_hfi_once;
static KebabGmuHfiRecord kebab_native_gmu_hfi_result;
static int kebab_native_gmu_hfi_gate(void *cookie)
{
 auto *a=(KebabNativeGmuHfiAccess *)cookie;if(!a || !a->firmware || !a->firmware->boot)return 95;
 int e=kebab_native_gmu_boot_worker(a->firmware->boot);if(e)return e;
 return kebab_gpu_gmu_hfi_gate_locked(a->firmware->boot->cold,a->firmware->boot->epoch);
}
static int kebab_native_gmu_hfi_read_shared(void *cookie,uint32_t off,uint32_t *v)
{auto *a=(KebabNativeGmuHfiAccess *)cookie;return kebab_a650_gmu_hfi_read(&a->firmware->boot->source,off,v);}
static int kebab_native_gmu_hfi_write_shared(void *cookie,uint32_t off,uint32_t v)
{auto *a=(KebabNativeGmuHfiAccess *)cookie;return kebab_a650_gmu_hfi_write(&a->firmware->boot->source,off,v);}
static int kebab_native_gmu_hfi_read_reg(void *cookie,uint32_t off,uint32_t *v)
{
 auto *a=(KebabNativeGmuHfiAccess *)cookie;if(!v || (off!=0x14648 && off!=0x24c14))return 22;int e=kebab_native_gmu_hfi_gate(a);if(e)return e;
 unsigned i=off==0x14648?0:1;uint32_t base=i?0x24000:0x12000;if(!a->address[i])return 95;*v=*(volatile const uint32_t *)(a->address[i]+(off-base));return 0;
}
static int kebab_native_gmu_hfi_write_reg(void *cookie,uint32_t off,uint32_t v)
{
 auto *a=(KebabNativeGmuHfiAccess *)cookie;if(v!=1 || (off!=0x14644 && off!=0x14650))return 22;int e=kebab_native_gmu_hfi_gate(a);if(e)return e;
 if(!a->address[0])return 95;*(volatile uint32_t *)(a->address[0]+(off-0x12000))=1;return 0;
}
static int kebab_native_gmu_hfi_barrier(void *cookie)
{auto *a=(KebabNativeGmuHfiAccess *)cookie;int e=kebab_native_gmu_hfi_gate(a);return e?e:kebab_a650_gmu_hfi_barrier(&a->firmware->boot->source);}
static int kebab_native_gmu_hfi_delay(void *cookie,uint32_t us)
{int e=kebab_native_gmu_hfi_gate(cookie);if(e)return e;if(us!=100)return 22;IODelay(us);return 0;}
static void kebab_native_gmu_hfi_unmap(KebabNativeGmuHfiAccess *a)
{for(unsigned n=2;n;){--n;a->address[n]=0;if(a->mapping[n]){a->mapping[n]->release();a->mapping[n]=NULL;}if(a->memory[n]){a->memory[n]->release();a->memory[n]=NULL;}}if(a->firmware && a->firmware->boot)kebab_native_gmu_boot_unmap(a->firmware->boot);}
static int kebab_native_gmu_hfi_version(void *cookie,int(*worker_check)(void *),KebabGmuHfiRecord *out)
{
 auto *a=&kebab_native_gmu_hfi;uint32_t expected=0;int e=95;if(!out)return 22;KebabGmuHfiInit(out);
 if(!KebabGmuHfiVersionQualified || !worker_check || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled() || worker_check(cookie))return 95;
 if(!__atomic_compare_exchange_n(&kebab_native_gmu_hfi_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2){out->result=16;return 16;}*out=kebab_native_gmu_hfi_result;out->evidence_replayed=1;return (int)out->result;}
 a->firmware=&kebab_native_gmu_firmware;
 if(kebab_native_gmu_firmware_once!=2 || kebab_native_gmu_firmware_result.result || kebab_native_gmu_firmware_result.phase!=KGB_HFI_QUEUES_READY)goto done;
 a->firmware->boot->cookie=cookie;a->firmware->boot->bus_check=worker_check;
 e=kebab_gpu_first_ready_locked(a->firmware->boot->cold,a->firmware->boot->epoch);if(e)goto done;
 {
  const uint32_t pa[4]={0x00170000,0x00100000,0x00152000,0x03d90000},bytes[4]={0x4000,0x1000,0x1000,0x4000};
  for(unsigned i=0;i<4;i++){e=kebab_native_gmu_boot_map(a->firmware->boot,i,pa[i],bytes[i]);if(e)goto done;}
  for(unsigned i=0;i<2;i++){
   const uint32_t base[2]={0x03d7c000,0x03d8e000},bytes2[2]={0x4000,0x1000};
   a->memory[i]=IOMemoryDescriptor::withPhysicalAddress(base[i],bytes2[i],i?kIODirectionIn:kIODirectionInOut);if(!a->memory[i]){e=12;goto done;}
   a->mapping[i]=a->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(i?kIOMapReadOnly:0));
   if(!a->mapping[i] || a->mapping[i]->getLength()!=bytes2[i] || !(a->address[i]=a->mapping[i]->getVirtualAddress())){e=12;goto done;}
  }
  auto *b=a->firmware->boot;
  KebabGmuBootIo clocks={b,kebab_native_gmu_boot_read,kebab_native_gmu_boot_write,kebab_native_gmu_boot_barrier,kebab_native_gmu_boot_delay,kebab_native_gmu_boot_now,NULL,NULL,NULL,NULL};
  KebabGmuHfiIo io={a,kebab_native_gmu_hfi_gate,kebab_native_gmu_hfi_read_shared,kebab_native_gmu_hfi_write_shared,kebab_native_gmu_hfi_read_reg,kebab_native_gmu_hfi_write_reg,kebab_native_gmu_hfi_barrier,kebab_native_gmu_hfi_delay,kebab_native_gmu_boot_now};
  e=kebab_rpmh_gpu_gmu_hfi_version(b->cold,b->epoch,&a->firmware->resources,a->firmware->irq,&clocks,&io,out);
 }
done:
 out->result=e;if(e && !out->io_error)out->io_error=e;kebab_native_gmu_hfi_unmap(a);kebab_native_gmu_hfi_result=*out;__atomic_store_n(&kebab_native_gmu_hfi_once,2,__ATOMIC_RELEASE);return e;
}
#endif
