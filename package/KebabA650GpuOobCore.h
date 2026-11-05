/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_OOB_CORE_H
#define KEBAB_A650_GPU_OOB_CORE_H
#include <stdint.h>
#include <string.h>
/* A650 nonlegacy GPU OOB only. Never programs GX/CP/SMMU or a performance
 * vote. Space0 is GMU 03d6a000, space1 GPUCC03d90000. A separately reviewed
 * native canonical owner must supply live Root/VM/IRQ/HFI checks and pins.
 * The only writes are GPU request bit30 and ACK W1C bit31. Success retains
 * the OOB request; uncertain failure never clears it or releases any pins. */
enum {KHO_EMPTY=0,KHO_FAILED=1,KHO_HELD=2,KHO_QUARANTINED=3};
typedef struct {
 void *cookie;
 int (*gate)(void*);
 int (*read32)(void*,unsigned,uint32_t,uint32_t*);
 int (*write32)(void*,unsigned,uint32_t,uint32_t);
 void (*barrier)(void*),(*delay_us)(void*,unsigned);
 uint64_t (*now_us)(void*);
} KebabA650GpuOobIo;
typedef struct {uint32_t attempted,quarantined,held;} KebabA650GpuOobBook;
typedef struct {
 uint32_t magic,schema,bytes,result,phase,primary_error,io_error,touched;
 uint32_t request_written,ACK_observed,ACK_cleared,held,quarantined,reads,writes,barriers,polls;
 uint32_t failed_region,failed_offset,reserved;
 /* INFO, MASK, AO, SPTPRAC, GX_GDSCR, GX_CFG_GDSCR, CX_GDSCR, RPMH_STATE.
  * Snapshot arrays are raw observations, never a live lease or CP proof. */
 uint32_t before[8],after[8];
 uint64_t epoch,elapsed_us;
} KebabA650GpuOobRecord;
typedef struct {
 const KebabA650GpuOobIo *io;KebabA650GpuOobBook *book;
 KebabA650GpuOobRecord *r;uint64_t begin,ack_begin;uint32_t ack_active;
} KebabA650GpuOobContext;
static inline void KebabA650GpuOobInit(KebabA650GpuOobRecord*r)
{memset(r,0,sizeof *r);r->magic=0x314f5847;r->schema=1;r->bytes=sizeof *r;}
static inline int KHOClock(KebabA650GpuOobContext*c)
{
 uint64_t n=c->io->now_us(c->io->cookie);
 if(n<c->begin || n-c->begin>=100000 || (c->ack_active && (n<c->ack_begin || n-c->ack_begin>=10000)))return 110;
 return 0;
}
static inline int KHORawRead(KebabA650GpuOobContext*c,unsigned region,uint32_t off,uint32_t*v)
{
 c->r->reads++;int e=c->io->read32(c->io->cookie,region,off,v);
 if(e){c->r->io_error=(uint32_t)e;c->r->failed_region=region;c->r->failed_offset=off;return 5;}
 return KHOClock(c);
}
static inline int KHOGate(KebabA650GpuOobContext*c)
{
 int e=KHOClock(c);if(e)return e;e=c->io->gate(c->io->cookie);
 if(e){c->r->io_error=(uint32_t)e;return e;}
 if((e=KHOClock(c)))return e;
 uint32_t info,ao;if((e=KHORawRead(c,0,0x14648,&info)))return e;
 if((e=KHORawRead(c,0,0x24c14,&ao)))return e;
 if((ao&0x29) || (info&0x8000))return 5;
 /* No other OOB, pending queue, debug/error consumer or pre-existing ACK is
  * adopted. IRQ masks stay unchanged; high OOB bits remain IRQ-masked. */
 if((info&0x7fffffffu) || (!c->r->touched && (info&0x80000000u)))return 16;
 return KHOClock(c);
}
static inline int KHORead(KebabA650GpuOobContext*c,unsigned region,uint32_t off,uint32_t*v)
{int e=KHOGate(c);return e?e:KHORawRead(c,region,off,v);}
static inline int KHOWrite(KebabA650GpuOobContext*c,uint32_t off,uint32_t value)
{
 if(!((off==0x14650 && value==0x40000000u) || (off==0x14644 && value==0x80000000u)))return 22;
 int e=KHOGate(c);if(e)return e;c->r->touched=1;c->r->writes++;
 e=c->io->write32(c->io->cookie,0,off,value);
 /* This ordering is unconditional after a store attempt. A late gate or
  * fallible callback cannot silently suppress the architectural barrier. */
 c->r->barriers++;c->io->barrier(c->io->cookie);
 if(e){c->r->io_error=(uint32_t)e;c->r->failed_region=0;c->r->failed_offset=off;return 5;}
 if(off==0x14650)c->r->request_written=1;
 return KHOGate(c);
}
static inline int KHOSnapshot(KebabA650GpuOobContext*c,uint32_t*out)
{
 static const uint32_t off[8]={0x14648,0x1464c,0x24c14,0x14340,0x100c,0x1010,0x106c,0x143b0};
 for(unsigned n=0;n<8;n++){int e=KHORead(c,n>=4&&n<=6?1u:0u,off[n],out+n);if(e)return e;}
 return 0;
}
static inline int KebabA650GpuOobHold(KebabA650GpuOobContext*c,uint32_t qualified,uint64_t epoch)
{
 if(!c || !c->r)return 22;KebabA650GpuOobInit(c->r);KebabA650GpuOobRecord*r=c->r;
 if(!c->book || !c->io || !c->io->gate || !c->io->read32 || !c->io->write32 || !c->io->barrier || !c->io->delay_us || !c->io->now_us){r->result=22;r->phase=KHO_FAILED;return 22;}
 if(qualified!=1 || epoch!=1){r->result=95;r->phase=KHO_FAILED;return 95;}
 if(c->book->attempted || c->book->quarantined || c->book->held){r->result=16;r->phase=KHO_FAILED;return 16;}
 c->book->attempted=1;c->ack_active=0;r->epoch=epoch;c->begin=c->io->now_us(c->io->cookie);int e=KHOSnapshot(c,r->before);
 /* A first bounded activation must not adopt an already-powered GX. The
  * private isolated VM is published, and fresh independent indicators must
  * still report GX power/clock off before the sole request is attempted. */
 if(!e && (!(r->before[1]&0x80000000u) || !(r->before[6]&0x80000000u) ||
            (r->before[3]&0xc0u)!=0xc0u || !(r->before[5]&0x8000u)))e=13;
 if(!e){c->ack_begin=c->io->now_us(c->io->cookie);c->ack_active=1;e=KHOWrite(c,0x14650,0x40000000u);}
 if(!e){
  uint32_t info=0;for(unsigned n=0;n<101;n++){
   r->polls++;e=KHORead(c,0,0x14648,&info);if(e)break;
   if(info&0x80000000u){r->ACK_observed=1;break;}
   c->io->delay_us(c->io->cookie,100);e=KHOClock(c);if(e)break;
  }
  if(!e&&!r->ACK_observed)e=110;
 }
 if(!e){e=KHOWrite(c,0x14644,0x80000000u);if(!e){uint32_t info=0;e=KHORead(c,0,0x14648,&info);if(!e&&(info&0x80000000u))e=5;if(!e)r->ACK_cleared=1;}}
 c->ack_active=0;
 if(!e)e=KHOSnapshot(c,r->after);
 /* GX_CFG_GDSCR bit16 is positive power-up completion. Do not invent an
  * inverse contract for its separate bit15 power-down diagnostic. SPTPRAC
  * bits6/7 must both report on while the retained GPU OOB is held. */
 if(!e && (r->after[0] || r->after[1]!=r->before[1] || (r->after[3]&0xc0u) || !(r->after[5]&0x10000u) || !(r->after[6]&0x80000000u)))e=11;
 if(!e)e=KHOGate(c);
 uint64_t end=c->io->now_us(c->io->cookie);r->elapsed_us=end>=c->begin?end-c->begin:UINT64_MAX;
 if(!e && r->elapsed_us>=100000)e=110;
 if(e){r->result=r->primary_error=(uint32_t)e;r->phase=r->touched?KHO_QUARANTINED:KHO_FAILED;
  if(r->touched){r->quarantined=1;c->book->quarantined=1;}return e;}
 c->book->held=1;r->held=1;r->phase=KHO_HELD;return 0;
}
#endif
