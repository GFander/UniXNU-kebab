/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_ZAP_AUTH_CORE_H
#define KEBAB_A650_ZAP_AUTH_CORE_H
#include "KebabA650ZapCore.h"
/* One normal worker and one SCM token span the entire lifecycle. Memory
 * preparation ends before the original graphics VM initializer is called.
 * Callbacks must bind actual canonical owners; host tests grant no permission.
 * Cooperative budget checks cannot recover a nonreturning SMC. */
enum {KZA2_ARGUMENT=22,KZA2_UNQUALIFIED=95,KZA2_IO=5,KZA2_UNRESOLVED=35,
      KZA2_PERIPHERAL=71,KZA2_DEADLINE=110};
enum {KZA2_EMPTY,KZA2_LEASED,KZA2_INIT_ATTEMPT,KZA2_INIT_RETURNED,
      KZA2_MEM_ATTEMPT,KZA2_MEM_RETURNED,KZA2_LOADED,KZA2_CP_READY,
      KZA2_AUTH_ATTEMPT,KZA2_AUTH_RETURNED,KZA2_FINAL,KZA2_DONE};
struct KZA2Proof {
 uint64_t image,metadata,image_capacity,metadata_capacity;
 uint64_t epoch,gmu_generation,gpu_generation,cp_generation;
 uint32_t ranges,pairs,pages,quiet_samples,address_bits,flags;
};
struct KZA2Operation {
 KebabPasResult raw;
 uint64_t elapsed_us;
 uint32_t guard,attempted;
};
struct KZA2Receipt {
 uint32_t phase,result,primary_error,end_error;
 uint32_t transaction_started,metadata_frozen,image_frozen,prepared;
 uint32_t cp_ready,authenticated,final_checked,completed;
 uint64_t elapsed_us;
 KZA2Proof before,after;
 KZA2Operation operation[3];
};
struct KZA2Ops {
 void *cookie;
 uint64_t (*now_us)(void *);
 int (*preflight)(void *,KZA2Proof *);
 int (*transaction_begin)(void *);
 int (*fresh_prevm)(void *,const KZA2Proof *);
 int (*invoke)(void *,unsigned,const KebabPasRegs *,KebabPasResult *,uint32_t *);
 int (*load_image_after_setup)(void *,const KZA2Proof *);
 /* Seal low memory and release only the old temporary namespace lease.
  * Reuse the existing VM/OOB/CP initializers with the SAME SCM token held.
  * Never load firmware or touch protected low memory in this callback. */
 int (*original_gpu_chain)(void *,const KZA2Proof *,KZA2Proof *);
 int (*final_cp_check)(void *,const KZA2Proof *);
 int (*transaction_end)(void *);
};
static inline int KZA2ProofValid(const KZA2Proof *p,int after)
{
 if(!p || (after!=0 && after!=1) || p->epoch!=1 || !p->gmu_generation ||
    (after?(!p->gpu_generation || !p->cp_generation):(p->gpu_generation || p->cp_generation)) ||
    p->ranges!=(after?17u:10u) || p->pairs!=(after?136u:45u) || p->pages!=260 ||
    p->quiet_samples!=2 || p->address_bits!=32 || p->flags!=(after?0x3ffu:0x1ffu))return KZA2_UNQUALIFIED;
 if(p->image_capacity!=0x100000 || p->metadata_capacity!=0x4000 ||
    !p->image || !p->metadata || (p->image&0xfffff) || (p->metadata&0x3fff) ||
    p->image>0x100000000ULL-p->image_capacity || p->metadata>0x100000000ULL-p->metadata_capacity ||
    !(p->image+p->image_capacity<=p->metadata || p->metadata+p->metadata_capacity<=p->image))return KZA2_UNQUALIFIED;
 return 0;
}
static inline int KZA2Budget(const KZA2Ops *o,uint64_t start,KZA2Receipt *r)
{
 uint64_t n=o->now_us(o->cookie);r->elapsed_us=n>=start?n-start:UINT64_MAX;
 return n<start || n-start>=5000000?KZA2_DEADLINE:0;
}
static inline int KZA2Run(const KZA2Ops *o,KZA2Receipt *r)
{
 if(!r)return KZA2_ARGUMENT;memset(r,0,sizeof(*r));
 if(!o || !o->now_us || !o->preflight || !o->transaction_begin || !o->fresh_prevm ||
    !o->invoke || !o->load_image_after_setup || !o->original_gpu_chain ||
    !o->final_cp_check || !o->transaction_end){r->result=r->primary_error=KZA2_ARGUMENT;return KZA2_ARGUMENT;}
 const uint64_t start=o->now_us(o->cookie);int e=o->preflight(o->cookie,&r->before);
 if(!e)e=KZA2ProofValid(&r->before,0);
 if(!e)e=KZA2Budget(o,start,r);
 if(!e)e=o->transaction_begin(o->cookie);
 if(e){r->result=r->primary_error=e;return e;}
 r->transaction_started=1;r->phase=KZA2_LEASED;
 e=o->fresh_prevm(o->cookie,&r->before);if(e)goto done;
 for(unsigned i=0;i<3;i++){
  KZA2Operation *op=&r->operation[i];KebabPasRegs request={};
  e=KZA2Budget(o,start,r);if(e)goto done;
  e=KebabPas13Request(i==0?1:i==1?2:5,1,i==0?r->before.metadata:i==1?r->before.image:0,i==1?4096:0,&request);
  if(e){e=KZA2_ARGUMENT;goto done;}
  if(i==0)r->metadata_frozen=1;
  if(i==2)r->image_frozen=1;
  r->phase=i==0?KZA2_INIT_ATTEMPT:i==1?KZA2_MEM_ATTEMPT:KZA2_AUTH_ATTEMPT;
  op->attempted=1;
  {const uint64_t t=o->now_us(o->cookie);
   e=o->invoke(o->cookie,i,&request,&op->raw,&op->guard);
   const uint64_t n=o->now_us(o->cookie);op->elapsed_us=n>=t?n-t:UINT64_MAX;}
  if(e || op->guard){if(!e)e=(int)op->guard;goto done;}
  if(op->raw.returned!=1 || !op->raw.attempts || op->raw.attempts>32 ||
     op->raw.interrupted_exhausted || (int64_t)op->raw.status_raw>0){e=KZA2_UNRESOLVED;goto done;}
  if(op->raw.status_raw){e=KZA2_IO;goto done;}
  if(!KebabPas13OperationSucceeded(&op->raw)){e=KZA2_PERIPHERAL;goto done;}
  r->phase=i==0?KZA2_INIT_RETURNED:i==1?KZA2_MEM_RETURNED:KZA2_AUTH_RETURNED;
  if(i==2)r->authenticated=1;
  e=KZA2Budget(o,start,r);if(e)goto done;
  if(i==1){
   e=o->load_image_after_setup(o->cookie,&r->before);if(e)goto done;
   r->prepared=1;r->image_frozen=1;r->phase=KZA2_LOADED;
   e=KZA2Budget(o,start,r);if(e)goto done;
   e=o->original_gpu_chain(o->cookie,&r->before,&r->after);if(e)goto done;
   e=KZA2ProofValid(&r->after,1);if(e)goto done;
   if(r->before.image!=r->after.image || r->before.metadata!=r->after.metadata ||
      r->before.epoch!=r->after.epoch || r->before.gmu_generation!=r->after.gmu_generation){e=KZA2_UNQUALIFIED;goto done;}
   r->cp_ready=1;r->phase=KZA2_CP_READY;
  }
 }
 r->phase=KZA2_FINAL;e=o->final_cp_check(o->cookie,&r->after);
 if(!e)r->final_checked=1;
 if(!e)e=KZA2Budget(o,start,r);
done:
 r->primary_error=e;r->end_error=(uint32_t)o->transaction_end(o->cookie);
 if(!e && r->end_error)e=(int)r->end_error;
 r->result=e;if(!e){r->completed=1;r->phase=KZA2_DONE;}
 return e;
}
#endif
