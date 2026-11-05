/* SPDX-License-Identifier: MIT */
#include "KebabSmmuAsidAudit.h"
struct KebabNativeAsidSnapshot {
 uint32_t magic,schema,bytes,flags,asid_result,reserved[3];
 KebabSmmuAsidObservation observation;uint32_t clock_test[24];
};
static_assert(sizeof(KebabNativeAsidSnapshot)==240,"fixed NS-ASID/clock-test readonly receipt");
static int kebab_native_asid_snapshot(uint64_t framebuffer,KebabNativeAsidSnapshot *out)
{
 if(!out)return 22;
 memset(out,0,sizeof *out);out->magic=0x3141534e;out->schema=1;out->bytes=sizeof *out;out->asid_result=2;
 KebabNativeDisplaySnapshot hardware={};KebabNativeDisplayMaps m={};
 KebabSmmuAsidWorkspace *workspace=NULL;
 if(kebab_native_display_snapshot(framebuffer,&hardware) || hardware.smmu_result)goto done;
 for(unsigned i=0;i<4;i++)if(hardware.smmu.stream[i].matches!=1 || !hardware.smmu.stream[i].context_read ||
    hardware.smmu.stream[i].shared_or_broad || (hardware.smmu.stream[i].s2cr&255)!=(hardware.smmu.stream[0].s2cr&255)){out->asid_result=95;goto done;}
 workspace=(KebabSmmuAsidWorkspace *)IOMalloc(sizeof *workspace);if(!workspace){out->asid_result=12;goto done;}
 if(!kebab_display_map(&m,0,0x100000,0x10000) || !kebab_display_map(&m,1,0xaf00000,0x4000) || !kebab_display_map(&m,3,0x15000000,0x100000))goto done;
 {
  KebabSmmuAsidIo io={&m,0x100000,kebab_smmu_native_read};
  out->asid_result=KebabSmmuAsidAudit(&io,hardware.smmu.stream[0].s2cr&255,workspace,&out->observation);
  if(!out->asid_result)out->flags|=1;
 }
done:
 for(unsigned n=4;n;n--){unsigned i=n-1;if(m.map[i])m.map[i]->release();if(m.memory[i])m.memory[i]->release();}
 if(workspace){memset(workspace,0,sizeof *workspace);IOFree(workspace,sizeof *workspace);}
 kebab_native_mdp_clock_receipt(out->clock_test);if(out->clock_test[1])out->flags|=2;
 return 0;
}
