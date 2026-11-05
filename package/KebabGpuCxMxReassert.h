/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_CXMX_REASSERT_H
#define KEBAB_GPU_CXMX_REASSERT_H
#include "KebabRscOwnershipQuery.h"
/* Strict SM8250 boot-floor reassertion, not a general RPMh sender.
 * All offsets are relative to APPS DRV2, never CommandDB resource MMIO.
 * Caller holds the existing native RPMh global busy across the whole run.
 * Binding is compiled, reviewed evidence; never accept it from user input. */
enum {KebabCxMxMx=1,KebabCxMxCx=2,KebabCxMxPollLimit=10000,KebabCxMxReadbackLimit=1000};
typedef struct {
 uint32_t schema,ready,ownership_audit_bound,polling_only,expected_used_mask;
 uint32_t expected[KebabRscQueryWords];
 uint8_t query_receipt_sha256[32],native_owner_audit_sha256[32];
} KebabGpuCxMxBinding;
typedef struct {
 void *cookie;
 int (*read32)(void *,uint32_t,uint32_t *);
 int (*write32)(void *,uint32_t,uint32_t);
 int (*barrier)(void *);
 uint64_t (*now_us)(void *);
} KebabGpuCxMxIo;
typedef struct {uint32_t attempted,complete,refs,ack_mask,uncertain_mask,quarantined;} KebabGpuCxMxLedger;
typedef struct {
 uint32_t magic,schema,bytes,result,same_native_owner,owner_dead,owner_used_mask;
 uint32_t refs_before,refs_after,ack_mask,uncertain_mask,lease_mask,quarantined;
 uint32_t read_attempts,write_attempts,failed_offset,failed_op,io_error;
 uint32_t last_command_status,last_irq_status,last_rsc_status,last_write_offset,last_write_value;
 uint32_t failure_result,evidence_replayed,last_valid;
 uint32_t poll_count[2],command_status[2],irq_status[2],response[2],gate_reason;
 KebabRscQueryRecord before,after;
} KebabGpuCxMxRecord;
typedef struct {const KebabGpuCxMxIo *io;KebabGpuCxMxRecord *record;KebabGpuCxMxLedger *ledger;} KebabGpuCxMxContext;
enum {KebabCxMxIrqEnable=0xd00,KebabCxMxIrqStatus=0xd04,KebabCxMxIrqClear=0xd08,
 KebabCxMxWait=0xfb0,KebabCxMxControl=0xfb4,KebabCxMxStatus=0xfb8,KebabCxMxEnable=0xfbc,
 KebabCxMxMsg=0xfd0,KebabCxMxAddress=0xfd4,KebabCxMxData=0xfd8,KebabCxMxCommandStatus=0xfdc};
