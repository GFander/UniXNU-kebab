/* SPDX-License-Identifier: MIT */
#include "KebabCmdDbArc.h"
struct KebabNativeDsiPowerSnapshot {
 uint32_t magic,schema,bytes,flags,dsi_result,arc_result,db_version,reserved;
 uint32_t clocks[8],dsi[24];KebabArcObservation arc[3];
};
static_assert(sizeof(KebabNativeDsiPowerSnapshot)==352,"fixed readonly DSI/ARC metadata");
static int kebab_native_dsi_power_snapshot(KebabNativeDsiPowerSnapshot *out)
{
 if(!out)return 22;
 memset(out,0,sizeof *out);out->magic=0x31575044;out->schema=1;out->bytes=sizeof *out;out->dsi_result=2;out->arc_result=2;
 KebabNativeDisplayMaps m={};
 if(kebab_display_map(&m,0,0x00100000,0x10000)) {
  out->clocks[0]=kebab_display_load(m.address[0],0xb00c);
  if((out->clocks[0]&1) && !(out->clocks[0]&0x80000000u) && kebab_display_map(&m,1,0x0af00000,0x4000)) {
   static const size_t off[]={0x3000,0x2080,0x2028,0x202c,0x2004,0x2038,0x200c};
   for(unsigned i=0;i<7;i++)out->clocks[i+1]=kebab_display_load(m.address[1],off[i]);
   out->dsi_result=3;
   if((out->clocks[1]&0x80000000u) && !(out->clocks[1]&1) && !(out->clocks[2]&0x80000000u)) {
    out->dsi_result=2;
    if(kebab_display_map(&m,2,0x0ae94000,0x400)) {
     /* Facts bound to the board's v2.4 vendor register definitions. No
      * FIFO ports, reset/trigger strobes or W1C writes are accessed. */
     static const size_t reg[]={0,0x1f4,4,8,0xc,0x3c,0x40,0x44,0x48,0x4c,0x50,0x84,0xa8,0xb4,0x10c,0x110,0x11c,0x120,0x15c,0x2a8,0x2ac,0x2b0,0x2b8,0x330};
     for(unsigned i=0;i<24;i++)out->dsi[i]=kebab_display_load(m.address[2],reg[i]);
     uint32_t gcc=kebab_display_load(m.address[0],0xb00c),gdsc=kebab_display_load(m.address[1],0x3000),ahb=kebab_display_load(m.address[1],0x2080);
     if(gcc!=out->clocks[0] || gdsc!=out->clocks[1] || ahb!=out->clocks[2])out->dsi_result=4;
     else {out->dsi_result=0;out->flags|=1;}
    }
   }
  } else out->dsi_result=3;
 }
 if(kebab_display_map(&m,3,0x80860000,0x20000)) {
  out->arc_result=KebabCmdDbArcObserve((const volatile uint8_t *)m.address[3],0x20000,&out->db_version,out->arc);
  if(!out->arc_result)out->flags|=2;
 }
 for(unsigned n=4;n;n--){unsigned i=n-1;if(m.map[i])m.map[i]->release();if(m.memory[i])m.memory[i]->release();}
 return 0;
}
