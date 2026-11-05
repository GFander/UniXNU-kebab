/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_LAUNCH_RESOURCE_CORE_H
#define KEBAB_GPU_LAUNCH_RESOURCE_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* A650 named rscc03de0000/10000 and PDCcfg0b290000/10000 only.
 * Facts from fixed7c2b vendor + v6.12. No legacy GMU+23000 fallback,
 * PDC-seq/TCS programming, CM3 release, IRQ, QMP/ACD or PAS operation.
 * Run under the ONE canonical RPMh busy lock with a private reset-held gate.
 * Transport callbacks below are native kernel peers, never user/CAP inputs. */
enum {KLR_RSC=0,KLR_PDC=1,KLR_GMU=2,KLR_CC=3,KLR_GCC=4,KLR_NEED_RSC=1,KLR_NEED_BW=2};
enum {KLR_BW_RETAIN_INHERITED_NO_WRITE=1};
enum {KLR_EMPTY=0,KLR_PREPARING=1,KLR_RSC_SLEEPING=2,KLR_READY=3,KLR_QUARANTINED=4};
typedef struct {
 uint32_t magic,schema,bytes,result,state,attempts,ready_mask,quarantined;
 uint32_t reads,writes,barriers,delays,polls,failed_region,failed_offset,failed_operation;
 int32_t io_error;uint32_t last_read,last_write,reset_readback,req,ack,busy,status0;
 uint32_t pdc_enable,pdc_start,sequence_verified,initial_bw_level,ddr_ib_kbps,cnoc_ib_kbps;
 uint32_t bw_commands,bw_acked,bw_uncertain,firmware_started,async_hfi_ready,reserved;
 uint32_t bandwidth_mode,bandwidth_qualified;int32_t bootstrap_vote_result;
 uint64_t elapsed_us;
} KebabGpuLaunchResourceRecord;
typedef struct {uint32_t attempted,ready_mask,quarantined,touched;} KebabGpuLaunchResourceLedger;
typedef struct {
 void *cookie;
 int (*gate_reset_held_locked)(void *);
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,unsigned);
 uint64_t (*now_us)(void *);
} KebabGpuLaunchResourceIo;
typedef struct {
 const KebabGpuLaunchResourceIo *io;KebabGpuLaunchResourceLedger *ledger;
 KebabGpuLaunchResourceRecord *r;uint64_t begin;
} KebabGpuLaunchResourceContext;
typedef struct {uint32_t offset,value;} KLRSetting;
static const KLRSetting KLRRscSetup[]={
 {0x10,0x01000000},{0x2c,1},{0x38,0},{0x34,0},{0x40,0},{0x3c,0},
 {0x48,0x80000000},{0x44,0},{0x400,0},{0x20,0x4520},{0x24,0x4510},{0x28,0x4514},
 {0x600,0xeaaae5a0},{0x604,0xe1a1ebab},{0x608,0xa2e0a581},{0x60c,0xecac82e2},{0x610,0x0020edad}
};
/* The vendor and v6.12 write these six hidden-TCS fields without requiring
 * an echo readback. No source here specifies their access type/field masks.
 * In particular actual581 ADDR0 read82204514 after write0; do not invent a
 * whole-word software ACK or interpret that raw value as a named field.
 * Index follows the exact vendor DATA0/ADDR0/DATA1/ADDR1/DATA2/ADDR2 order. */
