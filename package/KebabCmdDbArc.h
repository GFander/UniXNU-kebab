/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_CMD_DB_ARC_H
#define KEBAB_CMD_DB_ARC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
 uint32_t matches,slave,address,bytes,aux_offset,version,priority[2],aux[8];
} KebabArcObservation;
static uint32_t kebab_cmd_db_u32(const volatile uint8_t *p,size_t n)
{return(uint32_t)p[n]|((uint32_t)p[n+1]<<8)|((uint32_t)p[n+2]<<16)|((uint32_t)p[n+3]<<24);}
/* Fixed firmware command DB, no RSC or resource writes. Only cx.lvl/mx.lvl/
 * mmcx.lvl ARC metadata is retained. Resource addresses are RPMh commands,
 * not CPU physical addresses or permission to cast them to MMIO pointers. */
static int KebabCmdDbArcObserve(const volatile uint8_t *p,size_t bytes,uint32_t *version,KebabArcObservation out[3])
{
 if(out)memset(out,0,3*sizeof *out);if(version)*version=0;
 if(!p || !version || !out || bytes!=0x20000)return 1;
 *version=kebab_cmd_db_u32(p,0);if(kebab_cmd_db_u32(p,4)!=0x0c0330db)return 2;
 static const uint8_t names[3][8]={{'c','x','.','l','v','l',0,0},{'m','x','.','l','v','l',0,0},{'m','m','c','x','.','l','v','l'}};
 for(unsigned i=0;i<8;i++) {
  size_t h=8+16*i;uint32_t a=kebab_cmd_db_u32(p,h),b=kebab_cmd_db_u32(p,h+4);
  uint32_t slave=a&0xffff,headers=a>>16,data=b&0xffff,count=b>>16;
  if(count>1024 || 144+headers+(size_t)count*24>bytes)return 3;
  for(unsigned j=0;j<count;j++) {
   size_t e=144+headers+24*j;
   for(unsigned n=0;n<3;n++) {
    unsigned match=1;for(unsigned k=0;k<8;k++)if(p[e+k]!=names[n][k])match=0;
    if(!match)continue;
    KebabArcObservation *v=&out[n];v->matches++;v->slave=slave;v->address=kebab_cmd_db_u32(p,e+16);
    uint32_t tail=kebab_cmd_db_u32(p,e+20);v->bytes=tail&0xffff;v->aux_offset=144+data+(tail>>16);v->version=kebab_cmd_db_u32(p,h+8)&0xffff;
    v->priority[0]=kebab_cmd_db_u32(p,e+8);v->priority[1]=kebab_cmd_db_u32(p,e+12);
    if(slave!=3 || ((v->address>>16)&7)!=3)return 4;
    if(!v->bytes || v->bytes>32 || (v->bytes&1) || v->aux_offset>bytes || v->bytes>bytes-v->aux_offset)return 3;
    for(unsigned k=0;k<v->bytes;k++)v->aux[k/4]|=(uint32_t)p[v->aux_offset+k]<<((k%4)*8);
   }
  }
 }
 if(kebab_cmd_db_u32(p,0)!=*version || kebab_cmd_db_u32(p,4)!=0x0c0330db)return 6;
 for(unsigned n=0;n<3;n++)if(out[n].matches!=1)return 5;
 return 0;
}
#endif
