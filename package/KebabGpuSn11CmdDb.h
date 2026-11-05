/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_SN11_CMD_DB_H
#define KEBAB_GPU_SN11_CMD_DB_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Fixed one-name CommandDB observation. RAM metadata only; the returned
 * RPMh command address is never a CPU MMIO pointer or a bandwidth lease. */
typedef struct {uint32_t matches,result,slave,address,version,priority[2],aux_bytes,aux[8];} KebabGpuSn11Resource;
typedef struct {uint32_t magic,schema,bytes,result,db_version,valid,absent,reserved;KebabGpuSn11Resource resource;} KebabGpuSn11Record;
#if defined(__cplusplus)
static_assert(sizeof(KebabGpuSn11Resource)==64 && sizeof(KebabGpuSn11Record)==96,"SN11 schema1");
#else
_Static_assert(sizeof(KebabGpuSn11Resource)==64 && sizeof(KebabGpuSn11Record)==96,"SN11 schema1");
#endif
static inline void KebabGpuSn11Init(KebabGpuSn11Record *r)
{memset(r,0,sizeof *r);r->magic=0x314e5347;r->schema=1;r->bytes=sizeof *r;r->result=22;}
static inline uint32_t KebabGpuSn11U32(const volatile uint8_t *p,size_t at)
{return (uint32_t)p[at]|(uint32_t)p[at+1]<<8|(uint32_t)p[at+2]<<16|(uint32_t)p[at+3]<<24;}
static inline int KebabGpuSn11Pass(const volatile uint8_t *p,size_t bytes,KebabGpuSn11Record *out)
{
 if(!out)return 22;KebabGpuSn11Init(out);if(!p || bytes!=0x20000)return 22;
 if(KebabGpuSn11U32(p,4)!=0x0c0330db)return (int)(out->result=2);
 out->db_version=KebabGpuSn11U32(p,0);
 const uint8_t name[8]={'S','N','1','1',0,0,0,0};
 for(unsigned i=0;i<8;i++){
  size_t h=8+16*i;uint32_t a=KebabGpuSn11U32(p,h),b=KebabGpuSn11U32(p,h+4);
  uint32_t slave=a&0xffff,headers=a>>16,data=b&0xffff,count=b>>16;
  if(count>1024 || 144+(size_t)headers+(size_t)count*24>bytes)return (int)(out->result=3);
  for(unsigned j=0;j<count;j++){
   size_t e=144+(size_t)headers+(size_t)j*24;unsigned match=1;
   for(unsigned k=0;k<8;k++)if(p[e+k]!=name[k])match=0;if(!match)continue;
   KebabGpuSn11Resource *v=&out->resource;v->matches++;v->slave=slave;
   v->address=KebabGpuSn11U32(p,e+16);v->version=KebabGpuSn11U32(p,h+8)&0xffff;
   v->priority[0]=KebabGpuSn11U32(p,e+8);v->priority[1]=KebabGpuSn11U32(p,e+12);
   uint32_t tail=KebabGpuSn11U32(p,e+20);v->aux_bytes=tail&0xffff;
   size_t aux=144+(size_t)data+(tail>>16);memset(v->aux,0,sizeof v->aux);v->result=3;
   if(v->aux_bytes>32 || aux>bytes || v->aux_bytes>bytes-aux)continue;
   /* Preserve bounded raw metadata even when the identity contract fails. */
   for(unsigned k=0;k<v->aux_bytes;k++)v->aux[k/4]|=(uint32_t)p[aux+k]<<((k%4)*8);
   v->result=4;
   if(slave!=5 || !v->address || ((v->address>>16)&7)!=5 || v->aux_bytes!=8 || v->version!=0x1000)continue;
   v->result=0;
  }
 }
 out->result=95;
 if(!out->resource.matches){out->absent=1;out->resource.result=2;}
 else if(out->resource.matches!=1)out->resource.result=5;
 else if(!out->resource.result){out->valid=1;out->result=0;}
 return (int)out->result;
}
static inline int KebabGpuSn11Finish(KebabGpuSn11Record *out,const KebabGpuSn11Record *again,int first,int second)
{
 if(!out || !again)return 22;
 if(first!=second || memcmp(out,again,sizeof *out)){out->result=6;out->valid=0;out->resource.result=6;return 6;}
 return first;
}
static inline int KebabGpuSn11Observe(const volatile uint8_t *p,size_t bytes,KebabGpuSn11Record *out)
{
 if(!out)return 22;KebabGpuSn11Record again;int first=KebabGpuSn11Pass(p,bytes,out);
 int second=KebabGpuSn11Pass(p,bytes,&again);return KebabGpuSn11Finish(out,&again,first,second);
}
#endif
