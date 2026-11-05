/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_ROOT_CORE_H
#define KEBAB_GMU_ROOT_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabGpuCcCore.h" /* canonical persistent-field contract */
/* Fixed SM8250 200MHz controller-clock root. No CM3 branch enable, reset,
 * GPU PLL, GMU CSR, GX domain, memory or firmware access. GPLL0 is shared:
 * only calibrated FSM mode is supported, only its APPS vote is asserted,
 * and that keepalive vote is NEVER cleared by this owner. */
enum {KebabGmuRootObserve=0,KebabGmuRootHold200=1,KebabGmuRootRestore=2,KebabGmuRootInspect=3};
enum {KebabGmuRootSlots=16};
typedef struct {
 void *cookie;
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t,uint32_t,uint32_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,uint32_t);
 uint64_t (*now_us)(void *);
} KebabGmuRootIo;
typedef struct {
 uint32_t held,pinned,attempted,quarantined,vote_added;
 uint32_t saved_cfg,saved_cmd,saved_div,saved_vote;
 uint32_t cfg,cmd,div,vote;
} KebabGmuRootLedger;
typedef struct {
 uint32_t magic,schema,bytes,action,result,same_native_owner,parent_qualified,parent_refs;
 uint32_t cc_held,cc_children,held,pinned,quarantined,owner_dead,evidence_replayed,failure_result;
 uint32_t reads,writes,barriers,delay_calls,delay_us,polls,failed_slot,failed_op,io_error;
 uint32_t before_valid[2],after_valid[2],stable_before,stable_after,last_valid,last[16];
 uint32_t before[2][16],after[2][16];
 uint32_t pll_rate_hz,div_rate_hz,root_rate_hz,parent_active_ack,source_div_request_ack;
 uint32_t root_on_ack,update_ack,branches_quiet,restored,pll_vote_added,pll_vote_retained;
 uint32_t frequency_measured,cm3_reset_owned,cm3_branch_enabled,firmware_started,downstream_qualified;
} KebabGmuRootRecord;
typedef struct {const KebabGmuRootIo *io;KebabGmuRootLedger *ledger;KebabGmuRootRecord *r;uint32_t gcc_cfg,gcc_reset,cc_gdscr,cc_ahb,cc_gmu,cc_gx_gmu;uint32_t fixed[16];} KebabGmuRootContext;
static inline void KebabGmuRootInit(KebabGmuRootRecord *r,unsigned action)
{memset(r,0,sizeof *r);r->magic=0x524d474bu;r->schema=1;r->bytes=sizeof *r;r->action=action;r->result=22;r->failed_slot=UINT32_MAX;}
static inline int KebabGmuRootOffset(unsigned slot,unsigned *region,uint32_t *offset)
{
 static const uint32_t off[16]={0x71004,0x71000,0x106c,0x1540,0x1078,0x1098,0x1064,0x1120,0x1124,0,4,0xc,0x38,0x40,0x52018,0x52000};
 if(slot>=16 || !region || !offset)return 0;*region=slot>=2 && slot<=8?1:0;*offset=off[slot];return 1;
}
static inline int KebabGmuRootAllowed(unsigned region,uint32_t offset)
{unsigned i,r;uint32_t o;for(i=0;i<16;i++){(void)KebabGmuRootOffset(i,&r,&o);if(region==r && offset==o)return 1;}return 0;}
static inline uint32_t KebabGmuRootWriteMask(unsigned region,uint32_t offset)
{if(region==0){if(offset==0x52018)return 1;if(offset==0x52000)return 0x10000;return 0;}if(region==1){if(offset==0x1120)return 3;if(offset==0x1124)return 0x10371f;}return 0;}
static inline uint32_t KebabGmuRootInvariantMask(unsigned slot)
{if(slot==2)return KebabGpuCcControlMask(2);if(slot==4 || slot==5 || slot==6)return 0x0fffffffu;if(slot==7)return 0x7fffff0fu;if(slot==9)return 0x3fffffffu;return UINT32_MAX;}
static inline int KebabGmuRootQuiet(uint32_t v){return (v&0x80000001u)==0x80000000u;}
static inline int KebabGmuRootRaw(KebabGmuRootContext *c,unsigned slot,uint32_t *value)
{
 unsigned region;uint32_t offset,v=0;int e;if(!value || !KebabGmuRootOffset(slot,&region,&offset))return 22;
 ++c->r->reads;e=c->io->read32(c->io->cookie,region,offset,&v);
 if(e){c->r->failed_slot=slot;c->r->failed_op=1;c->r->io_error=(uint32_t)e;return 5;}
 *value=v;c->r->last[slot]=v;c->r->last_valid|=1u<<slot;return 0;
}
static inline int KebabGmuRootBarrier(KebabGmuRootContext *c)
{int e;++c->r->barriers;e=c->io->barrier(c->io->cookie);if(e){c->r->failed_op=3;c->r->io_error=(uint32_t)e;return 5;}return 0;}
static inline int KebabGmuRootDelay(KebabGmuRootContext *c,uint32_t us)
{int e;if(us!=1 && us!=10)return 22;++c->r->delay_calls;c->r->delay_us+=us;e=c->io->delay_us(c->io->cookie,us);if(e){c->r->failed_op=4;c->r->io_error=(uint32_t)e;return 5;}return 0;}
static inline int KebabGmuRootBusGates(KebabGmuRootContext *c)
{
 uint32_t cfg,reset;int e;
 if((e=KebabGmuRootRaw(c,0,&cfg)) || (e=KebabGmuRootRaw(c,1,&reset)))return e;
 if(cfg!=c->gcc_cfg || reset!=c->gcc_reset || !(cfg&1) || (cfg&0x80000000u) || (reset&1))return 11;
 return KebabGmuRootBarrier(c);
}
static inline int KebabGmuRootControlRaw(KebabGmuRootContext *c,unsigned slot,uint32_t *v)
{int e;if(slot<2 || slot>8)return 22;e=KebabGmuRootBusGates(c);return e?e:KebabGmuRootRaw(c,slot,v);}
static inline int KebabGmuRootGates(KebabGmuRootContext *c)
{
 uint32_t gds,hw,ahb,gmu,gx;int e;
 if((e=KebabGmuRootControlRaw(c,2,&gds)) || (e=KebabGmuRootControlRaw(c,3,&hw)) || (e=KebabGmuRootControlRaw(c,4,&ahb)) || (e=KebabGmuRootControlRaw(c,5,&gmu)) || (e=KebabGmuRootControlRaw(c,6,&gx)))return e;
 if((gds&KebabGpuCcControlMask(2))!=c->cc_gdscr || !(hw&0x80000000u) || (ahb&0x0fffffffu)!=c->cc_ahb || !(ahb&1) ||
    (gmu&0x0fffffffu)!=c->cc_gmu || (gx&0x0fffffffu)!=c->cc_gx_gmu || !KebabGmuRootQuiet(gmu) || !KebabGmuRootQuiet(gx))return 11;
 return KebabGmuRootBarrier(c);
}
static inline int KebabGmuRootRead(KebabGmuRootContext *c,unsigned slot,uint32_t *v)
{int e=KebabGmuRootGates(c);return e?e:KebabGmuRootRaw(c,slot,v);}
static inline int KebabGmuRootSnapshot(KebabGmuRootContext *c,int after)
{
 uint32_t (*sample)[16]=after?c->r->after:c->r->before;
 uint32_t *valid=after?c->r->after_valid:c->r->before_valid,*stable=after?&c->r->stable_after:&c->r->stable_before;
 unsigned pass,slot;int e;
 for(pass=0;pass<2;pass++){
  /* Before the first controller read, rails/CC ledger are held by the same
   * owner; validate CPU CFG/reset first. No downstream aperture is mapped. */
  for(slot=0;slot<2;slot++){if((e=KebabGmuRootRaw(c,slot,&sample[pass][slot])))return e;valid[pass]|=1u<<slot;}
  if(!(sample[pass][0]&1) || (sample[pass][0]&0x80000000u) || (sample[pass][1]&1))return 95;
  if(!after && !pass){c->gcc_cfg=sample[0][0];c->gcc_reset=sample[0][1];}
  if(sample[pass][0]!=c->gcc_cfg || sample[pass][1]!=c->gcc_reset)return 11;
  if((e=KebabGmuRootBarrier(c)))return e;
  for(slot=2;slot<16;slot++){e=slot<=8?KebabGmuRootControlRaw(c,slot,&sample[pass][slot]):KebabGmuRootRaw(c,slot,&sample[pass][slot]);if(e)return e;valid[pass]|=1u<<slot;}
  if((e=KebabGmuRootBarrier(c)))return e;
 }
 for(slot=0;slot<16;slot++)if(sample[0][slot]==sample[1][slot])*stable|=1u<<slot;
 return *stable==0xffff?0:11;
}
static inline int KebabGmuRootPllShape(const uint32_t *v)
{
 uint64_t pll=UINT64_C(19200000)*v[10]+((UINT64_C(19200000)*(v[13]&0xffffu))>>16);
 /* FSM needs ACTIVE, not software OPMODE_RUN; never calibrate/reprogram.
  * Keep scalar-rate conversion exact; no invented 300MHz default. */
 if(!(v[9]&(1u<<20)) || !(v[9]&(1u<<27)) || ((v[11]>>8)&15)!=1 || (v[13]&0xffff0000u) || pll!=600000000)return 95;
 return 0;
}
static inline int KebabGmuRootPrepare(KebabGmuRootContext *c)
{
 const uint32_t *v;unsigned slot;uint32_t source;int e=KebabGmuRootSnapshot(c,0);if(e)return e;v=c->r->before[0];
 if((v[2]&1) || (v[2]&6) || !(v[3]&0x80000000u) || !(v[4]&1) || !KebabGmuRootQuiet(v[5]) || !KebabGmuRootQuiet(v[6]))return 95;
 c->cc_gdscr=v[2]&KebabGpuCcControlMask(2);c->cc_ahb=v[4]&0x0fffffffu;c->cc_gmu=v[5]&0x0fffffffu;c->cc_gx_gmu=v[6]&0x0fffffffu;
 for(slot=0;slot<16;slot++)c->fixed[slot]=v[slot];
 if(c->r->action==KebabGmuRootObserve)return 0;
 if((e=KebabGmuRootPllShape(v)))return e;
 if(v[7]&0xf1)return 16; /* Fresh UPDATE=0 and no dirty shadow transaction. */
 if(v[8]&0x3000)return 95; /* MND disabled for this fixed mnd_width0 RCG. */
 source=(v[8]>>8)&7;
 /* Force-on precedes configure in the vendor safe sequence. The old parent
  * therefore must be known-live XO, or already-voted/active GPLL0_DIV.
  * Never force a root using an unknown/off GPU_PLL1 or change that PLL. */
 if(source!=0 && source!=6)return 95;
 if(source==6 && (!(v[14]&1) || !(v[15]&0x10000) || (v[9]&0xc0000000u)!=0xc0000000u))return 95;
 return KebabGmuRootGates(c);
}
static inline int KebabGmuRootUpdate(KebabGmuRootContext *c,unsigned slot,uint32_t mask,uint32_t bits,uint32_t *expected)
{
 unsigned region;uint32_t off,current,want,stable=KebabGmuRootInvariantMask(slot);int e;
 if(!expected || !KebabGmuRootOffset(slot,&region,&off) || !mask || (mask&~KebabGmuRootWriteMask(region,off)))return 22;
 if((e=KebabGmuRootRead(c,slot,&current)))return e;
 if(((current^*expected)&stable)!=0 || (slot==7 && (current&1)))return 11;
 want=(current&~mask)|(bits&mask);
 if((e=KebabGmuRootGates(c)))return e;
 c->ledger->attempted=1;++c->r->writes;
 e=c->io->write32(c->io->cookie,region,off,mask,bits,current);
 if(e){c->r->failed_slot=slot;c->r->failed_op=2;c->r->io_error=(uint32_t)e;return 5;}
 if((e=KebabGmuRootBarrier(c)))return e;
 *expected=want;return 0;
}
static inline int KebabGmuRootPoll(KebabGmuRootContext *c,unsigned slot,uint32_t mask,uint32_t bits,uint32_t expected)
{
 uint32_t v;unsigned i;uint64_t start=c->io->now_us(c->io->cookie);int e;
 for(i=0;i<500;i++){
  ++c->r->polls;if((e=KebabGmuRootRead(c,slot,&v)))return e;
  if(((v^expected)&KebabGmuRootInvariantMask(slot)&~mask)!=0)return 11;
  if((v&mask)==bits)return 0;
  if((e=KebabGmuRootDelay(c,1)))return e;
  if(c->io->now_us(c->io->cookie)-start>=500)break;
 }
 c->r->failed_slot=slot;c->r->failed_op=5;return 110;
}
static inline int KebabGmuRootVerifyParent(KebabGmuRootContext *c)
{
 uint32_t v[16];unsigned i;int e;
 memcpy(v,c->fixed,sizeof v);
 for(i=9;i<16;i++){if((e=KebabGmuRootRead(c,i,&v[i])))return e;}
 if((e=KebabGmuRootPllShape(v)))return e;
 /* FSM ACTIVE is authoritative; software OPMODE may differ without a
  * rate/configuration change. It is recorded but neither required RUN nor
  * used as a stable-control proof in this FSM-only path. */
 for(i=10;i<=13;i++)if(i!=12 && v[i]!=c->fixed[i])return 11;
 if(((v[9]^c->fixed[9])&KebabGmuRootInvariantMask(9)) || (v[9]&0xc0000000u)!=0xc0000000u || !(v[14]&1) || !(v[15]&0x10000))return 11;
 c->r->pll_rate_hz=600000000;c->r->div_rate_hz=300000000;c->r->parent_active_ack=1;return 0;
}
static inline int KebabGmuRootHold(KebabGmuRootContext *c)
{
 KebabGmuRootLedger *b=c->ledger;KebabGmuRootRecord *r=c->r;const uint32_t *v=r->before[0];int e;
 b->saved_cfg=b->cfg=v[8];b->saved_cmd=b->cmd=v[7];b->saved_div=b->div=v[15];b->saved_vote=b->vote=v[14];
 /* Reassert one shared keepalive vote. It is a permanent minimum floor;
  * never clear it on restore, even when inherited bit0 was zero. */
 if((e=KebabGmuRootUpdate(c,14,1,1,&b->vote)))return e;b->vote_added=!(b->saved_vote&1);
 if((e=KebabGmuRootPoll(c,9,0xc0000000u,0xc0000000u,c->fixed[9])))return e;
 if((e=KebabGmuRootUpdate(c,15,0x10000,0x10000,&b->div)) || (e=KebabGmuRootDelay(c,10)))return e;
 if((e=KebabGmuRootVerifyParent(c)))return e;r->source_div_request_ack=1;
 if((e=KebabGmuRootUpdate(c,7,2,2,&b->cmd)) || (e=KebabGmuRootPoll(c,7,0x80000002u,2,b->cmd)))return e;r->root_on_ack=1;
 if((e=KebabGmuRootUpdate(c,8,0x10371f,0x100602,&b->cfg)))return e;
 if((e=KebabGmuRootPoll(c,8,0x10371f,0x100602,b->cfg)))return e;
 if((e=KebabGmuRootUpdate(c,7,1,1,&b->cmd)) || (e=KebabGmuRootPoll(c,7,0xf1,0,b->cmd)))return e;b->cmd&=~1u;r->update_ack=1;
 if((e=KebabGmuRootVerifyParent(c)) || (e=KebabGmuRootSnapshot(c,1)))return e;
 v=r->after[0];if((v[7]&0x800000f3u)!=2 || v[8]!=b->cfg || v[14]!=b->vote || v[15]!=b->div || !KebabGmuRootQuiet(v[5]) || !KebabGmuRootQuiet(v[6]))return 11;
 r->root_rate_hz=200000000;r->branches_quiet=1;b->held=1;return 0;
}
static inline int KebabGmuRootRestoreSaved(KebabGmuRootContext *c)
{
 KebabGmuRootLedger *b=c->ledger;KebabGmuRootRecord *r=c->r;const uint32_t *v=r->before[0];int e;
 if(v[8]!=b->cfg || ((v[7]^b->cmd)&KebabGmuRootInvariantMask(7)) || v[14]!=b->vote || v[15]!=b->div)return 11;
 if((e=KebabGmuRootVerifyParent(c)))return e;
 if((e=KebabGmuRootUpdate(c,8,0x10371f,b->saved_cfg,&b->cfg)) || (e=KebabGmuRootPoll(c,8,0x10371f,b->saved_cfg&0x10371f,b->cfg)))return e;
 if((e=KebabGmuRootUpdate(c,7,1,1,&b->cmd)) || (e=KebabGmuRootPoll(c,7,0xf1,0,b->cmd)))return e;b->cmd&=~1u;
 if((e=KebabGmuRootUpdate(c,7,2,b->saved_cmd,&b->cmd)) || (e=KebabGmuRootPoll(c,7,3,b->saved_cmd&3,b->cmd)))return e;
 if((e=KebabGmuRootUpdate(c,15,0x10000,b->saved_div,&b->div)) || (e=KebabGmuRootDelay(c,10)))return e;
 if((e=KebabGmuRootSnapshot(c,1)))return e;v=r->after[0];
 if(v[8]!=b->saved_cfg || ((v[7]^b->saved_cmd)&KebabGmuRootInvariantMask(7)) || v[15]!=b->saved_div || v[14]!=b->vote)return 11;
 r->restored=1;r->branches_quiet=1;b->held=0;return 0;
}
#endif
