/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GMU_HFI_INIT_H
#define KEBAB_NATIVE_GMU_HFI_INIT_H
#include "KebabNativeGmuHfi.h"
#include "KebabGpuCmdDb.h"
#include "KebabGpuSn11CmdDb.h"
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGmuHfiInitOwner.h>
#include <kern/KebabGmuHfiInitBinding.h>
#else
#include "KebabGmuHfiInitOwner.h"
#include "KebabGmuHfiInitBinding.h"
#endif
struct KebabNativeGmuHfiInitAccess {
 KebabNativeGmuHfiAccess channel;
 IOMemoryDescriptor *db_memory;IOMemoryMap *db_mapping;const volatile uint8_t *db;
};
static KebabNativeGmuHfiInitAccess kebab_native_gmu_hfi_init;
static uint32_t kebab_native_gmu_hfi_init_once;
static KebabGmuHfiInitRecord kebab_native_gmu_hfi_init_result;
/* The first member is the exact old channel callback cookie. No pointer to
 * caller metadata, CAP bytes, temporary descriptor or copied IRQ is kept. */
static_assert(offsetof(KebabNativeGmuHfiInitAccess,channel)==0,"channel cookie first");
static_assert(sizeof(KebabGmuHfiInitResource)==sizeof(KebabGpuCmdDbResource),"metadata layout");
static_assert(sizeof(KebabGmuHfiInitResource)==sizeof(KebabGpuSn11Resource),"SN11 metadata layout");
static int kebab_native_gmu_hfi_init_metadata(void *cookie,KebabGmuHfiInitMetadata *out)
{
 auto *a=(KebabNativeGmuHfiInitAccess *)cookie;KebabGpuCmdDbRecord first,second;KebabGpuSn11Record sn,again;
 if(!a || !out || !a->db)return 95;memset(out,0,sizeof *out);
 int e=kebab_native_gmu_hfi_gate(&a->channel);if(e)return e;
 int f=kebab_gpu_cmd_db_pass(a->db,0x20000,&first),s=KebabGpuSn11Pass(a->db,0x20000,&sn);
 if((e=kebab_native_gmu_hfi_gate(&a->channel)))return e;
 int ff=kebab_gpu_cmd_db_pass(a->db,0x20000,&second),ss=KebabGpuSn11Pass(a->db,0x20000,&again);
 if((e=kebab_native_gmu_hfi_gate(&a->channel)))return e;
 if(f!=ff || s!=ss || memcmp(&first,&second,sizeof first) || memcmp(&sn,&again,sizeof sn))return 6;
 if(f || s || first.db_version!=sn.db_version)return 95;
 out->db_version=first.db_version;
 for(unsigned n=0;n<6;n++)memcpy(&out->resource[n],&first.resource[n],sizeof out->resource[n]);
 memcpy(&out->resource[6],&sn.resource,sizeof out->resource[6]);return KebabGmuHfiInitMetadataCheck(out);
}
static void kebab_native_gmu_hfi_init_unmap(KebabNativeGmuHfiInitAccess *a)
{
 a->db=NULL;if(a->db_mapping){a->db_mapping->release();a->db_mapping=NULL;}
 if(a->db_memory){a->db_memory->release();a->db_memory=NULL;}
 kebab_native_gmu_hfi_unmap(&a->channel);
}
/* New normal-worker entry; no automatic VERSION/IRQ/resource acquisition.
 * Old Version result/cache and private NativeHfiAccess remain byte intact. */
