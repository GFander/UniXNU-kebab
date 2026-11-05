/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_CC_CORE_H
#define KEBAB_GPU_CC_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* SM8250 controller only. No GPU/GMU/GX/SMMU access or arbitrary offsets.
 * Existing native RPMh owner must hold real CX+MX refs across this core.
 * Internal AHB/PWR_ON are observations, not CC-register access gates. */
enum {KebabGpuCcObserve=0,KebabGpuCcEnableHold=1,KebabGpuCcRestore=2,KebabGpuCcInspect=3};
enum {KebabGpuCcGcc=0,KebabGpuCcControl=1,KebabGpuCcSlots=6};
typedef struct {
 void *cookie;
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,uint32_t);
 uint64_t (*now_us)(void *);
} KebabGpuCcIo;
typedef struct {
 uint32_t held,pinned_parent,attempted,quarantined,retention_added;
 uint32_t original_gdscr,original_ahb,original_gmu,gdscr_control,ahb_control;
} KebabGpuCcLedger;
typedef struct {
 uint32_t magic,schema,bytes,action,result,same_native_owner,parent_qualified,parent_refs,parent_ack_mask;
 uint32_t owner_dead,quarantined,held,pinned_parent,evidence_replayed,failure_result;
 uint32_t reads,write_attempts,barriers,delay_calls,delay_total_us,polls,failed_region,failed_offset,failed_op,io_error;
 uint32_t valid_before[2],valid_after[2],stable_before,stable_after,last_valid,last[6];
 uint32_t before[2][6],after[2][6];
 uint32_t cx_power_ack,ahb_request_ack,ahb_reported_running,retention_added,collapse_and_ahb_restored,full_original_controls_restored;
 uint32_t downstream_qualified,not_run_subsystems;
} KebabGpuCcRecord;
typedef struct {const KebabGpuCcIo *io;KebabGpuCcRecord *r;KebabGpuCcLedger *ledger;uint32_t cfg,reset;} KebabGpuCcContext;
static inline void KebabGpuCcInit(KebabGpuCcRecord *r,unsigned action)
{
 memset(r,0,sizeof *r);r->magic=0x4343474b;r->schema=1;r->bytes=sizeof *r;r->action=action;r->result=22;
 r->failed_region=r->failed_offset=UINT32_MAX;r->not_run_subsystems=7;
}
static inline int KebabGpuCcOffset(unsigned slot,unsigned *region,uint32_t *offset)
{
 static const uint32_t offsets[6]={0x71004,0x71000,0x106c,0x1540,0x1078,0x1098};
 if(slot>=6 || !region || !offset)return 0;*region=slot<2?0:1;*offset=offsets[slot];return 1;
}
static inline int KebabGpuCcAllowed(unsigned region,uint32_t offset)
{
 unsigned i,actual;uint32_t off;
 for(i=0;i<6;i++){(void)KebabGpuCcOffset(i,&actual,&off);if(region==actual && offset==off)return 1;}return 0;
}
static inline uint32_t KebabGpuCcWriteMask(unsigned region,uint32_t offset)
{if(region!=1)return 0;if(offset==0x106c)return 0x801;if(offset==0x1078)return 1;return 0;}
static inline uint32_t KebabGpuCcControlMask(unsigned slot)
{/* Persistent software-control fields, not a complement of observed status.
  * Fixed gdsc.c: SW_COLLAPSE0/HW_CONTROL1/SW_OVERRIDE2,
  * CLK_DIS_WAIT15:12/EN_FEW_WAIT19:16/EN_REST_WAIT23:20.
  * Fixed Kona gdsc-regulator.c also owns RETAIN_FF11.
  * Every other GDSCR bit remains raw evidence and is preserved by native
  * fresh RMW. Unclassified/reserved bits are NOT presumed stable control
  * or assigned invented status meanings. Only HW1540 bit31 supplies ACK.
  * CBCR31 CLK_OFF and28..30 NOC_FSM are RO status, not controls. */
 if(slot==2)return 0x00fff807u;
 return slot==4 || slot==5?0x0fffffffu:0x7fffffffu;}
static inline int KebabGpuCcGmuClockQuiet(uint32_t value)
{/* No owned CM3 reset exists: require branch disabled AND reported halted. */
 return (value&0x80000001u)==0x80000000u;}
