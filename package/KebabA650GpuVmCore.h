/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GPU_VM_CORE_H
#define KEBAB_A650_GPU_VM_CORE_H
#if defined(KERNEL)
#include <kern/KebabGpuFirstAttachCore.h>
#else
#include "KebabGpuFirstAttachCore.h"
#endif
#include "KebabA650GpuBacking.h"
/* Separate graphics attachment inside the EXISTING namespace. The caller
 * supplies its real canonical post-HFI clock/Root child and real BO token.
 * No callback/ready vector originates in a CAP or user request. This header
 * alone does not grant native MMIO authority. Binding remains separately gated.
 * Graphics SID0/mask401 follows kona-gpu.dtsi, including SID1/privilege-bit
 * aliases. SMR2 routes that exact set; reserved SMR3 is never programmed.
 * CB1/ASID2 only; no VMID-wide ALL, ATS, protected bank, reset, fault ACK. */
enum {KVG_EMPTY=0,KVG_RESERVED=1,KVG_ATTACHED=2,KVG_FAILED=3,KVG_QUARANTINED=4};
typedef struct {
 void *cookie;int (*worker)(void*),(*hold)(void*,uint32_t*),(*check)(void*),(*end)(void*);
 int (*backing_acquire)(void*,KebabA650GpuBackingDescription*,uint32_t*),(*backing_check)(void*),(*backing_publish)(void*),(*backing_end)(void*);
 int (*map)(void*);void (*unmap)(void*);
 int (*read32)(void*,uint32_t,uint32_t*),(*read64)(void*,uint32_t,uint64_t*);
 int (*write32)(void*,uint32_t,uint32_t),(*write64)(void*,uint32_t,uint64_t);
 void (*barrier)(void*),(*delay_us)(void*,unsigned);uint64_t (*now_us)(void*);
 int (*reserve)(void*,KFNamespace*,KFToken*,const KFPolicy*);
} KVGops;
typedef struct {
 uint32_t magic,schema,bytes,result,phase,primary_error,cleanup_error,reads,writes;
 uint32_t touched,publish_attempted,published,held,backing_held,reserved,tlb_polls,GPU_DMA_proven;
 uint32_t cb,asid,smr,sid,mask,fsr_first,fsr_last,gfsr;
 uint64_t epoch,root_physical,elapsed_us,fault_far;
} KVGRecord;
typedef struct {
 uint32_t busy,attempted,mapped,held,backing_held,publish_attempted,published,touched;
 KFNamespace *space;const KFFirstAttach *parent;KFToken token;KVGops ops;
 KebabA650GpuBackingDescription backing;KGDiscovery before,after;
 KVGRecord record;uint64_t begin;uint32_t clock_started;
} KVGEngine;
static int KVGParent(const KVGEngine*a)
{
 const KFFirstAttach*p=a->parent;
 if(!p || p->space!=a->space || p->policy.initialize!=1 || p->policy.cb || p->token.cb || p->token.slot ||
 p->policy.kernel_smr || p->policy.user_smr!=1 || p->token.kernel_smr || p->token.user_smr!=1 ||
 !p->published || !p->backing_held || !p->held || !p->receipt.tlb_polls || !p->saved ||
 p->before.num_cb!=5 || p->before.num_s2 || p->before.gfsr || p->before.num_streams!=6 ||
 !KFColdCR0Matches(p->before.cr0,0x260406) || p->before.id0!=0x4c017e06 || p->before.id1!=0x20000005 || p->before.id2!=0x5511 ||
 KFFirstRunCheckPrivate(p,p->token.epoch))return KG_OWNERSHIP;
 /* Historical globals were retired by the original cold VMID-wide ALL.
  * All later owned table providers use nG; external mutations are blocked
  * by the same namespace. Virgin M0 alone would NOT prove this contract. */
 for(unsigned i=0;i<6;i++)if(p->before.stream[i].smr&0x80000000u)return KG_ROUTE;
 for(unsigned i=0;i<5;i++){const auto*c=&p->before.cb[i];if(!c->present || c->type || c->hypc || c->monc || c->vmid || (c->cba2r&1) || (c->sctlr&1) || KFContextFaultBits(c->fsr))return KG_OWNERSHIP;}
 return 0;
}
static int KVGCheck(KVGEngine*a)
{
 int e=a->ops.worker(a->ops.cookie);if(e)return e;
 e=KFNamespaceCheckPrivate(a->space,&a->token);if(e)return e;
 e=KVGParent(a);if(e)return e;e=a->ops.check(a->ops.cookie);if(e)return e;
 if(a->backing_held){e=a->ops.backing_check(a->ops.cookie);if(e)return e;}
 if(a->clock_started){uint64_t n=a->ops.now_us(a->ops.cookie);if(n<a->begin || n-a->begin>100000)return KG_TIMEOUT;}
 return 0;
}
static int KVGReadAllowed(uint32_t o,unsigned wide)
{
 /* The real ID0 profile exposes SIX rows, not the generic maximum128. */
 if(!wide&&((o>=0x818&&o<0xa00)||(o>=0xc18&&o<0xe00)))return 0;
 return KGReadAllowed(o,wide,31)||(!wide&&o==0x97f4);
}
static int KVGR32(KVGEngine*a,uint32_t o,uint32_t*v)
{if(!v || !KVGReadAllowed(o,0))return KG_INVALID;int e=KVGCheck(a);if(e)return e;a->record.reads++;return a->ops.read32(a->ops.cookie,o,v);}
static int KVGR64(KVGEngine*a,uint32_t o,uint64_t*v)
{if(!v || !KVGReadAllowed(o,1))return KG_INVALID;int e=KVGCheck(a);if(e)return e;a->record.reads++;return a->ops.read64(a->ops.cookie,o,v);}
static int KVGWriteAllowed(uint32_t o,unsigned wide)
{
 if(wide)return o==0x9020 || o==0x9028;
 return o==0x808 || o==0xc08 || o==0x1004 || o==0x1804 || o==0x9000 || o==0x9004 || o==0x9010 ||
 o==0x9030 || o==0x9038 || o==0x903c || o==0x9610 || o==0x97f0;
}
static int KVGW(KVGEngine*a,uint32_t o,uint64_t v,unsigned wide=0)
{
 if(!KVGWriteAllowed(o,wide) || !a->mapped || !a->published)return KG_CLOSED;
 int e=KVGCheck(a);if(e)return e;a->touched=1;a->record.writes++;
 return wide?a->ops.write64(a->ops.cookie,o,v):a->ops.write32(a->ops.cookie,o,(uint32_t)v);
}
#define KVG_TRY(x) do{int kvg_e=(x);if(kvg_e)return kvg_e;}while(0)
static int KVGSnapshot(KVGEngine*a,KGDiscovery*s)
{
 memset(s,0,sizeof *s);KVG_TRY(KVGR32(a,0x20,&s->id0));KVG_TRY(KVGR32(a,0x24,&s->id1));KVG_TRY(KVGR32(a,0x28,&s->id2));
 if(s->id0!=0x4c017e06 || s->id1!=0x20000005 || s->id2!=0x5511)return KG_UNSUPPORTED;
 s->num_streams=6;s->num_cb=5;s->stride=4096;s->cb_base=0x8000;s->read_mask=31;s->pa_bits=s->ipa_bits=36;s->va_bits=48;
 KVG_TRY(KVGR32(a,0,&s->cr0));KVG_TRY(KVGR32(a,0x48,&s->gfsr));a->record.gfsr=s->gfsr;
 for(unsigned i=0;i<3;i++)KVG_TRY(KVGR32(a,0x50+4*i,&s->gfsynr[i]));
 for(unsigned i=0;i<6;i++){KVG_TRY(KVGR32(a,0x800+4*i,&s->stream[i].smr));KVG_TRY(KVGR32(a,0xc00+4*i,&s->stream[i].s2cr));}
 for(unsigned i=0;i<5;i++){
  auto*c=&s->cb[i];unsigned b=0x8000+4096*i;c->present=1;
  KVG_TRY(KVGR32(a,0x1000+4*i,&c->cbar));KVG_TRY(KVGR32(a,0x1800+4*i,&c->cba2r));
  c->type=(c->cbar>>16)&3;c->hypc=(c->cbar>>10)&1;c->monc=(c->cba2r>>1)&1;c->vmid=(s->cr0&0x80000000u)?c->cba2r>>16:c->cbar&255;
  KVG_TRY(KVGR32(a,b,&c->sctlr));KVG_TRY(KVGR32(a,b+4,&c->actlr));KVG_TRY(KVGR32(a,b+0x10,&c->tcr2));KVG_TRY(KVGR32(a,b+0x30,&c->tcr));
  KVG_TRY(KVGR64(a,b+0x20,&c->ttbr0));KVG_TRY(KVGR64(a,b+0x28,&c->ttbr1));KVG_TRY(KVGR32(a,b+0x38,&c->mair0));KVG_TRY(KVGR32(a,b+0x3c,&c->mair1));
  KVG_TRY(KVGR32(a,b+0x58,&c->fsr));if(i==1){a->record.fsr_last=c->fsr;if(!a->touched)a->record.fsr_first=c->fsr;}
  KVG_TRY(KVGR64(a,b+0x60,&c->far));if(i==1)a->record.fault_far=c->far;
  KVG_TRY(KVGR32(a,b+0x68,&c->fsynr0));KVG_TRY(KVGR32(a,b+0x6c,&c->fsynr1));KVG_TRY(KVGR32(a,0x1400+4*i,&c->frsynra));
 }
 return 0;
}
static int KVGConfigEqual(const KGContext*a,const KGContext*b)
{return a->cbar==b->cbar&&a->cba2r==b->cba2r&&a->sctlr==b->sctlr&&a->actlr==b->actlr&&a->tcr==b->tcr&&a->tcr2==b->tcr2&&a->ttbr0==b->ttbr0&&a->ttbr1==b->ttbr1&&a->mair0==b->mair0&&a->mair1==b->mair1;}
static int KVGValidate(KVGEngine*a,const KGDiscovery*s,unsigned after)
{
 const auto*p=a->parent;
 if(s->cr0!=p->before.cr0 || s->gfsr || s->num_cb!=5 || s->num_streams!=6)return KG_OWNERSHIP;
 if(!KVGConfigEqual(&s->cb[0],&p->work.cb[0]) || s->stream[0].smr!=0x84000005 || (s->stream[0].s2cr&0x030300ffu) ||
 s->stream[1].smr!=0x84000004 || (s->stream[1].s2cr&0x030300ffu)!=0x20000)return KG_OWNERSHIP;
 for(unsigned i=0;i<5;i++)if(KFContextFaultBits(s->cb[i].fsr))return KG_FAULT;
 for(unsigned i=2;i<6;i++){
  if(after && i==2){if((s->stream[i].smr^0x84010000u)&0xffff7fffu || ((s->stream[i].s2cr^1u)&0x030300ffu))return KG_ROUTE;}
  else if(s->stream[i].smr&0x80000000u)return KG_ROUTE;
  if(after && i!=2 && memcmp(&s->stream[i],&a->before.stream[i],sizeof(KGStream)))return KG_OWNERSHIP;
 }
 for(unsigned i=1;i<5;i++){
  const auto*c=&s->cb[i];if(after&&i==1)continue;
  if(c->type || c->hypc || c->monc || c->vmid || (c->cba2r&1) || (c->sctlr&1))return KG_OWNERSHIP;
  if(!after&&i==1&&c->actlr!=3)return KG_OWNERSHIP;
  if(after && !KVGConfigEqual(c,&a->before.cb[i]))return KG_OWNERSHIP;
 }
 return 0;
}
static int KVGBackingValid(const KebabA650GpuBackingDescription*b)
{
 static const uint32_t va[6]={0x90000000,0x90004000,0x90008000,0x9000c000,0x90010000,0x90020000};
 static const uint32_t bytes[6]={4096,4096,16384,16384,65536,4096},perms[6]={1,1,5,5,3,3};
 if(!b->generation || !b->table_descriptor_identity || b->table_bytes!=65536 || b->region_count!=6 || b->mapped_pages!=27 || b->cleaned_for_device!=1 ||
 (b->root_physical&16383) || b->root_physical<0x20000 || b->root_physical>KG_PA_LIMIT-65536)return KG_POOL;
 for(unsigned i=0;i<6;i++){const auto*r=&b->region[i];unsigned n=(bytes[i]+16383)&~16383u;
  if(r->iova!=va[i] || r->bytes!=bytes[i] || r->permissions!=perms[i] || r->allocation_bytes!=n || !r->descriptor_identity || r->descriptor_identity==b->table_descriptor_identity ||
  !r->cpu || (r->cpu&16383) || (r->physical&16383) || r->physical<0x20000 || r->physical>KG_PA_LIMIT-n || KGOverlap(r->physical,n,b->root_physical,65536))return KG_POOL;
  for(unsigned j=0;j<i;j++)if(r->descriptor_identity==b->region[j].descriptor_identity || KGOverlap(r->physical,n,b->region[j].physical,b->region[j].allocation_bytes))return KG_POOL;
 }
 return 0;
}
static int KVGReadback(KVGEngine*a,uint32_t o,uint32_t want,uint32_t mask)
{uint32_t v;KVG_TRY(KVGR32(a,o,&v));return ((v^want)&mask)?KG_IO:0;}
static int KVGPut(KVGEngine*a,uint32_t o,uint32_t v,uint32_t mask=UINT32_MAX)
{KVG_TRY(KVGW(a,o,v));a->ops.barrier(a->ops.cookie);return KVGReadback(a,o,v,mask);}
static int KVGTlb(KVGEngine*a)
{
 KVG_TRY(KVGW(a,0x9610,2));a->ops.barrier(a->ops.cookie);KVG_TRY(KVGW(a,0x97f0,0));a->ops.barrier(a->ops.cookie);
 for(unsigned i=0;i<2000;i++){uint32_t v;KVG_TRY(KVGR32(a,0x97f4,&v));a->record.tlb_polls++;if(!(v&1))return 0;a->ops.delay_us(a->ops.cookie,1);}return KG_TIMEOUT;
}
static int KVGProgram(KVGEngine*a)
{
 /* Publish first; every failure from here keeps BOs, namespace and parent
  * pins. No guessed drain, reset, restore or speculative unpublish exists. */
 a->publish_attempted=1;KVG_TRY(a->ops.backing_publish(a->ops.cookie));a->published=1;
 KVG_TRY(KVGPut(a,0xc08,0x20000,0x030300ffu));KVG_TRY(KVGPut(a,0x808,0x84010000,0xffff7fffu));
 KVG_TRY(KVGPut(a,0x9000,0,0x0fdf11f7u));KVG_TRY(KVGPut(a,0x1804,1,3));KVG_TRY(KVGPut(a,0x1004,0x1f300,0x3ffff));KVG_TRY(KVGPut(a,0x9004,0x32b,0x32b));
 KVG_TRY(KVGTlb(a));KVG_TRY(KVGPut(a,0x9010,0x38011,0x38017));KVG_TRY(KVGPut(a,0x9030,0x00802020,0x00c0ffbf));
 uint64_t ttbr=a->backing.root_physical|(2ULL<<48),v;
 KVG_TRY(KVGW(a,0x9020,ttbr,1));KVG_TRY(KVGW(a,0x9028,2ULL<<48,1));a->ops.barrier(a->ops.cookie);
 KVG_TRY(KVGR64(a,0x9020,&v));if((v^ttbr)&(KG_PTE_PA|0xffff000000000000ULL))return KG_IO;
 KVG_TRY(KVGR64(a,0x9028,&v));if((v^(2ULL<<48))&(KG_PTE_PA|0xffff000000000000ULL))return KG_IO;
 KVG_TRY(KVGPut(a,0x9038,0x44,255));KVG_TRY(KVGPut(a,0x903c,0));
 KVG_TRY(KVGPut(a,0x9000,0xc00026,0x0fdf11f7u));KVG_TRY(KVGTlb(a));KVG_TRY(KVGPut(a,0x9000,0xc00027,0x0fdf11f7u));
 KVG_TRY(KVGPut(a,0xc08,1,0x030300ffu));KVG_TRY(KVGSnapshot(a,&a->after));KVG_TRY(KVGValidate(a,&a->after,1));
 return KVGCheck(a);
}
static int KVGFinish(KVGEngine*a,int e)
{
 if(a->clock_started){uint64_t n=a->ops.now_us(a->ops.cookie);a->record.elapsed_us=n>=a->begin?n-a->begin:UINT64_MAX;if(!e&&a->record.elapsed_us>100000)e=KG_TIMEOUT;}
 a->record.primary_error=e;
 if(e){
  if(a->publish_attempted){a->record.phase=KVG_QUARANTINED;KFNamespaceRevoke(a->space);}
  else{
   int cleanup=0;if(a->mapped){a->ops.unmap(a->ops.cookie);a->mapped=0;}
   if(a->backing_held){cleanup=a->ops.backing_end(a->ops.cookie);if(!cleanup)a->backing_held=0;}
   if(a->held&&!cleanup){cleanup=a->ops.end(a->ops.cookie);if(!cleanup)a->held=0;}
   if(!a->held&&!a->backing_held&&a->token.held&&!cleanup)cleanup=KFUnreserve(a->space,&a->token);
   a->record.cleanup_error=cleanup;a->record.phase=cleanup?KVG_QUARANTINED:KVG_FAILED;if(cleanup){KFNamespaceRevoke(a->space);e=KG_QUARANTINE;}
  }
 }
 a->record.result=e;a->record.touched=a->touched;a->record.publish_attempted=a->publish_attempted;a->record.published=a->published;a->record.held=a->held;
 a->record.backing_held=a->backing_held;a->record.reserved=a->token.held;a->record.epoch=a->token.epoch;a->record.root_physical=a->backing.root_physical;
 KFUnlock(&a->busy);return e;
}
static int KVGAttach(KVGEngine*a,KFNamespace*s,const KFFirstAttach*parent,const KFPolicy*p,const KVGops*o)
{
 if(!a||!s||!parent||!p||!o||!o->worker||!o->hold||!o->check||!o->end||!o->backing_acquire||!o->backing_check||!o->backing_publish||!o->backing_end||
 !o->map||!o->unmap||!o->read32||!o->read64||!o->write32||!o->write64||!o->barrier||!o->delay_us||!o->now_us||!o->reserve)return KG_INVALID;
 if(!KFPolicyBound(p)||p->cb!=1||p->kernel_smr!=2||p->user_smr!=3||p->vmid||!p->initialize||p->id0!=0x4c017e06||p->id1!=0x20000005||p->id2!=0x5511||p->cr0!=0x260406)return KG_CLOSED;
 if(!KFTry(&a->busy))return KG_BUSY;if(a->attempted){KFUnlock(&a->busy);return KG_STATE;}a->attempted=1;a->space=s;a->parent=parent;a->ops=*o;
 a->record.magic=0x31564741;a->record.schema=1;a->record.bytes=sizeof a->record;a->record.cb=1;a->record.asid=2;a->record.smr=2;a->record.sid=0;a->record.mask=0x401;
 int e=o->worker(o->cookie);if(e)return KVGFinish(a,e);e=KVGParent(a);if(e)return KVGFinish(a,e);e=o->reserve(o->cookie,s,&a->token,p);if(e)return KVGFinish(a,e);
 a->record.phase=KVG_RESERVED;e=o->hold(o->cookie,&a->held);if(e)return KVGFinish(a,e);if(!a->held)return KVGFinish(a,KG_OWNERSHIP);
 e=o->backing_acquire(o->cookie,&a->backing,&a->backing_held);if(e)return KVGFinish(a,e);if(!a->backing_held)return KVGFinish(a,KG_OWNERSHIP);e=KVGBackingValid(&a->backing);if(e)return KVGFinish(a,e);
 a->begin=o->now_us(o->cookie);a->clock_started=1;e=KVGCheck(a);if(e)return KVGFinish(a,e);a->mapped=1;e=o->map(o->cookie);if(e)return KVGFinish(a,e);
 e=KVGSnapshot(a,&a->before);if(e)return KVGFinish(a,e);e=KVGValidate(a,&a->before,0);if(e)return KVGFinish(a,e);
 e=KVGSnapshot(a,&a->after);if(e)return KVGFinish(a,e);e=KVGValidate(a,&a->after,0);if(e)return KVGFinish(a,e);
 if(memcmp(a->before.stream,a->after.stream,6*sizeof(KGStream)))return KVGFinish(a,KG_OWNERSHIP);
 for(unsigned i=0;i<5;i++)if(!KVGConfigEqual(&a->before.cb[i],&a->after.cb[i]))return KVGFinish(a,KG_OWNERSHIP);
 e=KVGProgram(a);if(!e)a->record.phase=KVG_ATTACHED;return KVGFinish(a,e);
}
#undef KVG_TRY
#endif
