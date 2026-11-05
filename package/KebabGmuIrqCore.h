/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_IRQ_CORE_H
#define KEBAB_GMU_IRQ_CORE_H
#include <stdint.h>
#include <string.h>
/* Fixed OTA HFI2 only: FDT SPI304/305 -> GIC INTID336/337, level-high.
 * Space0 is retained GICD; space1 is the retained 0x03d6a000 GMU window.
 * Every callback is bounded and IRQ-safe. fast() never claims a worker lock.
 * This core neither consumes MSGQ0/OOB16..31 nor changes interrupt groups. */
enum {KGI_EMPTY=0,KGI_PREPARING=1,KGI_ARMED=2,KGI_MASKED=3,KGI_QUARANTINED=4};
enum {KGI_BAD=22,KGI_BUSY=16,KGI_CLOSED=95,KGI_IO=5,KGI_FOREIGN=13,KGI_FAULT=14,KGI_OVERFLOW=75};
enum {KGI_HFI_OWNED=0x8006,KGI_HFI_FATAL=0x8000,KGI_AO_OWNED=0x29,KGI_AO_KNOWN=0x3b,KGI_RING=8};
#define KGI_HFI_KNOWN UINT32_C(0xffff8007)
typedef struct {
 void *cookie;
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t);
 int (*write64)(void *,unsigned,uint32_t,uint64_t);
 int (*write8)(void *,unsigned,uint32_t,uint8_t);
 void (*barrier)(void *);
 int (*fast)(void *); /* exact token/epoch + retained parent/power check */
 uint64_t (*now)(void *);
} KebabGmuIrqIo;
typedef struct {uint32_t intid,hfi,ao,reason;uint64_t sequence,time;} KebabGmuIrqEvent;
typedef struct {
 uint32_t phase,reason,irq_busy,consumer_busy,pending_head,pending_tail,overflow;
 uint32_t owned,touched,reads,writes,dispatches,spurious,hfi_async,async_waiting,containment_ack,initial_mask_step,arm_status_valid;
 uint32_t last_hfi,last_ao,hfi_mask_other,ao_mask_other,initial_priority;
 uint32_t initial_route_low[2],initial_route_high[2],initial_config,initial_enable;
 uint64_t route,epoch,events;
 KebabGmuIrqEvent ring[KGI_RING];
} KebabGmuIrqState;
static int KGIR(const KebabGmuIrqIo *i,KebabGmuIrqState *s,unsigned r,uint32_t o,uint32_t *v)
{__atomic_add_fetch(&s->reads,1,__ATOMIC_RELAXED);return i->read32(i->cookie,r,o,v)?KGI_IO:0;}
static int KGIW(const KebabGmuIrqIo *i,KebabGmuIrqState *s,unsigned r,uint32_t o,uint32_t v)
{__atomic_store_n(&s->touched,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->writes,1,__ATOMIC_RELAXED);return i->write32(i->cookie,r,o,v)?KGI_IO:0;}
static int KGIW8(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t o,uint8_t v)
{__atomic_store_n(&s->touched,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->writes,1,__ATOMIC_RELAXED);return i->write8(i->cookie,0,o,v)?KGI_IO:0;}
static int KGIW64(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t o,uint64_t v)
{__atomic_store_n(&s->touched,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->writes,1,__ATOMIC_RELAXED);return i->write64(i->cookie,0,o,v)?KGI_IO:0;}
#define KGI_TRY(x) do {int kgi_e=(x);if(kgi_e)return kgi_e;} while(0)
static int KGIPollRwp(const KebabGmuIrqIo *i,KebabGmuIrqState *s)
{uint32_t v;for(unsigned n=0;n<64;n++){KGI_TRY(KGIR(i,s,0,0,&v));if(!(v&0x80000000u))return 0;}return KGI_IO;}
static int KGIPrepareObserve(const KebabGmuIrqIo *i,KebabGmuIrqState *s)
{
 uint32_t reset,h,a;if(i->fast(i->cookie))return KGI_CLOSED;
 KGI_TRY(KGIR(i,s,1,0x14000,&reset));if(!(reset&1))return KGI_FOREIGN;
 KGI_TRY(KGIR(i,s,1,0x14648,&h));KGI_TRY(KGIR(i,s,1,0x24c14,&a));
 __atomic_store_n(&s->last_hfi,h,__ATOMIC_RELEASE);__atomic_store_n(&s->last_ao,a,__ATOMIC_RELEASE);
 __atomic_store_n(&s->arm_status_valid,1,__ATOMIC_RELEASE);
 return h || a?KGI_FAULT:0; /* Never clear inherited/unknown/fault pending. */
}
static int KGIInitialMasksBody(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t h,uint32_t a)
{
 uint32_t x,y;KGI_TRY(KGIPrepareObserve(i,s));
 KGI_TRY(KGIW(i,s,1,0x1464c,h|KGI_HFI_KNOWN));i->barrier(i->cookie);
 KGI_TRY(KGIPrepareObserve(i,s));
 KGI_TRY(KGIW(i,s,1,0x24c18,a|KGI_AO_KNOWN));i->barrier(i->cookie);
 KGI_TRY(KGIR(i,s,1,0x1464c,&x));KGI_TRY(KGIR(i,s,1,0x24c18,&y));
 if(x!=(h|KGI_HFI_KNOWN) || y!=(a|KGI_AO_KNOWN))return KGI_IO;
 /* After initial known-source masking, later writers own only 8006/29.
  * Unknown bits and MSGQ/OOB/RSCC/DBD mask floors are retained. */
 s->hfi_mask_other=x&~KGI_HFI_OWNED;s->ao_mask_other=y&~KGI_AO_OWNED;
 return KGIPrepareObserve(i,s);
}
static int KGIInitialMasks(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t h,uint32_t a)
{
 __atomic_store_n(&s->initial_mask_step,1,__ATOMIC_RELEASE);
 int e=KGIInitialMasksBody(i,s,h,a);__atomic_store_n(&s->initial_mask_step,0,__ATOMIC_RELEASE);return e;
}
static int KGIMasks(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t active)
{
 uint32_t h,a;KGI_TRY(KGIR(i,s,1,0x1464c,&h));KGI_TRY(KGIR(i,s,1,0x24c18,&a));
 if((h&~KGI_HFI_OWNED)!=s->hfi_mask_other || (a&~KGI_AO_OWNED)!=s->ao_mask_other)return KGI_FOREIGN;
 KGI_TRY(KGIW(i,s,1,0x1464c,(h|KGI_HFI_OWNED)&~active));
 KGI_TRY(KGIW(i,s,1,0x24c18,a&~KGI_AO_OWNED));i->barrier(i->cookie);
 KGI_TRY(KGIR(i,s,1,0x1464c,&h));KGI_TRY(KGIR(i,s,1,0x24c18,&a));
 return h==(s->hfi_mask_other|(KGI_HFI_OWNED&~active)) && a==s->ao_mask_other?0:KGI_IO;
}
static void KGIQuarantine(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t why,int powered)
{
 /* GIC W1C touches only our two bits. Never ACK active, clear unknown pending,
  * reset a producer, or withdraw table/data/map/parent pins here. */
 uint32_t zero=0;__atomic_compare_exchange_n(&s->reason,&zero,why,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
 __atomic_store_n(&s->phase,KGI_QUARANTINED,__ATOMIC_RELEASE);
 if(!__atomic_load_n(&s->owned,__ATOMIC_ACQUIRE))return;
 uint32_t v,ack=0;(void)KGIW(i,s,0,0x1a8,0x30000);i->barrier(i->cookie);
 if(!KGIR(i,s,0,0x128,&v) && !(v&0x30000))ack|=1;
 if(powered){if(!KGIR(i,s,1,0x1464c,&v))(void)KGIW(i,s,1,0x1464c,v|KGI_HFI_OWNED);
  if(!KGIR(i,s,1,0x24c18,&v))(void)KGIW(i,s,1,0x24c18,v|KGI_AO_OWNED);i->barrier(i->cookie);
  if(!KGIR(i,s,1,0x1464c,&v) && (v&KGI_HFI_OWNED)==KGI_HFI_OWNED)ack|=2;
  if(!KGIR(i,s,1,0x24c18,&v) && (v&KGI_AO_OWNED)==KGI_AO_OWNED)ack|=4;}
 __atomic_store_n(&s->containment_ack,ack,__ATOMIC_RELEASE);
}
static int KGIPush(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t id,uint32_t h,uint32_t a,uint32_t why)
{
 uint32_t head=__atomic_load_n(&s->pending_head,__ATOMIC_RELAXED),tail=__atomic_load_n(&s->pending_tail,__ATOMIC_ACQUIRE);
 if((uint32_t)(head-tail)>=KGI_RING){__atomic_add_fetch(&s->overflow,1,__ATOMIC_RELAXED);return KGI_OVERFLOW;}
 KebabGmuIrqEvent *e=&s->ring[head&(KGI_RING-1)];e->intid=id;e->hfi=h;e->ao=a;e->reason=why;e->sequence=++s->events;e->time=i->now(i->cookie);
 __atomic_store_n(&s->pending_head,head+1,__ATOMIC_RELEASE);return 0;
}
static int KebabGmuIrqArm(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint64_t route,uint64_t epoch)
{
 if(!i || !s || !i->read32 || !i->write32 || !i->write64 || !i->write8 || !i->barrier || !i->fast || !i->now || !epoch || (route&~UINT64_C(0xff00ffffff)))return KGI_BAD;
 if(__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE))return KGI_BUSY;
 if(i->fast(i->cookie))return KGI_CLOSED;
 uint32_t ctl,ty,en,pend,act,cf,pri,h,a,hm,am,lo[2],hi[2];
 KGI_TRY(KGIR(i,s,0,0,&ctl));KGI_TRY(KGIR(i,s,0,4,&ty));
 /* NS GICD view already initialized by PE_init_interrupts. No CTLR write. */
 if((ctl&0x80000012u)!=0x12u || 32*((ty&31)+1)<=337)return KGI_CLOSED;
 KGI_TRY(KGIR(i,s,0,0x128,&en));KGI_TRY(KGIR(i,s,0,0x228,&pend));KGI_TRY(KGIR(i,s,0,0x328,&act));
 KGI_TRY(KGIR(i,s,0,0xc54,&cf));KGI_TRY(KGIR(i,s,0,0x550,&pri));
 /* Secure priority fields are RAZ to NS (IHI0069F 4.8.1). Nonzero NS
  * priority is positive evidence; zero is ambiguous and gets zero writes.
  * IGROUPR/IGRPMODR are Secure-only/RAZ, so never infer group from zero. */
 if((en|pend|act)&0x30000u || (cf&0xau) || !(pri&0xff) || !(pri&0xff00))return KGI_FOREIGN;
 for(unsigned n=0;n<2;n++){KGI_TRY(KGIR(i,s,0,0x6a80+8*n,&lo[n]));KGI_TRY(KGIR(i,s,0,0x6a84+8*n,&hi[n]));}
 KGI_TRY(KGIPrepareObserve(i,s));h=__atomic_load_n(&s->last_hfi,__ATOMIC_ACQUIRE);a=__atomic_load_n(&s->last_ao,__ATOMIC_ACQUIRE);
 KGI_TRY(KGIR(i,s,1,0x1464c,&hm));KGI_TRY(KGIR(i,s,1,0x24c18,&am));
 /* Actual579 resets these masks to zero. Under real RESET_HELD and empty
  * pending, initialize known sources rather than assuming firmware masked.
  * The Native adapter allows this wider set-only step only in normal worker. */
 if(h || a)return KGI_FOREIGN;
 if(i->fast(i->cookie))return KGI_CLOSED;
 s->epoch=epoch;s->route=route;s->initial_priority=pri;s->initial_config=cf;s->initial_enable=en;
 for(unsigned n=0;n<2;n++){s->initial_route_low[n]=lo[n];s->initial_route_high[n]=hi[n];}
 s->hfi_mask_other=hm&~KGI_HFI_OWNED;s->ao_mask_other=am&~KGI_AO_OWNED;
 __atomic_store_n(&s->owned,1,__ATOMIC_RELEASE);__atomic_store_n(&s->phase,KGI_PREPARING,__ATOMIC_RELEASE);
 int e=KGIInitialMasks(i,s,hm,am);if(!e)e=KGIPrepareObserve(i,s);
 for(unsigned n=0;n<2 && !e;n++){e=KGIW64(i,s,0x6a80+8*n,route);if(!e)e=KGIW8(i,s,0x550+n,0xa0);}
 i->barrier(i->cookie);
 for(unsigned n=0;n<2 && !e;n++){e=KGIR(i,s,0,0x6a80+8*n,&lo[n]);if(!e)e=KGIR(i,s,0,0x6a84+8*n,&hi[n]);
  if(!e && ((lo[n]!=(uint32_t)route) || (hi[n]!=(uint32_t)(route>>32))))e=KGI_IO;}
 if(!e)e=KGIR(i,s,0,0x550,&pri);if(!e && (pri&0xffff)!=0xa0a0)e=KGI_IO;
 if(!e)e=KGIPrepareObserve(i,s);if(!e)e=KGIMasks(i,s,KGI_HFI_FATAL);if(!e)e=KGIPrepareObserve(i,s);
 if(e){KGIQuarantine(i,s,(uint32_t)e,i->fast(i->cookie)==0);return e;}
 __atomic_store_n(&s->phase,KGI_ARMED,__ATOMIC_RELEASE);
 e=KGIW(i,s,0,0x128,0x30000);i->barrier(i->cookie);if(!e)e=KGIPollRwp(i,s);
 if(!e)e=KGIR(i,s,0,0x128,&en);if(!e && (en&0x30000)!=0x30000)e=KGI_IO;
 if(!e && __atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)!=KGI_ARMED)e=KGI_FAULT;
 if(e)KGIQuarantine(i,s,(uint32_t)e,i->fast(i->cookie)==0);return e;
}
/* Handler is bounded, no allocation, worker calls, SCM, GIC EOI or busy-spin.
 * The outer sleh owns architectural IAR/EOI exactly as before. */