static inline int KebabGpuCxMxWriteAllowed(uint32_t offset)
{
 return offset==KebabCxMxIrqClear || offset==KebabCxMxWait ||
 offset==KebabCxMxControl || offset==KebabCxMxEnable || offset==KebabCxMxMsg ||
 offset==KebabCxMxAddress || offset==KebabCxMxData;
}
static inline void KebabGpuCxMxInitRecord(KebabGpuCxMxRecord *r)
{memset(r,0,sizeof *r);r->magic=0x4d584347;r->schema=1;r->bytes=sizeof *r;r->result=22;r->failed_offset=UINT32_MAX;r->last_write_offset=UINT32_MAX;}
static inline int KebabGpuCxMxBound(const KebabGpuCxMxBinding *b)
{
 unsigned i;uint8_t q=0,a=0;if(!b || b->schema!=1 || b->ready!=1 || b->ownership_audit_bound!=1 || b->polling_only!=1 || b->expected_used_mask!=5)return 0;
 for(i=0;i<32;i++){q|=b->query_receipt_sha256[i];a|=b->native_owner_audit_sha256[i];}return q && a;
}
static inline int KebabGpuCxMxRead(void *cookie,uint32_t offset,uint32_t *out)
{
 KebabGpuCxMxContext *c=(KebabGpuCxMxContext *)cookie;int e;
 if(!KebabRscQueryAllowed(offset) || !out)return 22;
 ++c->record->read_attempts;e=c->io->read32(c->io->cookie,offset,out);
 if(!e) {
  if(offset==KebabCxMxCommandStatus){c->record->last_command_status=*out;c->record->last_valid|=1;}
  if(offset==KebabCxMxIrqStatus){c->record->last_irq_status=*out;c->record->last_valid|=2;}
  if(offset==KebabCxMxStatus){c->record->last_rsc_status=*out;c->record->last_valid|=4;}
 }
 if(e){c->record->failed_offset=offset;c->record->failed_op=1;c->record->io_error=(uint32_t)e;}return e;
}
static inline int KebabGpuCxMxBarrier(void *cookie)
{
 KebabGpuCxMxContext *c=(KebabGpuCxMxContext *)cookie;int e=c->io->barrier(c->io->cookie);
 if(e){c->record->failed_op=3;c->record->io_error=(uint32_t)e;}return e;
}
static inline int KebabGpuCxMxWrite(KebabGpuCxMxContext *c,uint32_t offset,uint32_t value)
{
 int e;if(!KebabGpuCxMxWriteAllowed(offset))return 22;
 c->ledger->attempted=1;++c->record->write_attempts;
 c->record->last_write_offset=offset;c->record->last_write_value=value;c->record->last_valid|=8;
 e=c->io->write32(c->io->cookie,offset,value);
 if(e){c->record->failed_offset=offset;c->record->failed_op=2;c->record->io_error=(uint32_t)e;return 5;}
 return KebabGpuCxMxBarrier(c)?5:0;
}
static inline int KebabGpuCxMxSync(KebabGpuCxMxContext *c,uint32_t offset,uint32_t value)
{
 unsigned i;uint32_t got;uint64_t start=c->io->now_us(c->io->cookie);
 if(KebabGpuCxMxWrite(c,offset,value))return 5;
 for(i=0;i<KebabCxMxReadbackLimit;i++) {
  if(KebabGpuCxMxRead(c,offset,&got))return 5;
  if(got==value)return 0;
  if(c->io->now_us(c->io->cookie)-start>=10000)break;
 }
 c->record->failed_offset=offset;c->record->failed_op=5;return 110;
}
/* Fixed reviewed boot shape. Exact 676-word binding adds the remaining
 * register/configuration checks; absence of an enabled slot is not ownership. */
