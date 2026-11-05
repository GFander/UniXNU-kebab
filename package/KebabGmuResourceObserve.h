/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_RESOURCE_OBSERVE_H
#define KEBAB_GMU_RESOURCE_OBSERVE_H
#include <stdint.h>
#include <string.h>
/* Status observations only. Neither idle TCS, request registers, nor a GDSCR
 * sample proves an ARC/BCM target, an executed command, or sustained clocks. */
enum {KGRO_RSCC=0,KGRO_DCVS=1,KGRO_HFI=2,KGRO_CC=3,KGRO_AO=4,KGRO_WORDS=14};
typedef struct {
 uint32_t magic,schema,bytes,result,reads,passes,failed_region,failed_offset;
 uint32_t raw[2][KGRO_WORDS];
 uint64_t elapsed_us;
} KebabGmuResourceObserveRecord;
typedef struct {
 void *cookie;int(*gate)(void *);int(*read32)(void *,uint32_t,uint32_t,uint32_t *);
 uint64_t(*now_us)(void *);
} KebabGmuResourceObserveIo;
static inline void KebabGmuResourceObserveInit(KebabGmuResourceObserveRecord *r)
{memset(r,0,sizeof *r);r->magic=0x31565247;r->schema=1;r->bytes=sizeof *r;r->result=95;r->failed_region=r->failed_offset=~0u;}
static inline int KebabGmuResourceObserve(KebabGmuResourceObserveRecord *r,const KebabGmuResourceObserveIo *io)
{
 static const uint32_t region[KGRO_WORDS]={0,0,0,0,1,1,1,2,3,3,3,3,2,4};
 static const uint32_t offset[KGRO_WORDS]={0xd18,0xfb8,0x1258,0x14f8,0xff4,0xff8,0xffc,0x23b0,0x100c,0x1010,0x106c,0x1508,0x2648,0xc14};
 uint64_t begin,now;int e=22;
 if(!r)return 22;KebabGmuResourceObserveInit(r);
 if(!io || !io->gate || !io->read32 || !io->now_us){r->result=22;return 22;}
 begin=io->now_us(io->cookie);
 for(unsigned pass=0;pass<2;pass++){
  for(unsigned n=0;n<KGRO_WORDS;n++){
   e=io->gate(io->cookie);if(e)goto done;
   now=io->now_us(io->cookie);if(now<begin || now-begin>=100000){e=110;goto done;}
   r->failed_region=region[n];r->failed_offset=offset[n];
   e=io->read32(io->cookie,region[n],offset[n],&r->raw[pass][n]);if(e)goto done;r->reads++;
   e=io->gate(io->cookie);if(e)goto done;
  }
  /* Same defined fault masks as the qualified OTA HFI channel. Never W1C,
   * drain a message, or interpret a default zero as a successful vote. */
  if((r->raw[pass][12]&0x8006u) || (r->raw[pass][13]&0x29u)){e=5;goto done;}
  r->passes++;
 }
 e=0;
done:
 now=io->now_us(io->cookie);r->elapsed_us=now>=begin?now-begin:0;
 if(!e && (now<begin || now-begin>=100000))e=110;
 if(!e){r->failed_region=r->failed_offset=~0u;}
 r->result=(uint32_t)e;return e;
}
#endif
