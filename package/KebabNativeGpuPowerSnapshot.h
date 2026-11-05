/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_POWER_SNAPSHOT_H
#define KEBAB_NATIVE_GPU_POWER_SNAPSHOT_H
#include "KebabNativeGpuReadOnly.h"
#include "KebabGpuCmdDb.h"
struct KebabNativeGpuPowerSnapshot {
 uint32_t magic,schema,bytes,flags,clock_result,db_result,reserved[2];
 KebabGpuRoRecord clocks;KebabGpuCmdDbRecord command_db;
};
static_assert(sizeof(KebabNativeGpuPowerSnapshot)==756,"fixed GPU prerequisites snapshot schema1");
/* This port's GCC control bus stays in the CPU always-on domain. The only
 * caller is the normal USB snapshot worker, serialized against reset and
 * other diagnostic commands; no GPU power/clock writer exists yet. Existing
 * boot/UFS/display use the same controller. No GPUCC/rail qualification is
 * granted here, and no downstream GPU register can be read by this adapter. */
static int kebab_gpu_gcc_worker_access(void *)
{return panic_active()?95:0;}
static int kebab_native_gpu_power_snapshot(KebabNativeGpuPowerSnapshot *out)
{
 if(!out)return 22;
 memset(out,0,sizeof *out);out->magic=0x3150474b;out->schema=1;out->bytes=sizeof *out;out->db_result=12;
 if(panic_active()){out->clock_result=95;out->db_result=95;return 0;}
 out->clock_result=kebab_native_gpu_ro_snapshot(NULL,kebab_gpu_gcc_worker_access,&out->clocks);
 if(out->clock_result==KebabGpuRoGccOnly && out->clocks.stable==31)out->flags|=1;
 /* Same immutable board Command DB aperture already qualified by source11.
  * Read exact GPU resource metadata twice; addresses are never dereferenced
  * as MMIO and no RSC/rail/clock vote is generated. */
 KebabNativeDisplayMaps maps={};
 if(kebab_display_map(&maps,0,0x80860000,0x20000)) {
  out->db_result=KebabGpuCmdDbObserve((const volatile uint8_t *)maps.address[0],0x20000,&out->command_db);
  if(!out->db_result && out->command_db.valid==127)out->flags|=2;
 }
 if(maps.map[0])maps.map[0]->release();
 if(maps.memory[0])maps.memory[0]->release();
 return 0;
}
#endif
