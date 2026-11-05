/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_BUS_CORE_H
#define KEBAB_GPU_BUS_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* SM8250 GPU SMMU read-only configuration bus, not DMA/namespace permission.
 * Only add bit0 votes: GCC7100c/71018 and GPUCC5000. No5004/reset/GMU writes.
 * The caller holds the canonical native RPMh globalbusy and actual parent pins. */
enum {KebabGpuBusInspect=0,KebabGpuBusObserve=1,KebabGpuBusEnableHold=2};
enum {KebabGpuBusSlots=8,KebabGpuBusPolls=200,KebabGpuBusMaxReaders=4};
typedef struct {
 void *cookie;int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t);
 int (*barrier)(void *);int (*delay_us)(void *,uint32_t);
} KebabGpuBusIo;
typedef struct {
 uint32_t schema,ready,read_cb_mask;
 uint8_t actual_cc_receipt_sha256[32],native_owner_source_sha256[32],fdt_sha256[32];
} KebabGpuBusBinding;
typedef struct {uint64_t generation,sequence;uint32_t slot,held,read_cb_mask;} KebabGpuBusToken;
typedef struct {
 uint32_t magic,schema,bytes,action,result,primary_error,same_native_owner,owner_dead;
 uint32_t held,attempted,quarantined,pinned_parent,parent_refs,parent_children,readers,can_read,evidence_replayed;
 uint32_t reads,writes,barriers,delays,polls,failed_slot,failed_op,io_error;
 uint32_t before[2][8],after[2][8],valid_before[2],valid_after[2],stable_controls;
 uint32_t last[8],last_valid,clock_ack_mask,changed_mask,read_cb_mask,dma_bandwidth_qualified,parent_gdscr_control_mask;
 uint64_t generation;
} KebabGpuBusRecord;
typedef struct {
 uint32_t held,attempted,quarantined,pinned_parent,readers,control[3];
 uint32_t parent_gdscr,parent_gdscr_mask,parent_ahb,cfg_control,reset;
 uint64_t generation,sequence;
 KebabGpuBusToken *token[4];uint64_t token_sequence[4];KebabGpuBusIo io;
} KebabGpuBusLedger;
typedef struct {KebabGpuBusLedger *ledger;KebabGpuBusRecord *r;const KebabGpuBusIo *io;} KebabGpuBusContext;
static inline void KebabGpuBusInit(KebabGpuBusRecord *r,unsigned action)
{memset(r,0,sizeof *r);r->magic=0x31534247;r->schema=1;r->bytes=sizeof *r;r->action=action;r->result=95;r->failed_slot=UINT32_MAX;}
static inline int KebabGpuBusBound(const KebabGpuBusBinding *b)
{
 unsigned i;uint8_t x=0,y=0,z=0;if(!b || b->schema!=1 || b->ready!=1 || !b->read_cb_mask || (b->read_cb_mask&~31u))return 0;
 for(i=0;i<32;i++){x|=b->actual_cc_receipt_sha256[i];y|=b->native_owner_source_sha256[i];z|=b->fdt_sha256[i];}return x && y && z;
}
static inline int KebabGpuBusOffset(unsigned slot,unsigned *region,uint32_t *offset)
{
 static const uint32_t off[8]={0x71004,0x71000,0x106c,0x1540,0x1078,0x7100c,0x71018,0x5000};
 if(slot>=8 || !region || !offset)return 0;*region=slot<2 || slot==5 || slot==6?0:1;*offset=off[slot];return 1;
}
static inline int KebabGpuBusAllowed(unsigned region,uint32_t off)
{unsigned i,r;uint32_t o;for(i=0;i<8;i++){KebabGpuBusOffset(i,&r,&o);if(r==region && o==off)return 1;}return 0;}
static inline uint32_t KebabGpuBusWriteMask(unsigned region,uint32_t off)
{return (region==0 && (off==0x7100c || off==0x71018)) || (region==1 && off==0x5000)?1u:0u;}
static inline uint32_t KebabGpuBusControlMask(unsigned slot,uint32_t parent_mask)
{if(slot==1)return UINT32_MAX;if(slot==2)return parent_mask;if(slot==3)return 0;return 0x0fffffffu;}
static inline int KebabGpuBusRaw(KebabGpuBusContext *c,unsigned slot,uint32_t *v)
{
 unsigned region;uint32_t off;int e;if(!v || !KebabGpuBusOffset(slot,&region,&off))return 22;
 c->r->reads++;e=c->io->read32(c->io->cookie,region,off,v);
 if(e){c->r->failed_slot=slot;c->r->failed_op=1;c->r->io_error=(uint32_t)e;return e;}
 c->r->last[slot]=*v;c->r->last_valid|=1u<<slot;return 0;
}
static inline int KebabGpuBusBarrier(KebabGpuBusContext *c)
{int e;c->r->barriers++;e=c->io->barrier(c->io->cookie);if(e){c->r->failed_op=3;c->r->io_error=e;}return e;}
static inline int KebabGpuBusDelay(KebabGpuBusContext *c,unsigned us)
{int e;if(us!=1 && us!=10)return 22;c->r->delays++;e=c->io->delay_us(c->io->cookie,us);if(e){c->r->failed_op=4;c->r->io_error=e;}return e;}
static inline int KebabGpuBusParents(KebabGpuBusContext *c)
{
 uint32_t v;int e;KebabGpuBusLedger *l=c->ledger;
 if((e=KebabGpuBusRaw(c,0,&v)))return e;
 if((v&0x80000001u)!=1 || (v&0x0fffffff)!=l->cfg_control)return 11;
 if((e=KebabGpuBusRaw(c,1,&v)))return e;if(v!=l->reset || (v&1))return 11;
 if((e=KebabGpuBusRaw(c,2,&v)))return e;
 if((l->parent_gdscr_mask&0x00fff807u)!=0x00fff807u ||
    (v&l->parent_gdscr_mask)!=l->parent_gdscr || (v&1) || (v&6))return 11;
 if((e=KebabGpuBusRaw(c,3,&v)))return e;if(!(v&0x80000000u))return 11;
 if((e=KebabGpuBusRaw(c,4,&v)))return e;
 if((v&0x0fffffff)!=l->parent_ahb || !(v&1))return 11;
 return KebabGpuBusBarrier(c);
}
static inline int KebabGpuBusRead(KebabGpuBusContext *c,unsigned slot,uint32_t *v)
{int e=KebabGpuBusParents(c);return e?e:KebabGpuBusRaw(c,slot,v);}
static inline int KebabGpuBusSnapshot(KebabGpuBusContext *c,int after)
{
 unsigned pass,slot;int e;uint32_t (*s)[8]=after?c->r->after:c->r->before;
 uint32_t *valid=after?c->r->valid_after:c->r->valid_before;
 for(pass=0;pass<2;pass++)for(slot=0;slot<8;slot++) {
  e=KebabGpuBusRead(c,slot,&s[pass][slot]);if(e)return e;valid[pass]|=1u<<slot;
 }
 c->r->stable_controls=0;
 for(slot=0;slot<8;slot++)if(((s[0][slot]^s[1][slot])&KebabGpuBusControlMask(slot,c->ledger->parent_gdscr_mask))==0)c->r->stable_controls|=1u<<slot;
 return c->r->stable_controls==255?0:11;
}
static inline int KebabGpuBusVote(KebabGpuBusContext *c,unsigned slot)
{
 unsigned region,i;uint32_t off,v,expect;int e;KebabGpuBusLedger *l=c->ledger;
 if(slot<5 || slot>7)return 22;expect=l->control[slot-5];
 if((e=KebabGpuBusRead(c,slot,&v)))return e;
 if((v&0x0fffffff)!=expect)return 11;
 if(!(v&1)) {
  if((e=KebabGpuBusParents(c)))return e;
  KebabGpuBusOffset(slot,&region,&off);l->attempted=1;c->r->writes++;
  e=c->io->write32(c->io->cookie,region,off,v|1u);
  if(e){c->r->failed_slot=slot;c->r->failed_op=2;c->r->io_error=e;return e;}
  c->r->changed_mask|=1u<<(slot-5);
  if((e=KebabGpuBusBarrier(c)))return e;
 }
 l->control[slot-5]=expect|1u;
 /* clk-branch2 enable: mb then <=200 reads/1us, including BRANCH_VOTED.
  * No wait-for-halt on disable is used: this core never removes a vote. */
 for(i=0;i<KebabGpuBusPolls;i++) {
  c->r->polls++;if((e=KebabGpuBusRead(c,slot,&v)))return e;
  if((v&0x0fffffff)!=(expect|1u))return 11;
  if(!(v&0x80000000u)){c->r->clock_ack_mask|=1u<<(slot-5);return 0;}
  if((e=KebabGpuBusDelay(c,1)))return e;
 }
 c->r->failed_slot=slot;c->r->failed_op=5;return 110;
}
static inline int KebabGpuBusVerify(KebabGpuBusContext *c)
{
 unsigned slot;uint32_t v;int e;
 if((e=KebabGpuBusParents(c)))return e;
 for(slot=5;slot<8;slot++) {
  if((e=KebabGpuBusRead(c,slot,&v)))return e;
  if((v&0x0fffffff)!=c->ledger->control[slot-5] || (v&0x80000001u)!=1)return 11;
 }
 return 0;
}
static inline int KebabGpuBusRun(KebabGpuBusContext *c,unsigned action)
{
 unsigned i;int e;uint32_t cfg,reset;
 if((e=KebabGpuBusRaw(c,0,&cfg)) || (e=KebabGpuBusRaw(c,1,&reset)))return e;
 if((cfg&0x80000001u)!=1 || (reset&1))return 95;
 c->ledger->cfg_control=cfg&0x0fffffff;c->ledger->reset=reset;
 if((e=KebabGpuBusSnapshot(c,0)))return e;
 if(action==KebabGpuBusObserve)return 0;
 for(i=0;i<3;i++)c->ledger->control[i]=c->r->before[0][5+i]&0x0fffffff;
 for(i=5;i<8;i++){e=KebabGpuBusVote(c,i);if(e)return e;}
 if((e=KebabGpuBusSnapshot(c,1)) || (e=KebabGpuBusVerify(c)))return e;
 c->ledger->held=1;return 0;
}
#endif
