/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_CMD_DB_H
#define KEBAB_GPU_CMD_DB_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Board firmware Command DB metadata only. Address fields name RPMh
 * commands, never CPU MMIO. No voltage/frequency/vote is issued here.
 * Header layout is144 bytes, eight16-byte slave headers,24-byte entries.
 * ARC=3, BCM=5. Names/limits are fixed for SM8250 GPU bring-up. */
typedef struct {
 uint32_t matches,result,slave,address,version,priority[2],aux_bytes,aux[8];
} KebabGpuCmdDbResource;
typedef struct {
 uint32_t magic,schema,bytes,result,db_version,valid,absent,reserved;
 KebabGpuCmdDbResource resource[7];
} KebabGpuCmdDbRecord;
static_assert(sizeof(KebabGpuCmdDbRecord)==480,"GPU command DB record schema1");
static uint32_t kebab_gpu_db_u32(const volatile uint8_t *p,size_t at)
{return(uint32_t)p[at]|((uint32_t)p[at+1]<<8)|((uint32_t)p[at+2]<<16)|((uint32_t)p[at+3]<<24);}
static int kebab_gpu_cmd_db_pass(const volatile uint8_t *p,size_t bytes,KebabGpuCmdDbRecord *out)
{
 memset(out,0,sizeof *out);out->magic=0x31424447;out->schema=1;out->bytes=sizeof *out;out->result=22;
 if(!p || bytes!=0x20000)return 22;
 if(kebab_gpu_db_u32(p,4)!=0x0c0330db){out->result=2;return 2;}
 out->db_version=kebab_gpu_db_u32(p,0);
 static const uint8_t names[7][8]={{'g','f','x','.','l','v','l',0},{'c','x','.','l','v','l',0,0},{'m','x','.','l','v','l',0,0},{'M','C','0',0,0,0,0,0},{'S','H','0',0,0,0,0,0},{'A','C','V',0,0,0,0,0},{'C','N','0',0,0,0,0,0}};
 for(unsigned i=0;i<8;i++) {
  size_t h=8+16*i;uint32_t a=kebab_gpu_db_u32(p,h),b=kebab_gpu_db_u32(p,h+4);
  uint32_t slave=a&0xffff,headers=a>>16,data=b&0xffff,count=b>>16;
  if(count>1024 || 144+headers+(size_t)count*24>bytes){out->result=3;return 3;}
  for(unsigned j=0;j<count;j++) {
   size_t e=144+headers+(size_t)j*24;
   for(unsigned n=0;n<7;n++) {
    unsigned match=1;for(unsigned k=0;k<8;k++)if(p[e+k]!=names[n][k])match=0;
    if(!match)continue;
    KebabGpuCmdDbResource *v=&out->resource[n];v->matches++;
    v->slave=slave;v->address=kebab_gpu_db_u32(p,e+16);v->version=kebab_gpu_db_u32(p,h+8)&0xffff;
    v->priority[0]=kebab_gpu_db_u32(p,e+8);v->priority[1]=kebab_gpu_db_u32(p,e+12);
    uint32_t tail=kebab_gpu_db_u32(p,e+20);v->aux_bytes=tail&0xffff;
    size_t aux=144+(size_t)data+(tail>>16);v->result=3;
    if(v->aux_bytes>32 || aux>bytes || v->aux_bytes>bytes-aux)continue;
    v->result=4;
    if(slave!=(n<3?3u:5u) || !v->address || ((v->address>>16)&7)!=slave)continue;
    /* ARC tables are16-bit level encodings. BCM aux is retained raw until
     * the matching BCM layout/version is independently decoded. */
    if(n<3 && (!v->aux_bytes || (v->aux_bytes&1)))continue;
    for(unsigned k=0;k<v->aux_bytes;k++)v->aux[k/4]|=(uint32_t)p[aux+k]<<((k%4)*8);
    v->result=0;
   }
  }
 }
 out->result=0;
 for(unsigned n=0;n<7;n++) {
  KebabGpuCmdDbResource *v=&out->resource[n];
  if(!v->matches){out->absent|=1u<<n;v->result=2;out->result=95;}
  else if(v->matches!=1){v->result=5;out->result=95;}
  else if(v->result)out->result=95;
  else out->valid|=1u<<n;
 }
 return (int)out->result;
}
static int KebabGpuCmdDbObserve(const volatile uint8_t *p,size_t bytes,KebabGpuCmdDbRecord *out)
{
 if(!out)return 22;
 KebabGpuCmdDbRecord again={};
 int result=kebab_gpu_cmd_db_pass(p,bytes,out);
 int second=kebab_gpu_cmd_db_pass(p,bytes,&again);
 if(result!=second || memcmp(out,&again,sizeof again)) {out->result=6;out->valid=0;return 6;}
 return result;
}
#endif
