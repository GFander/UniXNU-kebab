/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_SECURE_EXIT_CORE_H
#define KEBAB_A650_SECURE_EXIT_CORE_H
#if defined(KERNEL)
#include <kern/KebabA650CpCore.h>
#else
#include "KebabA650CpCore.h"
#endif
/* CP_SET_SECURE_MODE(0), followed by the already qualified CP fence packet.
 * The completion goes to a distinct owned NC slot. The original global
 * fence stays2, so the original0605 render command words stay identical. */
enum {KZE_WORDS=9,KZE_START=KCP_FINAL_WPTR,KZE_END=KZE_START+KZE_WORDS,
 KZE_OFFSET=0x500,KZE_SEQUENCE=3,KZE_SENTINEL=0x13579bdf,
 KZE_DEADLINE_US=200000,KZE_POLL_LIMIT=2000};
enum {KZE_EMPTY,KZE_AUTHORIZED,KZE_INITIAL_QUIET,KZE_PREPARING,KZE_PREPARED,
 KZE_SUBMITTING,KZE_WAITING,KZE_COMPLETE};
struct KZEAuthProof {
 uint64_t epoch,gmu_generation,gpu_generation,cp_generation;
 uint32_t auth_completed,metadata_frozen,image_frozen,CP_baseline_valid;
};
struct KZESample {
 uint32_t wptr,rptr,shadow,fence,secure_fence,marker,status,irq,irq_mask;
 uint32_t cp_fault,cp_irq,protect,ahb_fence,gfsr,fsr,sqe;
};
struct KZERecord {
 uint32_t phase,result,primary_error,io_error,touched,prepared,submitted,completed;
 uint32_t gates,polls,samples,reserved;
 uint64_t elapsed_us;
 KZEAuthProof parent;
 uint32_t words[KZE_WORDS];
 KZESample initial,terminal;
};
struct KZEOps {
 void *cookie;
 uint64_t (*now_us)(void *);
 int (*gate)(void *);
 int (*authority)(void *,KZEAuthProof *);
 int (*sample)(void *,KZESample *);
 int (*prepare)(void *,const uint32_t *,uint32_t);
 int (*publish)(void *,uint32_t);
 int (*complete_guard)(void *);
 void (*delay_us)(void *,unsigned);
};
static inline void KZEWords(uint32_t out[KZE_WORDS])
{
 out[0]=KCPType7(0x66,1);out[1]=0;
 KCPFenceWords(out+2,KZE_SEQUENCE);
 out[6]=KCP_FENCE_IOVA+KZE_OFFSET;
}
static inline int KZEAuthValid(const KZEAuthProof *p)
{
 return !p || p->epoch!=1 || !p->gmu_generation || !p->gpu_generation || !p->cp_generation ||
  p->auth_completed!=1 || p->metadata_frozen!=1 || p->image_frozen!=1 || p->CP_baseline_valid!=1?95:0;
}
static inline int KZEFault(const KZESample *s)
{
 const uint32_t irq_faults=(1u<<1)|(1u<<7)|(1u<<9)|(1u<<23)|(1u<<24)|(1u<<25);
 return s->irq_mask || (s->irq&irq_faults) || s->cp_fault || (s->cp_irq&0xf7) ||
  s->protect || (s->ahb_fence&1) || s->gfsr || (s->fsr&0xc00001feu) ||
  s->sqe!=1 || s->marker!=KCP_MARKER?5:0;
}
static inline int KZEBudget(const KZEOps *o,uint64_t start,KZERecord *r)
{
 uint64_t n=o->now_us(o->cookie);r->elapsed_us=n>=start?n-start:UINT64_MAX;
 return n<start || n-start>=KZE_DEADLINE_US?110:0;
}
static inline int KZEGate(const KZEOps *o,uint64_t start,KZERecord *r)
{
 int e=KZEBudget(o,start,r);if(e)return e;++r->gates;
 e=o->gate(o->cookie);if(e){r->io_error=e;return e;}
 return KZEBudget(o,start,r);
}
static inline int KZERun(const KZEOps *o,KZERecord *r)
{
 if(!r)return 22;memset(r,0,sizeof(*r));
 if(!o || !o->now_us || !o->gate || !o->authority || !o->sample || !o->prepare ||
  !o->publish || !o->complete_guard || !o->delay_us){r->result=r->primary_error=22;return 22;}
 const uint64_t start=o->now_us(o->cookie);int e=o->authority(o->cookie,&r->parent);
 if(!e)e=KZEAuthValid(&r->parent);if(e)goto done;r->phase=KZE_AUTHORIZED;
 e=KZEGate(o,start,r);if(e)goto done;
 ++r->samples;e=o->sample(o->cookie,&r->initial);if(e)goto done;
 e=KZEFault(&r->initial);if(e)goto done;
 if(r->initial.wptr!=KZE_START || r->initial.rptr!=KZE_START || r->initial.shadow!=KZE_START ||
  r->initial.fence!=2 || (r->initial.status&~1u)){e=5;goto done;}
 r->phase=KZE_INITIAL_QUIET;KZEWords(r->words);
 e=KZEGate(o,start,r);if(e)goto done;
 r->phase=KZE_PREPARING;r->touched=1;e=o->prepare(o->cookie,r->words,KZE_WORDS);if(e)goto done;
 r->prepared=1;r->phase=KZE_PREPARED;e=KZEGate(o,start,r);if(e)goto done;
 r->phase=KZE_SUBMITTING;r->submitted=1;e=o->publish(o->cookie,KZE_END);if(e)goto done;
 r->phase=KZE_WAITING;
 for(unsigned i=0;i<KZE_POLL_LIMIT;i++){
  e=KZEGate(o,start,r);if(e)goto done;++r->polls;++r->samples;
  e=o->sample(o->cookie,&r->terminal);if(e)goto done;
  e=KZEFault(&r->terminal);if(e)goto done;
  {const auto *s=&r->terminal;
   if(s->wptr!=KZE_END || s->rptr<KZE_START || s->rptr>KZE_END || s->shadow<KZE_START || s->shadow>KZE_END ||
    s->fence!=2 || (s->secure_fence!=KZE_SENTINEL && s->secure_fence!=KZE_SEQUENCE)){e=5;goto done;}
   if(s->rptr==KZE_END && s->shadow==KZE_END && s->secure_fence==KZE_SEQUENCE && !(s->status&~1u)){
    e=o->complete_guard(o->cookie);if(e)goto done;
    e=KZEGate(o,start,r);if(e)goto done;r->completed=1;r->phase=KZE_COMPLETE;goto done;
   }
  }
  o->delay_us(o->cookie,100);
 }
 e=110;
done:
 r->primary_error=e;if(!e)e=KZEBudget(o,start,r);r->result=e;
 if(e)r->completed=0;
 return e;
}
#endif