static int KebabGmuIrqHandle(const KebabGmuIrqIo *i,KebabGmuIrqState *s,uint32_t id)
{
 if(!i || !s || (id!=336 && id!=337) || !__atomic_load_n(&s->owned,__ATOMIC_ACQUIRE))return 0;
 __atomic_add_fetch(&s->dispatches,1,__ATOMIC_RELAXED);
 if(__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)!=KGI_ARMED){
  /* A failed disable is never called acknowledged. One finite owned-GIC
   * retry on a later physical delivery; no device access or busy-spin. */
  if(!(__atomic_load_n(&s->containment_ack,__ATOMIC_ACQUIRE)&1)){uint32_t v;
   (void)KGIW(i,s,0,0x1a8,0x30000);i->barrier(i->cookie);
   if(!KGIR(i,s,0,0x128,&v) && !(v&0x30000))__atomic_fetch_or(&s->containment_ack,1,__ATOMIC_RELEASE);}
  return 1;}
 if(__atomic_exchange_n(&s->irq_busy,1,__ATOMIC_ACQ_REL)){KGIQuarantine(i,s,KGI_BUSY,0);return 1;}
 uint32_t h=0,a=0,hm=0,am=0;int e=0,powered=i->fast(i->cookie)==0;
 if(!powered)e=KGI_CLOSED;
 if(!e)e=KGIR(i,s,1,0x14648,&h);if(!e)e=KGIR(i,s,1,0x24c14,&a);
 __atomic_store_n(&s->last_hfi,h,__ATOMIC_RELEASE);__atomic_store_n(&s->last_ao,a,__ATOMIC_RELEASE);
 if(!e)e=KGIR(i,s,1,0x1464c,&hm);if(!e)e=KGIR(i,s,1,0x24c18,&am);
 uint32_t active=KGI_HFI_FATAL|(__atomic_load_n(&s->hfi_async,__ATOMIC_ACQUIRE)?6u:0u);
 if(!e && ((hm&~KGI_HFI_OWNED)!=s->hfi_mask_other || (am&~KGI_AO_OWNED)!=s->ao_mask_other ||
  (hm&KGI_HFI_OWNED)!=(KGI_HFI_OWNED&~active) || (am&KGI_AO_OWNED)))e=KGI_FOREIGN;
 if(!e && ((h&~0xffff8007u) || (a&~KGI_AO_KNOWN) || (h&KGI_HFI_FATAL) || (a&KGI_AO_OWNED)))e=KGI_FAULT;
 /* Only observed owned bits are W1C. MSGQ0, all OOB and unowned AO stay. */
 if(powered){if(h&active){int x=KGIW(i,s,1,0x14644,h&active);if(!e)e=x;}
  if(a&KGI_AO_OWNED){int x=KGIW(i,s,1,0x24c10,a&KGI_AO_OWNED);if(!e)e=x;}i->barrier(i->cookie);}
 if(!e && (h&active&6u)){
  /* A bounded deferred mailbox owns the wakeup; async bits remain masked
   * until a real transport consumer is separately integrated. No fake ACK. */
  e=KGIW(i,s,1,0x1464c,hm|6u);i->barrier(i->cookie);__atomic_store_n(&s->async_waiting,1,__ATOMIC_RELEASE);
  __atomic_store_n(&s->hfi_async,0,__ATOMIC_RELEASE);
 }
 if(!h && !a)__atomic_add_fetch(&s->spurious,1,__ATOMIC_RELAXED);
 int queue=KGIPush(i,s,id,h,a,(uint32_t)e);if(!e)e=queue;
 if(e)KGIQuarantine(i,s,(uint32_t)e,powered);
 __atomic_store_n(&s->irq_busy,0,__ATOMIC_RELEASE);return 1;
}
static int KebabGmuIrqTake(KebabGmuIrqState *s,KebabGmuIrqEvent *out)
{
 if(!s || !out)return KGI_BAD;if(__atomic_exchange_n(&s->consumer_busy,1,__ATOMIC_ACQ_REL))return KGI_BUSY;
 uint32_t t=__atomic_load_n(&s->pending_tail,__ATOMIC_RELAXED),h=__atomic_load_n(&s->pending_head,__ATOMIC_ACQUIRE);int e=KGI_BUSY;
 if(t!=h){*out=s->ring[t&(KGI_RING-1)];__atomic_store_n(&s->pending_tail,t+1,__ATOMIC_RELEASE);e=0;}
 __atomic_store_n(&s->consumer_busy,0,__ATOMIC_RELEASE);return e;
}
static int KGIMaskBody(const KebabGmuIrqIo *i,KebabGmuIrqState *s)
{
 if(!s || !__atomic_load_n(&s->owned,__ATOMIC_ACQUIRE))return KGI_CLOSED;
 if(__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)!=KGI_ARMED)return KGI_CLOSED;
 __atomic_store_n(&s->phase,KGI_MASKED,__ATOMIC_RELEASE);
 KGI_TRY(KGIW(i,s,0,0x1a8,0x30000));i->barrier(i->cookie);KGI_TRY(KGIPollRwp(i,s));
 uint32_t en;KGI_TRY(KGIR(i,s,0,0x128,&en));if(en&0x30000){KGIQuarantine(i,s,KGI_IO,i->fast(i->cookie)==0);return KGI_IO;}
 if(__atomic_load_n(&s->irq_busy,__ATOMIC_ACQUIRE))return KGI_BUSY;
 if(i->fast(i->cookie))return KGI_CLOSED;
 uint32_t h,a;KGI_TRY(KGIR(i,s,1,0x1464c,&h));KGI_TRY(KGIR(i,s,1,0x24c18,&a));
 KGI_TRY(KGIW(i,s,1,0x1464c,h|KGI_HFI_OWNED));KGI_TRY(KGIW(i,s,1,0x24c18,a|KGI_AO_OWNED));i->barrier(i->cookie);
 uint32_t x,y;KGI_TRY(KGIR(i,s,1,0x1464c,&x));KGI_TRY(KGIR(i,s,1,0x24c18,&y));
 if(x!=(h|KGI_HFI_OWNED) || y!=(a|KGI_AO_OWNED)){KGIQuarantine(i,s,KGI_IO,1);return KGI_IO;}
 __atomic_store_n(&s->containment_ack,7,__ATOMIC_RELEASE);return 0;
}
static int KebabGmuIrqMask(const KebabGmuIrqIo *i,KebabGmuIrqState *s)
{
 if(!i || !s)return KGI_BAD;
 int e=KGIMaskBody(i,s);if(e && s && __atomic_load_n(&s->owned,__ATOMIC_ACQUIRE) &&
  __atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)!=KGI_QUARANTINED)
  KGIQuarantine(i,s,(uint32_t)e,!__atomic_load_n(&s->irq_busy,__ATOMIC_ACQUIRE) && i->fast(i->cookie)==0);
 return e;
}
#undef KGI_TRY
#endif
