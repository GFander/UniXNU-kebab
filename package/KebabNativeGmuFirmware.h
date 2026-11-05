/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_FIRMWARE_H
#define KEBAB_NATIVE_GMU_FIRMWARE_H
/* Include after KebabNativeGmuBoot.h in the SAME normal-worker TU. The
 * private backing/cold pointers below are the retained reset/load objects. */
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuFirmwareOwner.h>
#include <kern/KebabGmuFirmwareBinding.h>
#else
#include "KebabGmuFirmwareOwner.h"
#include "KebabGmuFirmwareBinding.h"
#endif
#include "KebabNativeGpuLaunchResources.h"
struct KebabNativeGmuFirmwareAccess {
 KebabNativeGmuBootAccess *boot;IOMemoryDescriptor *memory[2];IOMemoryMap *mapping[2];uintptr_t address[2];
 KebabGpuLaunchResourceToken resources;const KebabGmuIrqToken *irq;uint64_t irq_generation;uint32_t reads,writes;
};
static KebabNativeGmuFirmwareAccess kebab_native_gmu_firmware;
static uint32_t kebab_native_gmu_firmware_once;
static KebabGmuBootRecord kebab_native_gmu_firmware_result;
static int kebab_native_gmu_firmware_borrowed_irq(const KebabGmuIrqToken *irq,KebabNativeGmuBootAccess *boot,uint64_t generation)
{
 /* healthy compares exact registered pointer before dereferencing it. No
  * reconstructed token or CAP can pass this pure, no-MMIO first check. */
 if(!boot || !irq || kebab_gpu_gmu_irq_healthy(irq))return 95;
 if(irq->cold!=boot->cold || irq->epoch!=boot->epoch || !irq->held || !irq->generation ||
    (generation && irq->generation!=generation))return 95;
 return 0;
}
static int kebab_native_gmu_firmware_resource_worker(void *cookie)
{
 auto *a=(KebabNativeGmuFirmwareAccess *)cookie;if(!a)return 95;
 int e=kebab_native_gmu_boot_worker(a->boot);if(e)return e;
 /* The real resource adapter invokes this before every RSC/PDC MMIO,
  * including after a concurrent IRQ-health/namespace revocation. */
 return kebab_native_gmu_firmware_borrowed_irq(a->irq,a->boot,a->irq_generation);
}
static int kebab_native_gmu_firmware_power(KebabNativeGmuFirmwareAccess *a)
{
 /* Read only the proven controller pages before ANY downstream access.
  * The real private root/rail/bus refs are checked separately; these are
  * positive current bus/clock/reset/rate gates, never a lease substitute. */
 const unsigned region[11]={0,0,1,1,1,0,0,1,1,1,1};
 const uint32_t offset[11]={0x71004,0x71000,0x106c,0x1540,0x1078,0x7100c,0x71154,0x109c,0x1098,0x1120,0x1124};
 uint32_t v[11],pll[16]={};int e;
 for(unsigned i=0;i<11;i++){e=kebab_native_gmu_boot_read(a->boot,region[i],offset[i],&v[i]);if(e)return e;}
 if((v[0]&0x80000001u)!=1 || (v[1]&1) || (v[2]&7) || !(v[3]&0x80000000u))return 95;
 /* ResetLoad already obtained the initial CM3 clock ACK. For CSR access the
  * held CM3 request/control is required; its later status is not a bus gate.
  * Real AHB/AXI/CXO/MEMNOC must remain live, as do the rail/root refs. */
 for(unsigned i=4;i<=8;i++)if(!(v[i]&1) || (i!=8 && !KebabGmuBootRunning(v[i])))return 95;
 if((v[9]&0x800000f3u)!=2 || (v[10]&0x10371fu)!=0x100602u)return 95;
 const unsigned slots[6]={9,10,11,13,14,15};
 for(unsigned i=0;i<6;i++){unsigned r;uint32_t off;KebabGmuRootOffset(slots[i],&r,&off);e=kebab_native_gmu_boot_read(a->boot,r,off,&pll[slots[i]]);if(e)return e;}
 if(KebabGmuRootPllShape(pll) || (pll[9]&0xc0000000u)!=0xc0000000u || !(pll[14]&1) || !(pll[15]&0x10000))return 95;
 return 0;
}
static int kebab_native_gmu_firmware_check(KebabNativeGmuFirmwareAccess *a)
{
 if(!a || !a->boot)return 95;int e=kebab_native_gmu_boot_check(a->boot);if(e)return e;
 e=kebab_gpu_gmu_launch_active_locked(a->boot->cold,a->boot->epoch);return e?e:kebab_native_gmu_firmware_power(a);
}
static int kebab_native_gmu_firmware_address(KebabNativeGmuFirmwareAccess *a,unsigned region,uint32_t off,uintptr_t *address)
{
 if(!a || !address || (off&3))return 22;
 if(region==KGB_GMU){
  const uint32_t csr[]={0x14000,0x14070,0x14600,0x143a0,0x14648,0x143c4,0x143c0,0x14004,0x14614,0x14610,
   0x140b4,0x14608,0x14400,0x1303c,0x13000,0x13004,0x14300,0x14404,0x14618};
  for(unsigned i=0;i<sizeof(csr)/sizeof(csr[0]);i++)if(off==csr[i]){
   if(!a->boot->address[4])return 95;*address=a->boot->address[4]+(off-0x12000);return 0;}
  if(off==0x24c14 || off==0x24c20 || off==0x24c1c || off==0x24c44){
   if(!a->address[0])return 95;*address=a->address[0]+(off-0x24000);return 0;}
 }else if(region==KGB_RSCC && off==0x404){if(!a->address[1])return 95;*address=a->address[1]+off;return 0;}
 return 22;
}
static int kebab_native_gmu_firmware_read(void *cookie,unsigned region,uint32_t off,uint32_t *v)
{
 KebabNativeGmuFirmwareAccess *a=(KebabNativeGmuFirmwareAccess *)cookie;uintptr_t address;int e;
 if(!v)return 22;if((e=kebab_native_gmu_firmware_check(a)))return e;
 if(region<=KGB_CC)return kebab_native_gmu_boot_read(a->boot,region,off,v);
 if((e=kebab_native_gmu_firmware_address(a,region,off,&address)))return e;
 *v=*(volatile const uint32_t *)address;a->reads++;return 0;
}
static int kebab_native_gmu_firmware_write(void *cookie,unsigned region,uint32_t off,uint32_t mask,uint32_t bits,uint32_t expected)
{
 KebabNativeGmuFirmwareAccess *a=(KebabNativeGmuFirmwareAccess *)cookie;uintptr_t address;int e;
 if((e=kebab_native_gmu_firmware_check(a)))return e;if(region!=KGB_GMU)return 22;
 if((e=kebab_native_gmu_firmware_address(a,region,off,&address)))return e;
 if(off==0x143a0){
  if(mask!=0xf11 || bits!=0xf11)return 22;uint32_t current=*(volatile const uint32_t *)address;a->reads++;
  if((current^expected)&~0xf11u)return 11;*(volatile uint32_t *)address=current|0xf11u;a->writes++;return 0;
 }
 if(mask!=UINT32_MAX)return 22;
 const uint32_t allow[][2]={{0x143c4,1},{0x143c0,1},{0x14070,0},{0x14004,2},{0x14614,0x60001000},{0x14610,1},
  {0x24c44,0x802800a0},{0x140b4,0x4052},{0x14608,0x06050200},{0x14400,0x60006000},{0x1303c,1},{0x13000,1},
  {0x13004,1},{0x14300,0x09c40400},{0x14404,0},{0x14618,1},{0x24c1c,2},{0x24c1c,0},{0x14000,0}};
 unsigned found=0;for(unsigned i=0;i<sizeof(allow)/sizeof(allow[0]);i++)if(off==allow[i][0] && bits==allow[i][1])found=1;
 if(!found)return 22;
 if(off==0x14000){
  /* Fresh real providers at the actual release write, not just at entry. */
  e=kebab_gpu_gmu_firmware_write_stage_locked(a->boot->cold,a->boot->epoch,1);if(e)return e;
  e=kebab_gpu_launch_resources_verify_locked(&a->resources,a->boot->cold,a->boot->epoch,KLR_NEED_RSC);if(e)return e;
  e=kebab_gpu_gmu_irq_verify_locked(a->irq,a->boot->cold,a->boot->epoch);if(e)return e;
  uint32_t reset=*(volatile const uint32_t *)address;a->reads++;if(!(reset&1))return 11;
 }
 if(off==0x14618){e=kebab_gpu_gmu_firmware_write_stage_locked(a->boot->cold,a->boot->epoch,2);if(e)return e;}
 *(volatile uint32_t *)address=bits;a->writes++;return 0;
}
static int kebab_native_gmu_firmware_barrier(void *cookie)
{KebabNativeGmuFirmwareAccess *a=(KebabNativeGmuFirmwareAccess *)cookie;int e=kebab_native_gmu_firmware_check(a);return e?e:kebab_native_gmu_boot_barrier(a->boot);}
static int kebab_native_gmu_firmware_delay(void *cookie,uint32_t us)
{KebabNativeGmuFirmwareAccess *a=(KebabNativeGmuFirmwareAccess *)cookie;int e=kebab_native_gmu_firmware_check(a);return e?e:kebab_native_gmu_boot_delay(a->boot,us);}
static int kebab_native_gmu_firmware_map(KebabNativeGmuFirmwareAccess *a,unsigned i,uint64_t pa,uint32_t bytes)
{
 a->memory[i]=IOMemoryDescriptor::withPhysicalAddress(pa,bytes,i?kIODirectionIn:kIODirectionInOut);if(!a->memory[i])return 12;
 a->mapping[i]=a->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(i?kIOMapReadOnly:0));
 if(!a->mapping[i] || a->mapping[i]->getLength()!=bytes || !(a->address[i]=a->mapping[i]->getVirtualAddress()))return 12;return 0;
}
static void kebab_native_gmu_firmware_unmap(KebabNativeGmuFirmwareAccess *a)
{for(unsigned n=2;n;){--n;a->address[n]=0;if(a->mapping[n]){a->mapping[n]->release();a->mapping[n]=NULL;}if(a->memory[n]){a->memory[n]->release();a->memory[n]=NULL;}}kebab_native_gmu_boot_unmap(a->boot);}
static int kebab_native_gmu_firmware_launch(const KebabGmuIrqToken *borrowed_irq,void *cookie,int(*worker_check)(void *),KebabGmuBootRecord *out)
{
 KebabNativeGmuFirmwareAccess *a=&kebab_native_gmu_firmware;uint32_t expected=0;int e=95;
 if(!out)return 22;KebabGmuBootInit(out,KebabGmuBootLaunch);
 if(!KebabGmuFirmwareLaunchQualified || !worker_check || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled() || worker_check(cookie))return 95;
 e=kebab_native_gmu_firmware_borrowed_irq(borrowed_irq,&kebab_native_gmu_boot_access,0);if(e){out->result=e;return e;}
 if(!__atomic_compare_exchange_n(&kebab_native_gmu_firmware_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2){out->result=16;return 16;}if(borrowed_irq!=a->irq){out->result=95;return 95;}*out=kebab_native_gmu_firmware_result;out->evidence_replayed=1;return (int)out->result;}
 a->boot=&kebab_native_gmu_boot_access;
 if(kebab_native_gmu_boot_once!=2 || kebab_native_gmu_boot_result.result || kebab_native_gmu_boot_result.phase!=KGB_RESET_HELD_TCM){e=95;goto done;}
 a->boot->cookie=cookie;a->boot->bus_check=worker_check;
 a->irq=borrowed_irq;a->irq_generation=borrowed_irq->generation;
 a->boot->own_reads=a->boot->own_writes=0;
 /* Pure private epoch check before even allocating resource/IRQ mappings;
  * the canonical entry will repeat current ownership checks under busy. */
 e=kebab_gpu_first_ready_locked(a->boot->cold,a->boot->epoch);if(e)goto done;
 e=kebab_rpmh_gpu_gmu_firmware_irq_preflight(a->boot->cold,a->boot->epoch,a->irq);if(e)goto done;
 {
  KebabGpuLaunchResourceRecord resource_record;
  e=kebab_native_gpu_launch_resources_prepare(a->boot->cold,a->boot->epoch,&a->resources,&resource_record,a,kebab_native_gmu_firmware_resource_worker);if(e)goto done;
  const uint32_t pa[5]={0x00170000,0x00100000,0x00152000,0x03d90000,0x03d7c000},bytes[5]={0x4000,0x1000,0x1000,0x4000,0x4000};
  for(unsigned i=0;i<5;i++){e=kebab_native_gmu_boot_map(a->boot,i,pa[i],bytes[i]);if(e)goto done;}
  e=kebab_native_gmu_firmware_map(a,0,0x03d8e000,0x1000);if(e)goto done;
  e=kebab_native_gmu_firmware_map(a,1,0x03de0000,0x10000);if(e)goto done;
  KebabGmuBootIo io={a,kebab_native_gmu_firmware_read,kebab_native_gmu_firmware_write,kebab_native_gmu_firmware_barrier,
   kebab_native_gmu_firmware_delay,kebab_native_gmu_boot_now,NULL,NULL,NULL,NULL};
  e=kebab_rpmh_gpu_gmu_launch(a->boot->cold,a->boot->epoch,&a->resources,a->irq,&io,out);
 }
done:
 out->result=e;if(e && !out->primary_error)out->primary_error=e;
 out->reads=a->reads+a->boot->own_reads;out->writes=a->writes;
 if(a->boot)kebab_native_gmu_firmware_unmap(a);
 kebab_native_gmu_firmware_result=*out;__atomic_store_n(&kebab_native_gmu_firmware_once,2,__ATOMIC_RELEASE);return e;
}
#endif
