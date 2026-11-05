/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_BACKING_H
#define KEBAB_A650_GMU_BACKING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabA650GmuBackingTypes.h"
/* Caller supplies SHA256_CTX + SHA256_Init/Update/Final (native XNU:
 * <libkern/crypto/sha2.h>). This is CPU-only preparation of exclusively
 * owned, non-live extents. It does NOT authorize SMMU publication or MMIO.
 * Original OTA firmware is SHA-bound, not authenticated through PAS13. */
enum {KebabA650GmuImageBytes=41548,KebabA650GmuMaxBlocks=64,KebabA650GmuRamExtents=6};
typedef struct {uint32_t image_offset,address,bytes,region,type,value;} KebabA650GmuBlock;
typedef struct {uint32_t blocks,payloads,ram_bytes,tcm_bytes,core_version,hfi_version;KebabA650GmuBlock block[64];} KebabA650GmuPlan;
typedef struct {uint32_t iova,bytes;void *cpu;} KebabA650GmuExtent;
static inline uint32_t KebabA650GmuU32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline void KebabA650GmuPut32(uint8_t *p,uint32_t x)
{p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
static inline void KebabA650GmuDigest(const void *p,size_t n,uint8_t out[32])
{SHA256_CTX c;SHA256_Init(&c);SHA256_Update(&c,p,n);SHA256_Final(out,&c);}
static inline int KebabA650GmuRange(uint32_t region,uint32_t *base,uint32_t *bytes)
{
 static const uint32_t b[8]={0x4000,0x144000,0x157000,0x60000000,0x60001000,0x60006000,0,0x10004000};
 static const uint32_t n[8]={0x8000,0x13000,0x9000,0x1000,0x5000,0x1000,0x4000,0x4000};
 if(region>=8 || !base || !bytes)return 22;*base=b[region];*bytes=n[region];return 0;
}
static inline int KebabA650GmuLocate(uint32_t address,uint32_t bytes,uint32_t *region)
{
 uint32_t i,b,n;if(!bytes || !region || (uint64_t)address+bytes>UINT64_C(0x100000000))return 22;
 for(i=0;i<8;i++){(void)KebabA650GmuRange(i,&b,&n);if(address>=b && (uint64_t)address+bytes<=(uint64_t)b+n){*region=i;return 0;}}return 22;
}
/* Format parser is independently bounded; trust is established only by
 * KebabA650GmuBoundPlan below. Metadata is not treated as file payload. */
static inline int KebabA650GmuParse(const uint8_t *image,size_t bytes,KebabA650GmuPlan *out)
{
 size_t at=0;uint32_t i,j;if(!image || !out || !bytes || bytes>2u*1024u*1024u)return 22;memset(out,0,sizeof *out);
 while(at<bytes){
  KebabA650GmuBlock *p;uint32_t region,n;if(bytes-at<16 || out->blocks==64)return 22;p=&out->block[out->blocks];
  p->address=KebabA650GmuU32(image+at);p->bytes=KebabA650GmuU32(image+at+4);p->type=KebabA650GmuU32(image+at+8);p->value=KebabA650GmuU32(image+at+12);p->image_offset=(uint32_t)at+16;
  if((p->address&3) || (p->bytes&3) || p->bytes>bytes-at-16)return 22;
  n=p->bytes?p->bytes:p->value;
  if(p->bytes || p->type==1 || p->type==7){if(KebabA650GmuLocate(p->address,n,&region))return 22;p->region=region;}
  else p->region=UINT32_MAX;
  if(p->bytes){if(region==4 || region==5)return 22; /* runtime HFI/log never firmware payload targets */
   ++out->payloads;if(region<6)out->ram_bytes+=p->bytes;else out->tcm_bytes+=p->bytes;
   for(i=0;i<out->blocks;i++){KebabA650GmuBlock *q=&out->block[i];if(q->bytes && p->address<(uint64_t)q->address+q->bytes && q->address<(uint64_t)p->address+p->bytes)return 22;}
  }
  ++out->blocks;at+=16+p->bytes;
 }
 for(i=0;i<out->blocks;i++){
  const KebabA650GmuBlock *p=&out->block[i];const uint32_t addr[2]={0x10007fe0,0x10007ff0};uint32_t *version[2]={&out->core_version,&out->hfi_version};
  for(j=0;j<2;j++)if(p->bytes && p->address<=addr[j] && (uint64_t)addr[j]+4<=(uint64_t)p->address+p->bytes)*version[j]=KebabA650GmuU32(image+p->image_offset+addr[j]-p->address);
 }
 return 0;
}
static inline int KebabA650GmuBoundPlan(const void *image,size_t bytes,KebabA650GmuPlan *out,uint8_t digest[32])
{
 static const uint8_t expected[32]={0xdf,0x79,0xfa,0x84,0x82,0xfc,0xc7,0x58,0x7b,0xb2,0x39,0xb9,0xc9,0xe2,0xb2,0x4b,0xce,0x47,0x29,0x89,0x3b,0x99,0x0f,0xe3,0xf8,0x2b,0x30,0xda,0x81,0x75,0xb0,0x1c};
 uint8_t hash[32];int e;if(!image || !out || bytes!=41548)return 22;KebabA650GmuDigest(image,bytes,hash);if(digest)memcpy(digest,hash,32);if(memcmp(hash,expected,32))return 80;
 e=KebabA650GmuParse((const uint8_t *)image,bytes,out);if(e)return e;
 return out->core_version==0x20010008 && out->hfi_version==0x20000001?0:80;
}
static inline int KebabA650GmuOverlap(uintptr_t a,size_t n,uintptr_t b,size_t m)
{if(n>UINTPTR_MAX-a || m>UINTPTR_MAX-b)return 1;return a<b+m && b<a+n;}
static inline int KebabA650GmuStage(const void *image,size_t bytes,const KebabA650GmuExtent *extent,size_t count,KebabA650GmuBackingRecord *out)
{
 KebabA650GmuPlan plan;uint32_t i,j,b,n;uint8_t repeat[32];int e;
 if(!out)return 22;memset(out,0,sizeof *out);out->result=22;
 e=KebabA650GmuBoundPlan(image,bytes,&plan,out->sha256);if(e){out->result=(uint32_t)e;return e;}out->image_verified=1;
 if(!extent || count!=6)return 22;
 /* Validate the entire destination set before the first memset/copy. This
  * neither allocates nor changes mappings; the producer must own/pin them. */
 for(i=0;i<6;i++){
  (void)KebabA650GmuRange(i,&b,&n);
  if(extent[i].iova!=b || extent[i].bytes!=n || !extent[i].cpu || ((uintptr_t)extent[i].cpu&15) || KebabA650GmuOverlap((uintptr_t)extent[i].cpu,n,(uintptr_t)image,bytes))return 22;
  for(j=0;j<i;j++)if(KebabA650GmuOverlap((uintptr_t)extent[i].cpu,n,(uintptr_t)extent[j].cpu,extent[j].bytes))return 22;
 }
 for(i=0;i<6;i++)memset(extent[i].cpu,0,extent[i].bytes);
 for(i=0;i<plan.blocks;i++){
  const KebabA650GmuBlock *p=&plan.block[i];if(!p->bytes || p->region>=6)continue;
  memcpy((uint8_t *)extent[p->region].cpu+p->address-extent[p->region].iova,(const uint8_t *)image+p->image_offset,p->bytes);
 }
 /* Actual OTA HFI2 layout: table216B, queue start224B, four4KiB slots,
  * CMD/MSG/DBG enabled, DSP disabled. num_active_q stays source value4. */
 {
  uint8_t *h=(uint8_t *)extent[4].cpu;const uint32_t header[6]={0,54,6,12,4,4},priority[4]={10,10,40,20};
  for(i=0;i<6;i++)KebabA650GmuPut32(h+4*i,header[i]);
  for(i=0;i<4;i++){
   uint8_t *q=h+24+48*i;KebabA650GmuPut32(q,i<3?1:0);KebabA650GmuPut32(q+4,extent[4].iova+224+4096*i);
   KebabA650GmuPut32(q+8,i|(priority[i]<<8));KebabA650GmuPut32(q+12,1024);
  }
 }
 KebabA650GmuDigest(image,bytes,repeat);if(memcmp(repeat,out->sha256,32)){out->result=80;return 80;}
 out->ram_staged=out->hfi_initialized=1;out->extent_count=6;out->mapped_pages=43;out->ram_bytes=plan.ram_bytes;out->tcm_bytes_pending=plan.tcm_bytes;out->result=0;return 0;
}
#endif