static inline int KebabGpuCxMxBaseline(const KebabRscQueryRecord *q,uint32_t used)
{
 unsigned t,s;const uint32_t *p;
 if(q->result || q->valid_words[0]!=676 || q->valid_words[1]!=676 || q->stable_words!=676 ||
 q->num_tcs!=8 || q->num_commands!=16 || q->irq_status || (q->irq_enable&2) || used!=5)return 0;
 p=&q->sample[0][4];
 if(p[0] || p[1]!=0x1010000 || p[2]!=1 || p[3]!=3)return 0;
 for(s=0;s<2;s++)if(p[4+s*5]!=0x10108 || p[5+s*5]!=(s?0x30010u:0x30000u) || p[6+s*5]!=5 || p[7+s*5]!=0x10101)return 0;
 for(t=0;t<8;t++) {
  p=&q->sample[0][4+t*84];
  if(t && (p[0] || p[2]!=1 || p[3] || (p[1]!=0 && p[1]!=0x10000)))return 0;
  for(s=0;s<16;s++) {
   uint32_t address=p[5+s*5],status=p[7+s*5];
   /* No GFX or unidentified ARC residue anywhere, even disabled. */
   if(((address>>16)&15)==3 && !(t==0 && s<2))return 0;
   if(t==0 && s<2)continue;
   if(t==1 && s==0) {
    if(p[4]!=0x10108 || address!=0x42f04 || p[6]!=1 || (status!=0 && status!=0x10101))return 0;
   } else if(status)return 0;
  }
 }
 return 1;
}
static inline int KebabGpuCxMxFresh(KebabGpuCxMxContext *c)
{
 uint32_t status,irq,idle;
 if(KebabGpuCxMxRead(c,KebabCxMxCommandStatus,&status) || KebabGpuCxMxRead(c,KebabCxMxIrqStatus,&irq) || KebabGpuCxMxRead(c,KebabCxMxStatus,&idle))return 5;
 return status==0 && irq==0 && idle==1?0:95;
}
static inline int KebabGpuCxMxClearOwnIrq(KebabGpuCxMxContext *c)
{
 uint32_t irq;unsigned i;uint64_t start=c->io->now_us(c->io->cookie);
 if(KebabGpuCxMxRead(c,KebabCxMxIrqStatus,&irq))return 5;
 if(irq&~2u)return 95;if(!(irq&2))return 0;
 /* Baseline IRQ_STATUS was zero, bit1 belongs to this fresh trigger only.
  * IRQ_ENABLE stays unchanged/off for bit1; no interrupt delivery is used. */
 if(KebabGpuCxMxWrite(c,KebabCxMxIrqClear,2))return 5;
 for(i=0;i<KebabCxMxReadbackLimit;i++) {
  if(KebabGpuCxMxRead(c,KebabCxMxIrqStatus,&irq))return 5;
  if(irq&~2u)return 95;if(!(irq&2))return 0;
  if(c->io->now_us(c->io->cookie)-start>=10000)break;
 }
 return 110;
}
static inline int KebabGpuCxMxSendOne(KebabGpuCxMxContext *c,unsigned command,uint32_t address)
{
 uint32_t status=0,irq=0,idle=0,response=0,bit=1u<<command;unsigned i;uint64_t start;int result;
 if(KebabGpuCxMxSync(c,KebabCxMxControl,0) || KebabGpuCxMxSync(c,KebabCxMxEnable,0) || KebabGpuCxMxSync(c,KebabCxMxWait,0))return 5;
 if((result=KebabGpuCxMxFresh(c))!=0)return result;
 if(KebabGpuCxMxSync(c,KebabCxMxMsg,0x10108) || KebabGpuCxMxSync(c,KebabCxMxAddress,address) || KebabGpuCxMxSync(c,KebabCxMxData,5) ||
 KebabGpuCxMxSync(c,KebabCxMxWait,1) || KebabGpuCxMxSync(c,KebabCxMxEnable,1) || KebabGpuCxMxSync(c,KebabCxMxControl,0x10000))return 5;
 if((result=KebabGpuCxMxFresh(c))!=0)return result;
 /* Set before attempting trigger: an error return cannot prove no acceptance. */
 c->ledger->uncertain_mask|=bit;
 if(KebabGpuCxMxWrite(c,KebabCxMxControl,0x1010000))return 5;
 start=c->io->now_us(c->io->cookie);
 for(i=0;i<KebabCxMxPollLimit;i++) {
  ++c->record->poll_count[command];
  if(KebabGpuCxMxRead(c,KebabCxMxCommandStatus,&status) || KebabGpuCxMxRead(c,KebabCxMxIrqStatus,&irq) || KebabGpuCxMxRead(c,KebabCxMxStatus,&idle) || KebabGpuCxMxRead(c,0xfe0,&response))return 5;
  c->record->command_status[command]=status;c->record->irq_status[command]=irq;c->record->response[command]=response;
  if((irq&~2u) || (status&~0x10101u))return 95;
  if((status&0x10100)==0x10100 && idle==1)break;
  if(c->io->now_us(c->io->cookie)-start>=100000)break;
 }
 if((status&0x10100)!=0x10100 || idle!=1)return 110;
 c->ledger->ack_mask|=bit;c->ledger->uncertain_mask&=~bit;
 /* A real ACK immediately installs an owner-held protection reference.
  * Partial failure keeps it; only complete pair+restore grants lease_mask3. */
 if(!c->ledger->refs)c->ledger->refs=1;
 if(KebabGpuCxMxSync(c,KebabCxMxControl,0) || KebabGpuCxMxSync(c,KebabCxMxEnable,0) || KebabGpuCxMxSync(c,KebabCxMxWait,0))return 5;
 return KebabGpuCxMxClearOwnIrq(c);
}
static inline int KebabGpuCxMxRun(const KebabGpuCxMxIo *io,const KebabGpuCxMxBinding *binding,
 uint32_t owner_used,uint32_t used_mask,KebabGpuCxMxLedger *ledger,KebabGpuCxMxRecord *r)
{
 KebabGpuCxMxContext c;KebabRscQueryIo query;unsigned i;int result=22;
 if(!r)return 22;KebabGpuCxMxInitRecord(r);
 if(!io || !io->read32 || !io->write32 || !io->barrier || !io->now_us || !ledger)return 22;
 if(!KebabGpuCxMxBound(binding)){r->gate_reason=1;r->result=95;return 95;}
 if(ledger->quarantined || ledger->attempted){r->gate_reason=2;r->result=5;return 5;}
 c.io=io;c.record=r;c.ledger=ledger;query.cookie=&c;query.read32=KebabGpuCxMxRead;query.barrier=KebabGpuCxMxBarrier;
 result=KebabRscQuery(&query,&r->before);if(result) {r->gate_reason=4;goto done;}
 if(owner_used!=1 || used_mask!=binding->expected_used_mask || !KebabGpuCxMxBaseline(&r->before,used_mask)) {r->gate_reason=5;result=95;goto done;}
 if(memcmp(binding->expected,r->before.sample[0],sizeof binding->expected)) {r->gate_reason=6;result=11;goto done;}
 /* IRQ_ENABLE is never written. IRQ completion is not required: fresh
  * WRITE_ACK ISSUED+COMPL and RSC idle prove the polled transaction. */
 result=KebabGpuCxMxSendOne(&c,0,0x30010);if(result)goto done;
 result=KebabGpuCxMxSendOne(&c,1,0x30000);if(result)goto done;
 /* Only slot0's three configuration words changed. All other 15 slots
  * stay untouched and are verified against the complete original snapshot. */
 for(i=0;i<3;i++)if((result=KebabGpuCxMxSync(&c,KebabCxMxMsg+i*4,r->before.sample[0][4+84+4+i]))!=0)goto done;
 if((result=KebabGpuCxMxSync(&c,KebabCxMxControl,r->before.sample[0][4+84+1]))!=0)goto done;
 result=KebabRscQuery(&query,&r->after);if(result)goto done;
 for(i=0;i<676;i++) {
  /* Read-only own slot STATUS/RESP cannot be restored. Completion zero
   * before the next command is mandatory; no foreign word may change. */
  if(i==4+84+7 || i==4+84+8)continue;
  if(r->before.sample[0][i]!=r->after.sample[0][i]){result=11;goto done;}
 }
 if(r->after.sample[0][4+84+7]!=0 || ledger->ack_mask!=3 || ledger->uncertain_mask){result=5;goto done;}
 ledger->complete=1;ledger->refs=1;result=0;
done:
 if(result && ledger->attempted)ledger->quarantined=1;
 if(result==5 && r->gate_reason==4)ledger->quarantined=1;
 if(result && ledger->quarantined)r->failure_result=(uint32_t)result;
 r->result=(uint32_t)result;r->refs_after=ledger->refs;r->ack_mask=ledger->ack_mask;r->uncertain_mask=ledger->uncertain_mask;
 r->quarantined=ledger->quarantined;r->lease_mask=ledger->complete && ledger->refs?3:0;return result;
}
#endif