static inline int KLRHiddenCommandIndex(uint32_t off)
{
 const uint32_t offsets[6]={0x38,0x34,0x40,0x3c,0x48,0x44};
 for(unsigned i=0;i<6;i++)if(off==offsets[i])return (int)i;return -1;
}
static inline uint32_t KLRHiddenCommandExpected(unsigned index)
{return index==4?0x80000000u:0;}
static inline void KLRInit(KebabGpuLaunchResourceRecord *r)
{memset(r,0,sizeof *r);r->magic=0x31524c4b;r->schema=1;r->bytes=sizeof *r;r->result=95;r->failed_region=r->failed_offset=UINT32_MAX;r->bootstrap_vote_result=INT32_MIN;}
static inline int KLRReadAllowed(unsigned region,uint32_t off)
{
 if(region==KLR_GMU)return off==0x14000 || off==0x24c1c || off==0x24c20;
 if(region==KLR_PDC)return off==0x4500 || off==0x4520;
 if(region==KLR_CC)return off==0x1098 || off==0x109c || off==0x1120 || off==0x1124;
 if(region==KLR_GCC)return off==0x71154;
 if(region!=KLR_RSC)return 0;if(off==0x404)return 1;
 for(unsigned i=0;i<sizeof(KLRRscSetup)/sizeof(KLRRscSetup[0]);i++)if(off==KLRRscSetup[i].offset)return 1;return 0;
}
static inline int KLRWriteAllowed(unsigned region,uint32_t off,uint32_t value)
{
 if(region==KLR_PDC)return (off==0x4500 && value==0x80000001) || (off==0x4520 && !value);
 if(region==KLR_GMU)return off==0x24c1c && value<=1;
 if(region!=KLR_RSC)return 0;
 for(unsigned i=0;i<sizeof(KLRRscSetup)/sizeof(KLRRscSetup[0]);i++)if(off==KLRRscSetup[i].offset && value==KLRRscSetup[i].value)return 1;return 0;
}
static inline int KLRGate(KebabGpuLaunchResourceContext *c)
{
 int e=c->io->gate_reset_held_locked(c->io->cookie);if(e)return e;
 uint64_t now=c->io->now_us(c->io->cookie);if(now<c->begin || now-c->begin>=100000)return 110;return 0;
}
static inline int KLRRead(KebabGpuLaunchResourceContext *c,unsigned region,uint32_t off,uint32_t *value)
{
 int e;if(!value || !KLRReadAllowed(region,off))return 22;if((e=KLRGate(c)))return e;
 c->r->reads++;e=c->io->read32(c->io->cookie,region,off,value);
 if(e){c->r->failed_region=region;c->r->failed_offset=off;c->r->failed_operation=1;c->r->io_error=e;return e;}
 c->r->last_read=*value;return 0;
}
static inline int KLRWrite(KebabGpuLaunchResourceContext *c,unsigned region,uint32_t off,uint32_t value)
{
 int e;if(!KLRWriteAllowed(region,off,value))return 22;if((e=KLRGate(c)))return e;
 c->ledger->touched=1;c->r->writes++;c->r->last_write=value;
 e=c->io->write32(c->io->cookie,region,off,value);
 if(e){c->r->failed_region=region;c->r->failed_offset=off;c->r->failed_operation=2;c->r->io_error=e;return e;}
 c->r->barriers++;e=c->io->barrier(c->io->cookie);
 if(e){c->r->failed_operation=3;c->r->io_error=e;}return e;
}
static inline int KLRSync(KebabGpuLaunchResourceContext *c,unsigned region,uint32_t off,uint32_t value,uint32_t mask)
{
 uint32_t got;int e;if((e=KLRWrite(c,region,off,value)))return e;
 uint64_t begin=c->io->now_us(c->io->cookie);
 for(unsigned i=0;i<1000;i++){
  if((e=KLRRead(c,region,off,&got)))return e;
  if((got&mask)==(value&mask))return 0;
  uint64_t now=c->io->now_us(c->io->cookie);if(now<begin || now-begin>=10000)break;
 }c->r->failed_region=region;c->r->failed_offset=off;c->r->failed_operation=4;return 110;
}
static inline int KLRPoll(KebabGpuLaunchResourceContext *c,unsigned region,uint32_t off,uint32_t mask,uint32_t wanted,uint32_t *last)
{
 int e;uint64_t begin=c->io->now_us(c->io->cookie);
 for(unsigned i=0;i<100;i++){
  if((e=KLRRead(c,region,off,last)))return e;c->r->polls++;
  if((*last&mask)==wanted)return 0;
  uint64_t now=c->io->now_us(c->io->cookie);if(now<begin || now-begin>=10000)break;
  c->r->delays++;if((e=c->io->delay_us(c->io->cookie,100))){c->r->io_error=e;c->r->failed_operation=5;return e;}
 }c->r->failed_region=region;c->r->failed_offset=off;c->r->failed_operation=6;return 110;
}
static inline int KLRConfigureRsc(KebabGpuLaunchResourceContext *c)
{
 uint32_t v;int e;
 if((e=KLRRead(c,KLR_GMU,0x14000,&v)))return e;c->r->reset_readback=v;if(!(v&1))return 95;
 if((e=KLRRead(c,KLR_GMU,0x24c1c,&v)))return e;c->r->req=v;if(v)return 16;
 if((e=KLRRead(c,KLR_GMU,0x24c20,&v)))return e;c->r->ack=v;if(v&2)return 16;
 if((e=KLRRead(c,KLR_RSC,0x404,&v)))return e;c->r->busy=v;if(v)return 16;
 for(unsigned i=0;i<sizeof(KLRRscSetup)/sizeof(KLRRscSetup[0]);i++){
  const KLRSetting *w=&KLRRscSetup[i];uint32_t mask=w->offset==0x10?0x01000000u:UINT32_MAX;
  if(KLRHiddenCommandIndex(w->offset)>=0){
   if((e=KLRWrite(c,KLR_RSC,w->offset,w->value)))return e;
   /* Single diagnostic sample, never a completion predicate. Native keeps
    * these raw words separately; last_read may change at the next step. */
   if((e=KLRRead(c,KLR_RSC,w->offset,&v)))return e;
  }else if((e=KLRSync(c,KLR_RSC,w->offset,w->value,mask)))return e;
 }c->r->sequence_verified=1;
 /* A650 pdc_in_aop=true: only these two config registers. The AOP owns
  * sequence/TCS scripts. Never map or write the PDC sequence resource. */
 if((e=KLRSync(c,KLR_PDC,0x4520,0,UINT32_MAX)))return e;c->r->pdc_start=0;
 if((e=KLRSync(c,KLR_PDC,0x4500,0x80000001,0x80000001)))return e;c->r->pdc_enable=0x80000001;
 /* Source-backed initial stop leaves a known sleeping RSC. The separate
  * GMU launch core later performs fresh REQ2/ACK2/BUSY0/REQ0 wake. */
 if((e=KLRSync(c,KLR_GMU,0x24c1c,1,UINT32_MAX)))return e;c->r->req=1;
 if((e=KLRPoll(c,KLR_RSC,0x10,1u<<16,1u<<16,&c->r->status0)))return e;
 if((e=KLRSync(c,KLR_GMU,0x24c1c,0,UINT32_MAX)))return e;c->r->req=0;
 if((e=KLRPoll(c,KLR_GMU,0x24c20,2,0,&c->r->ack)))return e;
 /* Source stop requires STATUS16, not idle BUSY. A sleeping sequencer can
  * remain busy until the later REQ2/ACK2 wake, where BUSY0 is mandatory.
  * Keep this raw sample diagnostic; initial preprogram BUSY0 is unchanged. */
 if((e=KLRRead(c,KLR_RSC,0x404,&c->r->busy)))return e;
 c->ledger->ready_mask|=KLR_NEED_RSC;c->r->ready_mask=c->ledger->ready_mask;c->r->state=KLR_RSC_SLEEPING;return 0;
}
/* Caller claims its private one-shot ledger before invoking this function.
 * Unknown writes/timeouts quarantine and retain all parent/map resources.
 * There are no retry, restore, disable, release or sleep fallbacks here. */
static inline int KLRPrepareRsc(const KebabGpuLaunchResourceIo *io,KebabGpuLaunchResourceLedger *ledger,KebabGpuLaunchResourceRecord *r)
{
 if(!io || !ledger || !r || !io->gate_reset_held_locked || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us)return 22;
 KLRInit(r);if(ledger->attempted){r->ready_mask=ledger->ready_mask;r->quarantined=ledger->quarantined;return r->result=ledger->quarantined?5:16;}
 ledger->attempted=1;r->attempts=1;r->state=KLR_PREPARING;
 KebabGpuLaunchResourceContext c={io,ledger,r,io->now_us(io->cookie)};int e=KLRConfigureRsc(&c);
 uint64_t now=io->now_us(io->cookie);r->elapsed_us=now>=c.begin?now-c.begin:0;
 if(e && ledger->touched){ledger->quarantined=1;r->quarantined=1;r->state=KLR_QUARANTINED;}
 r->result=(uint32_t)e;return e;
}
#endif