static inline int KebabGpuCcRaw(KebabGpuCcContext *c,unsigned slot,uint32_t *out)
{
 unsigned region=0;uint32_t offset=0,value=0;int e;if(!out || !KebabGpuCcOffset(slot,&region,&offset))return 22;
 ++c->r->reads;e=c->io->read32(c->io->cookie,region,offset,&value);
 if(e){c->r->failed_region=region;c->r->failed_offset=offset;c->r->failed_op=1;c->r->io_error=(uint32_t)e;return 5;}
 *out=value;c->r->last[slot]=value;c->r->last_valid|=1u<<slot;return 0;
}
static inline int KebabGpuCcBarrier(KebabGpuCcContext *c)
{int e;++c->r->barriers;e=c->io->barrier(c->io->cookie);if(e){c->r->failed_op=3;c->r->io_error=(uint32_t)e;return 5;}return 0;}
static inline int KebabGpuCcDelay(KebabGpuCcContext *c,uint32_t us)
{
 int e;if(us!=1 && us!=10 && us!=100)return 22;++c->r->delay_calls;c->r->delay_total_us+=us;
 e=c->io->delay_us(c->io->cookie,us);if(e){c->r->failed_op=4;c->r->io_error=(uint32_t)e;return 5;}return 0;
}
static inline int KebabGpuCcGates(KebabGpuCcContext *c)
{
 uint32_t cfg,reset;int e;
 if((e=KebabGpuCcRaw(c,0,&cfg)) || (e=KebabGpuCcRaw(c,1,&reset)))return e;
 if(cfg!=c->cfg || reset!=c->reset || !(cfg&1) || (cfg&0x80000000u) || (reset&1))return 11;
 return KebabGpuCcBarrier(c);
}
static inline int KebabGpuCcRead(KebabGpuCcContext *c,unsigned slot,uint32_t *out)
{int e;if(slot<2)return KebabGpuCcRaw(c,slot,out);e=KebabGpuCcGates(c);return e?e:KebabGpuCcRaw(c,slot,out);}
static inline int KebabGpuCcSnapshot(KebabGpuCcContext *c,int after)
{
 unsigned pass,slot;int e;uint32_t (*sample)[6]=after?c->r->after:c->r->before;
 uint32_t *valid=after?c->r->valid_after:c->r->valid_before,*stable=after?&c->r->stable_after:&c->r->stable_before;
 for(pass=0;pass<2;pass++) {
  for(slot=0;slot<2;slot++){e=KebabGpuCcRaw(c,slot,&sample[pass][slot]);if(e)return e;valid[pass]|=1u<<slot;}
  if(!(sample[pass][0]&1) || (sample[pass][0]&0x80000000u) || (sample[pass][1]&1))return 95;
  if(!after && pass==0){c->cfg=sample[0][0];c->reset=sample[0][1];}
  if(sample[pass][0]!=c->cfg || sample[pass][1]!=c->reset)return 11;
  for(slot=2;slot<6;slot++){e=KebabGpuCcRead(c,slot,&sample[pass][slot]);if(e)return e;valid[pass]|=1u<<slot;}
  if((e=KebabGpuCcBarrier(c)))return e;
 }
 for(slot=0;slot<6;slot++)if(sample[0][slot]==sample[1][slot])*stable|=1u<<slot;
 return *stable==63?0:11;
}
/* Bit writes preserve unowned controls. The adapter independently enforces
 * exact writable masks and rebases the request on its fresh register read. */