static int kebab_native_gmu_hfi_initialize(void *cookie,int(*worker_check)(void *),KebabGmuHfiInitRecord *out)
{
 auto *a=&kebab_native_gmu_hfi_init;auto *f=&kebab_native_gmu_firmware;KebabNativeGmuBootAccess *b=NULL;
 void *saved_cookie=NULL;int(*saved_check)(void *)=NULL;bool changed_worker=false;uint32_t expected=0;int e=95;
 if(!out)return 22;KebabGmuHfiInitRecordInit(out);
 if(!KebabGmuHfiInitQualified || !worker_check || ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled() || worker_check(cookie))return 95;
 if(kebab_native_gmu_firmware_once!=2 || kebab_native_gmu_firmware_result.result || kebab_native_gmu_firmware_result.phase!=KGB_HFI_QUEUES_READY ||
    kebab_native_gmu_hfi_once!=2 || kebab_native_gmu_hfi_result.result || !kebab_native_gmu_hfi_result.version_verified || kebab_native_gmu_hfi_result.returned_version!=0x20010008 ||
    !f->boot || !f->irq || kebab_gpu_gmu_irq_healthy(f->irq))return 95;
 b=f->boot;e=kebab_gpu_first_ready_locked(b->cold,b->epoch);if(e){out->io.result=e;return e;}
 if(!__atomic_compare_exchange_n(&kebab_native_gmu_hfi_init_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected!=2){out->io.result=16;return 16;}*out=kebab_native_gmu_hfi_init_result;out->replayed=1;return (int)out->io.result;}
 /* VERSION already unmapped all six Boot slots. Refuse foreign mappings;
  * never overwrite/release a slot which this invocation did not acquire. */
 for(unsigned n=0;n<6;n++)if(b->memory[n] || b->mapping[n] || b->address[n]){e=16;goto done;}
 a->channel.firmware=f;saved_cookie=b->cookie;saved_check=b->bus_check;b->cookie=cookie;b->bus_check=worker_check;changed_worker=true;
 e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
 {
  const uint32_t pa[4]={0x00170000,0x00100000,0x00152000,0x03d90000},bytes[4]={0x4000,0x1000,0x1000,0x4000};
  for(unsigned n=0;n<4;n++){e=kebab_native_gmu_boot_map(b,n,pa[n],bytes[n]);if(e)goto cleanup;e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;}
  const uint32_t pa2[2]={0x03d7c000,0x03d8e000},size2[2]={0x4000,0x1000};
  for(unsigned n=0;n<2;n++){
   e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
   a->channel.memory[n]=IOMemoryDescriptor::withPhysicalAddress(pa2[n],size2[n],n?kIODirectionIn:kIODirectionInOut);
   if(!a->channel.memory[n]){e=12;goto cleanup;}e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
   a->channel.mapping[n]=a->channel.memory[n]->map(kIOMapAnywhere|kIOMapInhibitCache|(n?kIOMapReadOnly:0));
   if(!a->channel.mapping[n] || a->channel.mapping[n]->getLength()!=size2[n] || !(a->channel.address[n]=a->channel.mapping[n]->getVirtualAddress())){e=12;goto cleanup;}
   e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
  }
  a->db_memory=IOMemoryDescriptor::withPhysicalAddress(0x80860000,0x20000,kIODirectionIn);
  if(!a->db_memory){e=12;goto cleanup;}e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
  a->db_mapping=a->db_memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
  if(!a->db_mapping || a->db_mapping->getLength()!=0x20000 || !(a->db=(const volatile uint8_t *)a->db_mapping->getVirtualAddress())){e=12;goto cleanup;}
  e=kebab_native_gmu_boot_worker(b);if(e)goto cleanup;
  KebabGmuBootIo clocks={b,kebab_native_gmu_boot_read,kebab_native_gmu_boot_write,kebab_native_gmu_boot_barrier,kebab_native_gmu_boot_delay,kebab_native_gmu_boot_now,NULL,NULL,NULL,NULL};
  KebabGmuHfiInitIo io={{&a->channel,kebab_native_gmu_hfi_gate,kebab_native_gmu_hfi_read_shared,kebab_native_gmu_hfi_write_shared,kebab_native_gmu_hfi_read_reg,kebab_native_gmu_hfi_write_reg,kebab_native_gmu_hfi_barrier,kebab_native_gmu_hfi_delay,kebab_native_gmu_boot_now},kebab_native_gmu_hfi_init_metadata};
  e=kebab_rpmh_gpu_gmu_hfi_initialize(b->cold,b->epoch,&f->resources,f->irq,&clocks,&io,out);
 }
cleanup:
 kebab_native_gmu_hfi_init_unmap(a);
 if(changed_worker){b->cookie=saved_cookie;b->bus_check=saved_check;}
done:
 a->channel.firmware=NULL;out->io.result=e;if(e && !out->io.io_error)out->io.io_error=e;
 kebab_native_gmu_hfi_init_result=*out;__atomic_store_n(&kebab_native_gmu_hfi_init_once,2,__ATOMIC_RELEASE);return e;
}
#endif