static inline int KebabGpuCcUpdate(KebabGpuCcContext *c,unsigned slot,uint32_t mask,uint32_t bits,uint32_t expected)
{
 unsigned region=0,i;uint32_t offset=0,current,value,control_mask=KebabGpuCcControlMask(slot);uint64_t start;int e;
 if(!KebabGpuCcOffset(slot,&region,&offset) || !mask || (mask&~KebabGpuCcWriteMask(region,offset)))return 22;
 if((e=KebabGpuCcRead(c,slot,&current)))return e;
 if((current&control_mask)!=expected)return 11;
 value=(current&~mask)|(bits&mask);if((current&mask)==(value&mask))return 0;
 if((e=KebabGpuCcGates(c)))return e;
 c->ledger->attempted=1;++c->r->write_attempts;
 e=c->io->write32(c->io->cookie,region,offset,value);
 if(e){c->r->failed_region=region;c->r->failed_offset=offset;c->r->failed_op=2;c->r->io_error=(uint32_t)e;return 5;}
 if((e=KebabGpuCcBarrier(c)))return e;
 start=c->io->now_us(c->io->cookie);
 for(i=0;i<1000;i++) {
  if((e=KebabGpuCcRead(c,slot,&current)))return e;
  if((current&control_mask&~mask)!=(expected&~mask))return 11;
  if((current&mask)==(value&mask))return 0;
  if(c->io->now_us(c->io->cookie)-start>=10000)break;
 }
 c->r->failed_region=region;c->r->failed_offset=offset;c->r->failed_op=5;return 110;
}
static inline int KebabGpuCcPowerAck(KebabGpuCcContext *c)
{
 unsigned retry,i;uint32_t hw;uint64_t start;int e;
 /* DT gds-timeout500us; vendor retries once using hw_ctrl1540. */
 for(retry=0;retry<2;retry++) {
  start=c->io->now_us(c->io->cookie);
  for(i=0;i<500;i++) {
   ++c->r->polls;if((e=KebabGpuCcRead(c,3,&hw)))return e;
   if(hw&0x80000000u){c->r->cx_power_ack=1;return 0;}
   if((e=KebabGpuCcDelay(c,1)))return e;
   if(c->io->now_us(c->io->cookie)-start>=500)break;
  }
 }
 return 110;
}
static inline int KebabGpuCcPrepare(KebabGpuCcContext *c)
{
 int e=KebabGpuCcSnapshot(c,0);if(e)return e;
 /* Probe normally clears HW_CONTROL/SW_OVERRIDE; native must not seize it. */
 if(c->r->action==KebabGpuCcEnableHold && ((c->r->before[0][2]&6) || !KebabGpuCcGmuClockQuiet(c->r->before[0][5])))return 95;
 return 0;
}
static inline int KebabGpuCcEnable(KebabGpuCcContext *c)
{
 KebabGpuCcLedger *b=c->ledger;KebabGpuCcRecord *r=c->r;int e;
 b->original_gdscr=r->before[0][2];b->original_ahb=r->before[0][4];b->original_gmu=r->before[0][5];
 b->gdscr_control=b->original_gdscr&KebabGpuCcControlMask(2);b->ahb_control=b->original_ahb&KebabGpuCcControlMask(4);
 e=KebabGpuCcUpdate(c,2,1,0,b->gdscr_control);if(e)return e;b->gdscr_control&=~1u;
 if((e=KebabGpuCcBarrier(c)) || (e=KebabGpuCcDelay(c,1)) || (e=KebabGpuCcPowerAck(c)))return e;
 /* Original DT qcom,retain-regs: vendor installs retention after power ACK.
  * It never clears this policy bit on disable; neither does restore here. */
 if(!(b->gdscr_control&0x800)) {
  e=KebabGpuCcUpdate(c,2,0x800,0x800,b->gdscr_control);if(e)return e;
  b->gdscr_control|=0x800;b->retention_added=1;
 }
 if((e=KebabGpuCcDelay(c,1)) || (e=KebabGpuCcDelay(c,1)))return e;
 e=KebabGpuCcUpdate(c,4,1,1,b->ahb_control);if(e)return e;b->ahb_control|=1;
 if((e=KebabGpuCcDelay(c,10)) || (e=KebabGpuCcSnapshot(c,1)))return e;
 if((r->after[0][2]&KebabGpuCcControlMask(2))!=b->gdscr_control || !(r->after[0][3]&0x80000000u) ||
 (r->after[0][4]&KebabGpuCcControlMask(4))!=b->ahb_control || (r->after[0][5]&KebabGpuCcControlMask(5))!=(b->original_gmu&KebabGpuCcControlMask(5)) ||
 !KebabGpuCcGmuClockQuiet(r->after[0][5]))return 11;
 r->ahb_request_ack=1;r->ahb_reported_running=(r->after[0][4]&0x80000000u)==0;
 b->held=1;r->retention_added=b->retention_added;return 0;
}
static inline int KebabGpuCcRestoreSaved(KebabGpuCcContext *c)
{
 KebabGpuCcLedger *b=c->ledger;KebabGpuCcRecord *r=c->r;int e;
 /* No GMU execution/quiesce owner exists in this phase. Do not withdraw
  * any controller hold while this branch is enabled or not reported halted. */
 if(!KebabGpuCcGmuClockQuiet(r->before[0][5]))return 95;
 if((r->before[0][2]&KebabGpuCcControlMask(2))!=b->gdscr_control || (r->before[0][4]&KebabGpuCcControlMask(4))!=b->ahb_control ||
 (r->before[0][5]&KebabGpuCcControlMask(5))!=(b->original_gmu&KebabGpuCcControlMask(5)))return 11;
 e=KebabGpuCcUpdate(c,4,1,b->original_ahb&1,b->ahb_control);if(e)return e;
 b->ahb_control=(b->ahb_control&~1u)|(b->original_ahb&1);
 if((e=KebabGpuCcDelay(c,10)))return e;
 e=KebabGpuCcUpdate(c,2,1,b->original_gdscr&1,b->gdscr_control);if(e)return e;
 b->gdscr_control=(b->gdscr_control&~1u)|(b->original_gdscr&1);
 if(b->original_gdscr&1) {
  /* DT no-status-check-on-disable: write/readback request +1us+100us.
   * Other TZ/HYP votes may keep PWR_ON1; never invent a power-off ACK. */
  if((e=KebabGpuCcBarrier(c)) || (e=KebabGpuCcDelay(c,1)) || (e=KebabGpuCcDelay(c,100)))return e;
 }
 if((e=KebabGpuCcSnapshot(c,1)))return e;
 if((r->after[0][2]&KebabGpuCcControlMask(2))!=b->gdscr_control || (r->after[0][4]&KebabGpuCcControlMask(4))!=b->ahb_control ||
 (r->after[0][5]&KebabGpuCcControlMask(5))!=(b->original_gmu&KebabGpuCcControlMask(5)))return 11;
 r->collapse_and_ahb_restored=1;r->retention_added=b->retention_added;r->full_original_controls_restored=!b->retention_added;b->held=0;return 0;
}
#endif
