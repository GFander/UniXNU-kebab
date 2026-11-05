/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_SMMU_CORE_H
#define KEBAB_GPU_SMMU_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* SM8250 GPU unit only. Not a replacement for arm-smmu device_reset().
 * CB5/6/7, ATS, GR0 writes, routing writes and fault ACK/resume are absent.
 * KG_NO_GLOBAL: ASID mode needs historical and lifetime absence of conflicting
 * globals; KG_VM_ALL may retire historical globals but still needs a positive
 * lifetime namespace compatibility grant. ALL is VMID-wide, never CB-local.
 * KG_NO_LOCKED is a target namespace contract, not an inference from M==0.
 * All public APIs require a zero-initialized, boot-lifetime owner object.
 * Callbacks are trusted kernel bindings, never an ioctl/user-selected vector. */
enum { KG_OK=0, KG_INVALID=1, KG_CLOSED=2, KG_IO=3, KG_UNSUPPORTED=4,
 KG_ROUTE=5, KG_OWNERSHIP=6, KG_BUSY=7, KG_TIMEOUT=8, KG_FAULT=9,
 KG_POOL=10, KG_TABLE_FULL=11, KG_QUARANTINE=12, KG_STATE=13 };
enum { KG_EMPTY=0, KG_PREPARED=1, KG_CONFIGURED_QUIESCED=2,
 KG_RESTORED=3, KG_RELEASED=4, KG_QUARANTINED=5, KG_OBSERVER=6 };
enum { KG_GFX_USER=0, KG_GMU_USER=1, KG_GMU_KERNEL=2 };
enum { KG_READ=1, KG_WRITE=2, KG_EXEC=4, KG_PRIV=8 };
enum { KG_POWER=1, KG_GCC=2, KG_AHB=4, KG_BUS=8, KG_EXEC_BLOCKED=16,
 KG_NS_ACCESS=32, KG_CONTEXT=64, KG_STREAM=128, KG_ASID=256,
 KG_NO_GLOBAL=512, KG_DRAINED=1024, KG_NO_LOCKED=2048, KG_VM_ALL=4096 };
/* RO bus access does not require a running GMU core or a CM3 reset owner.
 * EXEC_BLOCKED is an active-domain producer hold, not a bus clock fact. */
#define KG_ACCESS (KG_POWER|KG_GCC|KG_AHB|KG_BUS|KG_NS_ACCESS)
#define KG_EXCLUSIVE (KG_ACCESS|KG_EXEC_BLOCKED|KG_CONTEXT|KG_STREAM|KG_ASID|KG_NO_GLOBAL|KG_NO_LOCKED)
#define KG_BIND (KG_EXCLUSIVE|KG_DRAINED)
#define KG_BASE 0x03da0000ULL
#define KG_BYTES 0x10000u
#define KG_MAX_STREAMS 128u
#define KG_MAX_REGIONS 16u
#define KG_TABLE_BYTES 65536u
#define KG_PA_LIMIT 0x1000000000ULL
#define KG_PTE_PA 0xffffff000ULL
#define KG_POLL_LIMIT 1024u

typedef struct {
 uint64_t generation; uint32_t flags, held, role, cb, smr, asid, cb_read_mask,uncertain;
} KGLease;
typedef struct {
 void *handle, *address; uint64_t pa; uint32_t bytes, prepared;
 uint32_t prepare_result, complete_result;
} KGPool;
typedef struct {
 uint32_t iova, bytes, permissions, initial_bytes; const void *initial;
} KGRegion;
typedef struct { uint32_t smr,s2cr; } KGStream;
typedef struct { uint32_t result,cb,smr,protected_cb,owner_proven; } KGObservedRoute;
typedef struct {
 uint32_t present,cbar,cba2r,sctlr,actlr,tcr2,tcr,mair0,mair1,vmid,type,hypc,monc;
 uint32_t fsr,fsynr0,fsynr1,frsynra; uint64_t ttbr0,ttbr1,far;
} KGContext;
typedef struct {
 uint32_t id0,id1,id2,cr0,gfsr,gfsynr[3],num_streams,num_cb,num_s2,pa_bits,ipa_bits,va_bits;
 uint32_t cb_base,stride,read_mask; KGStream stream[KG_MAX_STREAMS];
 KGObservedRoute route[3];
 KGContext cb[5];
} KGDiscovery;
typedef struct {
 void *cookie;
 int (*worker)(void *);
 /* Failure with held=1 is uncertain: the core quarantines, never ends it. */
 int (*acquire)(void *,uint32_t role,uint32_t active,KGLease *);
 int (*check)(void *,const KGLease *,uint32_t required);
 /* Must stop every producer of this stream group and drain all transactions,
  * including stalled ones. Reset assertion alone is insufficient evidence. */
 int (*quiesce)(void *,KGLease *);
 int (*end)(void *,KGLease *);
 int (*map)(void *,const KGLease *); void (*unmap)(void *);
 int (*read32)(void *,uint32_t,uint32_t *);
 int (*read64)(void *,uint32_t,uint64_t *);
 int (*write32)(void *,uint32_t,uint32_t);
 int (*write64)(void *,uint32_t,uint64_t);
 void (*barrier)(void *); void (*delay)(void *);
 /* Alloc returns its partial resource even on failure. Free must retain a
  * prepared descriptor if complete fails; it must not wipe a published pool. */
 int (*alloc)(void *,uint32_t index,uint32_t bytes,KGPool *);
 int (*pin_check)(void *,const KGPool *);
 int (*clean_poc)(void *,const KGPool *);
 int (*free_pool)(void *,KGPool *);
} KGOps;
typedef struct {
 uint32_t magic,schema,bytes,result,primary_error,cleanup_error,state,role;
 uint32_t reads,writes,tlb_polls,pools,table_pages,mapped_pages;
 uint32_t hardware_touched,dma_proven,lease_held,retained_pools;
 uint32_t fault_first,fault_last,fsynr0,fsynr1,frsynra,fault_unstable;
 uint64_t fault_far,root_pa,generation;
} KGReceipt;
typedef struct {
 uint32_t busy,magic; KGOps ops; KGLease lease; KGReceipt receipt;
 KGPool pool[KG_MAX_REGIONS+1]; KGRegion region[KG_MAX_REGIONS];
 uint32_t count,mapped,touched,saved_valid;
 KGDiscovery before,work;
} KGDomain;

static uint32_t KGTry(KGDomain *d) { return __sync_bool_compare_and_swap(&d->busy,0,1); }
static void KGUnlock(KGDomain *d) { __sync_lock_release(&d->busy); }
static int KGOverlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m)
{ return a<b+m && b<a+n; }
static int KGRole(uint32_t r) { return r<=KG_GMU_KERNEL; }
static uint32_t KGSid(uint32_t r) { return r==KG_GFX_USER?0:r==KG_GMU_USER?4:5; }
static uint32_t KGMask(uint32_t r) { return r==KG_GFX_USER?0x401:0x400; }
static int KGCheck(KGDomain *d,uint32_t need)
{
 if(!d->lease.held || d->lease.uncertain || !d->lease.generation || (d->lease.flags&need)!=need ||
    (d->lease.cb_read_mask&~31u))return KG_CLOSED;
 return d->ops.check(d->ops.cookie,&d->lease,need);
}
static int KGReadAllowed(uint32_t off,uint32_t wide,uint32_t cbmask)
{
 if(!wide && (off==0 || off==0x20 || off==0x24 || off==0x28 || off==0x48 ||
    off==0x50 || off==0x54 || off==0x58 ||
    (((off>=0x800 && off<0xa00) || (off>=0xc00 && off<0xe00)) && !(off&3))))return 1;
 for(uint32_t cb=0;cb<5;cb++)if(cbmask&(1u<<cb)) {
  if(!wide && (off==0x1000+4*cb || off==0x1400+4*cb || off==0x1800+4*cb))return 1;
  uint32_t b=0x8000+4096*cb;
  if(wide && (off==b+0x20 || off==b+0x28 || off==b+0x60))return 1;
  if(!wide && (off==b || off==b+4 || off==b+0x10 || off==b+0x30 || off==b+0x38 ||
      off==b+0x3c || off==b+0x58 || off==b+0x68 || off==b+0x6c || off==b+0x7f4))return 1;
 }
 return 0;
}
static int KGWriteAllowed(uint32_t off,uint32_t wide,uint32_t cb)
{
 if(cb>=5)return 0;
 uint32_t b=0x8000+4096*cb;
 if(wide)return off==b+0x20 || off==b+0x28;
 return off==b || off==b+0x10 || off==b+0x30 || off==b+0x38 || off==b+0x3c ||
  off==b+0x610 || off==b+0x7f0;
}
static int KGWriteScope(uint32_t off,uint32_t wide,const KGLease *l)
{
 if(!l || l->cb>=5)return 0;
 if(!wide && (l->flags&KG_VM_ALL) && off==0x8618+l->cb*4096)return 1;
 return KGWriteAllowed(off,wide,l->cb);
}
static int KGR32(KGDomain *d,uint32_t o,uint32_t *v)
{
 int e=KGCheck(d,KG_ACCESS);if(e)return e;
 if(!KGReadAllowed(o,0,d->lease.cb_read_mask))return KG_INVALID;
 d->receipt.reads++;return d->ops.read32(d->ops.cookie,o,v);
}
static int KGR64(KGDomain *d,uint32_t o,uint64_t *v)
{
 int e=KGCheck(d,KG_ACCESS);if(e)return e;
 if(!KGReadAllowed(o,1,d->lease.cb_read_mask))return KG_INVALID;
 d->receipt.reads++;return d->ops.read64(d->ops.cookie,o,v);
}
static int KGW(KGDomain *d,uint32_t o,uint64_t v,uint32_t wide)
{
 int e=KGCheck(d,KG_BIND);if(e)return e;
 if(!KGWriteScope(o,wide,&d->lease))return KG_INVALID;
 /* A callback may report failure after a completed bus write. Retain first. */
 d->touched=1;d->receipt.hardware_touched=1;d->receipt.writes++;
 return wide?d->ops.write64(d->ops.cookie,o,v):d->ops.write32(d->ops.cookie,o,(uint32_t)v);
}
#define KG_TRY(x) do { int kg_e=(x);if(kg_e)return kg_e; } while(0)
static void KGObserveRoutes(KGDiscovery *s)
{
 for(uint32_t role=0;role<3;role++) {
  KGObservedRoute *r=&s->route[role];r->result=KG_ROUTE;r->cb=r->smr=255;
  uint32_t sid=KGSid(role),mask=KGMask(role),matches=0,bad=0;
  for(uint32_t i=0;i<s->num_streams;i++) {
   uint32_t a=s->stream[i].smr,b=s->stream[i].s2cr,m=(a>>16)&0x7fff,id=a&0x7fff;
   if(!(a&0x80000000u) || ((id^sid)&~(m|mask)&0x7fff))continue;
   matches++;if(m!=mask || (id&~mask)!=(sid&~mask) || ((b>>16)&3)!=0)bad=1;
   r->smr=i;r->cb=b&255;
  }
  if(matches==1 && !bad && r->cb<s->num_cb) {
   r->protected_cb=r->cb>=5;r->result=r->protected_cb?KG_CLOSED:KG_OK;
  }
  /* Observed routing never reserves a CB, stream or fixed ASID. */
  r->owner_proven=0;
 }
}
static int KGDiscoverLocked(KGDomain *d,KGDiscovery *s)
{
 memset(s,0,sizeof *s);
 KG_TRY(KGR32(d,0x20,&s->id0));KG_TRY(KGR32(d,0x24,&s->id1));KG_TRY(KGR32(d,0x28,&s->id2));
 s->num_streams=s->id0&255;s->num_cb=s->id1&255;s->num_s2=(s->id1>>16)&255;
 s->stride=(s->id1&(1u<<31))?65536:4096;
 s->cb_base=(1u<<(((s->id1>>28)&7)+1))*s->stride;
 static const uint32_t bits[6]={32,36,40,42,44,48};uint32_t oas=(s->id2>>4)&15;
 s->pa_bits=oas<6?bits[oas]:0;
 uint32_t ias=s->id2&15,ubs=(s->id2>>8)&15;
 s->ipa_bits=ias<6?bits[ias]:0;s->va_bits=ubs<6?bits[ubs]:0;
 if(!(s->id0&(1u<<30)) || !(s->id0&(1u<<27)) || (s->id0&(1u<<8)) ||
    !(s->id2&(1u<<12)) || s->pa_bits<36 || s->ipa_bits<36 || s->va_bits<32 ||
    s->stride!=4096 || s->cb_base!=KG_BYTES/2 ||
    !s->num_cb || s->num_cb>8 || s->num_s2>s->num_cb ||
    !s->num_streams || s->num_streams>KG_MAX_STREAMS)return KG_UNSUPPORTED;
 KG_TRY(KGR32(d,0,&s->cr0));KG_TRY(KGR32(d,0x48,&s->gfsr));
 for(uint32_t i=0;i<3;i++)KG_TRY(KGR32(d,0x50+4*i,&s->gfsynr[i]));
 for(uint32_t i=0;i<s->num_streams;i++) {
  KG_TRY(KGR32(d,0x800+4*i,&s->stream[i].smr));KG_TRY(KGR32(d,0xc00+4*i,&s->stream[i].s2cr));
 }
 KGObserveRoutes(s);
 s->read_mask=d->lease.cb_read_mask&((1u<<s->num_cb)-1)&31u;
 for(uint32_t i=0;i<5;i++)if(s->read_mask&(1u<<i)) {
  KGContext *c=&s->cb[i];uint32_t b=s->cb_base+i*s->stride;c->present=1;
  KG_TRY(KGR32(d,0x1000+4*i,&c->cbar));KG_TRY(KGR32(d,0x1800+4*i,&c->cba2r));
  c->type=(c->cbar>>16)&3;c->hypc=(c->cbar>>10)&1;c->monc=(c->cba2r>>1)&1;
  c->vmid=(s->cr0&0x80000000u)?c->cba2r>>16:c->cbar&255;
  KG_TRY(KGR32(d,b,&c->sctlr));KG_TRY(KGR32(d,b+4,&c->actlr));
  KG_TRY(KGR32(d,b+0x10,&c->tcr2));KG_TRY(KGR32(d,b+0x30,&c->tcr));
  KG_TRY(KGR64(d,b+0x20,&c->ttbr0));KG_TRY(KGR64(d,b+0x28,&c->ttbr1));
  KG_TRY(KGR32(d,b+0x38,&c->mair0));KG_TRY(KGR32(d,b+0x3c,&c->mair1));
  KG_TRY(KGR32(d,b+0x58,&c->fsr));KG_TRY(KGR64(d,b+0x60,&c->far));
  KG_TRY(KGR32(d,b+0x68,&c->fsynr0));KG_TRY(KGR32(d,b+0x6c,&c->fsynr1));
  KG_TRY(KGR32(d,0x1400+4*i,&c->frsynra));
 }
 return KG_OK;
}
static int KGRouteCheck(const KGDiscovery *s,const KGLease *l,uint32_t disabled)
{
 if(l->cb>=5 || l->cb>=s->num_cb || l->cb<s->num_s2 || l->smr>=s->num_streams ||
    l->asid!=l->cb+1 || !s->cb[l->cb].present)return KG_OWNERSHIP;
 uint32_t sid=KGSid(l->role),mask=KGMask(l->role),matches=0;
 for(uint32_t i=0;i<s->num_streams;i++) {
  uint32_t a=s->stream[i].smr,b=s->stream[i].s2cr;
  if(!(a&0x80000000u))continue;
  uint32_t id=a&0x7fff,m=(a>>16)&0x7fff;
  /* Intersection of two masked sets. Reject any broader/split/duplicate route.
   * This deliberately does not reinterpret SID4/5 as a bank index. */
  if(!((id^sid)&~(m|mask)&0x7fff)) {
   if(i!=l->smr || m!=mask || (id&~mask)!=(sid&~mask) || ((b>>16)&3)!=0 ||
      (b&255)!=l->cb)return KG_ROUTE;
   matches++;
  } else if(((b>>16)&3)==0 && (b&255)==l->cb)return KG_ROUTE;
 }
 const KGContext *c=&s->cb[l->cb];
 /* TYPE=S1/S2-bypass, no HYPC/E2HC, VA64, no MONC. Existing ACTLR is
  * validated against the GPU DT's 0x32b contract; it is never rewritten. */
 if(matches!=1 || ((c->cbar>>16)&3)!=1 || (c->cbar&(1u<<10)) ||
    (c->cba2r&3)!=1 || c->actlr!=0x32b || (s->cr0&1) || s->gfsr || c->fsr ||
    (disabled && (c->sctlr&1)))return KG_ROUTE;
 /* ASID-only invalidation cannot retire inherited tags for another ASID.
  * VM_ALL is separately authorized VMID-wide maintenance, not CB-local ALL. */
 if(!(l->flags&KG_VM_ALL) && ((c->ttbr0>>48)!=l->asid || (c->ttbr1>>48)!=l->asid))return KG_ROUTE;
 return KG_OK;
}
static int KGSameRouting(const KGDiscovery *a,const KGDiscovery *b,uint32_t cb)
{
 if(a->id0!=b->id0 || a->id1!=b->id1 || a->id2!=b->id2 || a->cr0!=b->cr0 ||
    a->num_streams!=b->num_streams || a->read_mask!=b->read_mask ||
    memcmp(a->stream,b->stream,a->num_streams*sizeof(a->stream[0])))return 0;
 if(cb>=5)return 0;
 return a->cb[cb].cbar==b->cb[cb].cbar && a->cb[cb].cba2r==b->cb[cb].cba2r &&
  a->cb[cb].actlr==b->cb[cb].actlr;
}
static int KGTlb(KGDomain *d)
{
 uint32_t b=0x8000+d->lease.cb*4096,v=1;
 KG_TRY(KGW(d,b+((d->lease.flags&KG_VM_ALL)?0x618:0x610),
  (d->lease.flags&KG_VM_ALL)?0:d->lease.asid,0));d->ops.barrier(d->ops.cookie);
 KG_TRY(KGW(d,b+0x7f0,0,0));d->ops.barrier(d->ops.cookie);
 for(uint32_t i=0;i<KG_POLL_LIMIT;i++) {
  KG_TRY(KGR32(d,b+0x7f4,&v));d->receipt.tlb_polls++;
  if(!(v&1))return KG_OK;
  d->ops.delay(d->ops.cookie);
 }
 return KG_TIMEOUT;
}
static int KGProgram(KGDomain *d,const KGContext *c)
{
 uint32_t b=0x8000+d->lease.cb*4096;
 KG_TRY(KGW(d,b+0x10,c->tcr2,0));KG_TRY(KGW(d,b+0x30,c->tcr,0));
 KG_TRY(KGW(d,b+0x20,c->ttbr0,1));KG_TRY(KGW(d,b+0x28,c->ttbr1,1));
 KG_TRY(KGW(d,b+0x38,c->mair0,0));KG_TRY(KGW(d,b+0x3c,c->mair1,0));
 d->ops.barrier(d->ops.cookie);return KG_OK;
}
static int KGVerify(KGDomain *d,const KGContext *c,uint32_t sctlr)
{
 uint32_t b=0x8000+d->lease.cb*4096,v;uint64_t q;
 KG_TRY(KGR32(d,b+0x10,&v));if(v!=c->tcr2)return KG_IO;
 KG_TRY(KGR32(d,b+0x30,&v));if(v!=c->tcr)return KG_IO;
 KG_TRY(KGR64(d,b+0x20,&q));if(q!=c->ttbr0)return KG_IO;
 KG_TRY(KGR64(d,b+0x28,&q));if(q!=c->ttbr1)return KG_IO;
 KG_TRY(KGR32(d,b+0x38,&v));if(v!=c->mair0)return KG_IO;
 KG_TRY(KGR32(d,b+0x3c,&v));if(v!=c->mair1)return KG_IO;
 KG_TRY(KGR32(d,b,&v));return v==sctlr?KG_OK:KG_IO;
}
static int KGRestoreLocked(KGDomain *d)
{
 KG_TRY(KGCheck(d,KG_EXCLUSIVE));KG_TRY(d->ops.quiesce(d->ops.cookie,&d->lease));
 KG_TRY(KGCheck(d,KG_BIND));KG_TRY(KGDiscoverLocked(d,&d->work));
 if(!d->saved_valid || !KGSameRouting(&d->before,&d->work,d->lease.cb))return KG_OWNERSHIP;
 /* A stalled fault cannot be proved drained by TLBSYNC. No ACK, RESUME or
  * blanket reset here; uncertain resources and all power refs stay retained. */
 if(d->work.cb[d->lease.cb].fsr || d->work.gfsr)return KG_FAULT;
 KGContext *old=&d->before.cb[d->lease.cb];uint32_t b=0x8000+d->lease.cb*4096;
 KG_TRY(KGW(d,b,old->sctlr&~1u,0));d->ops.barrier(d->ops.cookie);
 uint32_t v;KG_TRY(KGR32(d,b,&v));if(v!=(old->sctlr&~1u))return KG_IO;
 KG_TRY(KGTlb(d));KG_TRY(KGProgram(d,old));
 KG_TRY(KGVerify(d,old,old->sctlr&~1u));
 KG_TRY(KGW(d,b,old->sctlr,0));d->ops.barrier(d->ops.cookie);
 KG_TRY(KGVerify(d,old,old->sctlr));
 d->touched=0;d->receipt.state=KG_RESTORED;return KG_OK;
}
static int KGEnd(KGDomain *d)
{
 if(d->mapped) {d->ops.unmap(d->ops.cookie);d->mapped=0;}
 if(d->lease.held) {
  int e=d->ops.end(d->ops.cookie,&d->lease);if(e)return e;
  d->lease.held=0;
 }
 return KG_OK;
}
static int KGDispose(KGDomain *d)
{
 int e=0;
 for(uint32_t i=KG_MAX_REGIONS+1;i;i--)if(d->pool[i-1].handle) {
  int r=d->ops.free_pool(d->ops.cookie,&d->pool[i-1]);if(r)e=r;
 }
 return e;
}
static int KGFinish(KGDomain *d,int error)
{
 d->receipt.primary_error=(uint32_t)error;
 if(d->lease.uncertain)d->receipt.cleanup_error=KG_OWNERSHIP;
 if(error && d->touched && d->receipt.state!=KG_QUARANTINED) {
  int e=KGRestoreLocked(d);if(e)d->receipt.cleanup_error=(uint32_t)e;
 }
 if((d->touched && d->receipt.state!=KG_CONFIGURED_QUIESCED) || d->receipt.cleanup_error) {
  d->receipt.state=KG_QUARANTINED;error=KG_QUARANTINE;
 } else if(d->receipt.state!=KG_CONFIGURED_QUIESCED) {
  int e=KGEnd(d);if(e){d->receipt.cleanup_error=(uint32_t)e;d->receipt.state=KG_QUARANTINED;error=KG_QUARANTINE;}
 }
 d->receipt.retained_pools=0;
 for(uint32_t i=0;i<KG_MAX_REGIONS+1;i++)if(d->pool[i].handle)d->receipt.retained_pools++;
 d->receipt.lease_held=d->lease.held;d->receipt.result=(uint32_t)error;
 KGUnlock(d);return error;
}
static int KGValidOps(const KGOps *o)
{
 return o && o->worker && o->acquire && o->check && o->quiesce && o->end && o->map &&
  o->unmap && o->read32 && o->read64 && o->write32 && o->write64 && o->barrier &&
  o->delay && o->alloc && o->pin_check && o->clean_poc && o->free_pool;
}
static int KGWindow(uint32_t role,uint32_t va,uint32_t bytes)
{
 uint64_t end=(uint64_t)va+bytes;
 if(!KGRole(role) || !bytes || (va&4095) || (bytes&4095) || end>0x100000000ULL)return 0;
 if(role==KG_GMU_KERNEL)return (va>=0x4000 && end<=0x1000000) || (va>=0x60000000 && end<=0x80000000);
 if(role==KG_GMU_USER)return va>=0x80000000 && end<=0xc0000000;
 return va>=0x20000 && end<=0x100000000ULL;
}
static int KebabGpuSmmuObserverInit(KGDomain *d,const KGOps *o,uint32_t role)
{
 if(!d || !KGValidOps(o) || !KGRole(role))return KG_INVALID;
 if(!KGTry(d))return KG_BUSY;if(d->magic){KGUnlock(d);return KG_STATE;}
 d->magic=0x3153474b;d->ops=*o;d->receipt.magic=d->magic;d->receipt.schema=1;
 d->receipt.bytes=sizeof(d->receipt);d->receipt.role=role;d->receipt.state=KG_OBSERVER;
 d->receipt.result=KG_CLOSED;KGUnlock(d);return KG_OK;
}
static uint64_t KGAttrs(uint32_t p)
{
 uint64_t a=3u|0x20u|0x300u|0x400u|0x800u; /* valid, NS, IS, AF, nG */
 if(!(p&KG_PRIV))a|=0x40;
 if(!(p&KG_WRITE))a|=0x80;
 if(!(p&KG_EXEC))a|=0x0060000000000000ULL;
 return a;
}
static uint64_t *KGChild(KGPool *p,uint64_t e)
{
 uint64_t a=e&KG_PTE_PA;
 if((e&~KG_PTE_PA)!=3 || a<p->pa || a-p->pa>p->bytes-4096)return NULL;
 return (uint64_t *)((uint8_t *)p->address+(a-p->pa));
}
static int KGBuild(KGDomain *d)
{
 KGPool *p=&d->pool[0];uint64_t *root=(uint64_t *)p->address;uint32_t used=1;
 d->receipt.mapped_pages=0;d->receipt.table_pages=0;
 memset(p->address,0,p->bytes);
 for(uint32_t r=0;r<d->count;r++)for(uint32_t j=0;j<d->region[r].bytes;j+=4096) {
  uint64_t va=(uint64_t)d->region[r].iova+j;uint32_t i1=(uint32_t)(va>>30),i2=(va>>21)&511,i3=(va>>12)&511;
  if(!root[i1]){if(used==p->bytes/4096)return KG_TABLE_FULL;root[i1]=(p->pa+4096*used++)|3;}
  uint64_t *l2=KGChild(p,root[i1]);if(!l2)return KG_POOL;
  if(!l2[i2]){if(used==p->bytes/4096)return KG_TABLE_FULL;l2[i2]=(p->pa+4096*used++)|3;}
  uint64_t *l3=KGChild(p,l2[i2]);if(!l3 || l3[i3])return KG_POOL;
  l3[i3]=(d->pool[r+1].pa+j)|KGAttrs(d->region[r].permissions);d->receipt.mapped_pages++;
 }
 d->receipt.table_pages=used;d->receipt.root_pa=p->pa;return KG_OK;
}
static int KGTranslate(const KGDomain *d,uint32_t va,uint64_t *pa,uint64_t *attrs)
{
 if(!d || !pa || !attrs || !d->pool[0].address)return KG_INVALID;
 KGPool *p=(KGPool *)&d->pool[0];const uint64_t *root=(const uint64_t *)p->address;
 const uint64_t *l2=KGChild(p,root[va>>30]);if(!l2)return KG_ROUTE;
 const uint64_t *l3=KGChild(p,l2[(va>>21)&511]);if(!l3)return KG_ROUTE;
 uint64_t e=l3[(va>>12)&511];if((e&3)!=3)return KG_ROUTE;
 *pa=(e&KG_PTE_PA)|(va&4095);*attrs=e&~KG_PTE_PA;return KG_OK;
}
static int KGPoolValid(KGDomain *d,KGPool *p,uint32_t bytes)
{
 if(!p->handle || !p->prepared || !p->address || ((uintptr_t)p->address&4095) ||
    p->bytes!=bytes || p->pa<0x20000 || (p->pa&4095) || p->pa>KG_PA_LIMIT-bytes)return KG_POOL;
 return d->ops.pin_check(d->ops.cookie,p);
}
static int KebabGpuSmmuPrepare(KGDomain *d,const KGOps *o,uint32_t role,const KGRegion *r,uint32_t n)
{
 if(!d || !KGValidOps(o) || !r || !n || n>KG_MAX_REGIONS || !KGRole(role))return KG_INVALID;
 if(!KGTry(d))return KG_BUSY;
 if(d->magic){KGUnlock(d);return KG_STATE;}
 d->magic=0x3153474b;d->ops=*o;d->receipt.magic=d->magic;d->receipt.schema=1;
 d->receipt.bytes=sizeof(d->receipt);d->receipt.role=role;d->count=n;int e=o->worker(o->cookie);
 if(e)return KGFinish(d,e);
 uint64_t total=0;
 for(uint32_t i=0;i<n;i++) {
  if(!KGWindow(role,r[i].iova,r[i].bytes) || r[i].bytes>1048576 || !(r[i].permissions&KG_READ) ||
     (r[i].permissions&~15u) || r[i].initial_bytes>r[i].bytes || (r[i].initial_bytes && !r[i].initial))return KGFinish(d,KG_INVALID);
  total+=r[i].bytes;if(total>4194304)return KGFinish(d,KG_INVALID);
  for(uint32_t j=0;j<i;j++)if(KGOverlap(r[i].iova,r[i].bytes,r[j].iova,r[j].bytes))return KGFinish(d,KG_INVALID);
  d->region[i]=r[i];
 }
 for(uint32_t i=0;i<=n;i++) {
  uint32_t bytes=i?((r[i-1].bytes+16383)&~16383u):KG_TABLE_BYTES;
  e=o->alloc(o->cookie,i,bytes,&d->pool[i]);if(e)goto fail;
  e=KGPoolValid(d,&d->pool[i],bytes);if(e)goto fail;
  for(uint32_t j=0;j<i;j++)if(KGOverlap(d->pool[i].pa,bytes,d->pool[j].pa,d->pool[j].bytes)){e=KG_POOL;goto fail;}
  d->receipt.pools++;
  if(i){memset(d->pool[i].address,0,bytes);if(r[i-1].initial_bytes)memcpy(d->pool[i].address,r[i-1].initial,r[i-1].initial_bytes);}
 }
 e=KGBuild(d);if(e)goto fail;
 for(uint32_t i=0;i<n;i++)for(uint32_t off=0;off<r[i].bytes;off+=4096) {
  uint64_t pa,attrs;e=KGTranslate(d,r[i].iova+off,&pa,&attrs);
  if(e || pa!=d->pool[i+1].pa+off || attrs!=KGAttrs(r[i].permissions)){e=KG_POOL;goto fail;}
 }
 for(uint32_t i=0;i<=n;i++){e=o->clean_poc(o->cookie,&d->pool[i]);if(e)goto fail;}
 o->barrier(o->cookie);d->receipt.state=KG_PREPARED;return KGFinish(d,KG_OK);
fail:
 d->receipt.cleanup_error=KGDispose(d);return KGFinish(d,e);
}
/* Independent read-only acquisition. It never reuses a cached receipt as a lease. */
static int KebabGpuSmmuDiscover(KGDomain *d)
{
 if(!d || d->magic!=0x3153474b)return KG_STATE;
 if(!KGTry(d))return KG_BUSY;
 if(d->receipt.state!=KG_PREPARED && d->receipt.state!=KG_RESTORED && d->receipt.state!=KG_OBSERVER){KGUnlock(d);return KG_STATE;}
 int e=d->ops.worker(d->ops.cookie);if(e)return KGFinish(d,e);
 e=d->ops.acquire(d->ops.cookie,d->receipt.role,0,&d->lease);
 if(e){if(d->lease.held)d->receipt.cleanup_error=KG_OWNERSHIP;return KGFinish(d,e);}
 e=KGCheck(d,KG_ACCESS);if(e)return KGFinish(d,e);
 e=d->ops.map(d->ops.cookie,&d->lease);if(e)return KGFinish(d,e);d->mapped=1;
 e=KGDiscoverLocked(d,&d->work);return KGFinish(d,e);
}
static int KebabGpuSmmuBind(KGDomain *d)
{
 if(!d || d->magic!=0x3153474b)return KG_STATE;
 if(!KGTry(d))return KG_BUSY;
 if(d->receipt.state!=KG_PREPARED && d->receipt.state!=KG_RESTORED){KGUnlock(d);return KG_STATE;}
 int e=d->ops.worker(d->ops.cookie);if(e)return KGFinish(d,e);
 e=d->ops.acquire(d->ops.cookie,d->receipt.role,1,&d->lease);
 if(e){if(d->lease.held)d->receipt.cleanup_error=KG_OWNERSHIP;return KGFinish(d,e);}
 e=KGCheck(d,KG_EXCLUSIVE);if(e)return KGFinish(d,e);
 if(d->lease.role!=d->receipt.role || d->lease.cb>=5 || d->lease.asid!=d->lease.cb+1)return KGFinish(d,KG_OWNERSHIP);
 e=d->ops.quiesce(d->ops.cookie,&d->lease);if(e)return KGFinish(d,e);
 e=KGCheck(d,KG_BIND);if(e)return KGFinish(d,e);
 e=d->ops.map(d->ops.cookie,&d->lease);if(e)return KGFinish(d,e);d->mapped=1;
 e=KGDiscoverLocked(d,&d->before);if(e)return KGFinish(d,e);
 e=KGRouteCheck(&d->before,&d->lease,1);if(e)return KGFinish(d,e);d->saved_valid=1;
 for(uint32_t i=0;i<=d->count;i++) {
  e=KGPoolValid(d,&d->pool[i],d->pool[i].bytes);if(e)return KGFinish(d,e);
 }
 e=KGBuild(d);if(e)return KGFinish(d,e);
 for(uint32_t i=0;i<=d->count;i++) {
  e=d->ops.clean_poc(d->ops.cookie,&d->pool[i]);if(e)return KGFinish(d,e);
 }
 d->ops.barrier(d->ops.cookie);
 e=KGDiscoverLocked(d,&d->work);if(e)return KGFinish(d,e);
 e=KGRouteCheck(&d->work,&d->lease,1);if(e)return KGFinish(d,e);
 if(!KGSameRouting(&d->before,&d->work,d->lease.cb) ||
    memcmp(&d->before.cb[d->lease.cb],&d->work.cb[d->lease.cb],sizeof(KGContext)))return KGFinish(d,KG_OWNERSHIP);
 /* Previous non-global translations must be gone before changing TTBR/TCR. */
 e=KGTlb(d);if(e)return KGFinish(d,e);
 KGContext next=d->before.cb[d->lease.cb];
 next.tcr=0x00802020; /* EPD1, outer-shareable NC walks, 32-bit IOVA, 4K */
 next.tcr2=0x00038011; /* 36-bit output, AS16, SEP_UPSTREAM (vendor) */
 next.ttbr0=d->pool[0].pa|((uint64_t)d->lease.asid<<48);
 next.ttbr1=(uint64_t)d->lease.asid<<48;next.mair0=0x44;next.mair1=0;
 /* Abort on fault, no stall and no unowned interrupt handler. */
 next.sctlr=(1u<<12)|(3u<<22)|(1u<<5)|(1u<<2)|(1u<<1);
 e=KGProgram(d,&next);if(e)return KGFinish(d,e);
 e=KGVerify(d,&next,d->before.cb[d->lease.cb].sctlr);if(e)return KGFinish(d,e);
 e=KGW(d,0x8000+d->lease.cb*4096,next.sctlr|1,0);if(e)return KGFinish(d,e);
 d->ops.barrier(d->ops.cookie);e=KGVerify(d,&next,next.sctlr|1);if(e)return KGFinish(d,e);
 uint32_t fsr;e=KGR32(d,0x8058+d->lease.cb*4096,&fsr);if(e)return KGFinish(d,e);
 if(fsr)return KGFinish(d,KG_FAULT);
 d->receipt.state=KG_CONFIGURED_QUIESCED;d->receipt.generation=d->lease.generation;
 return KGFinish(d,KG_OK); /* Retained pinned pools + lease; streams still held. */
}
static int KebabGpuSmmuFault(KGDomain *d)
{
 if(!d || d->magic!=0x3153474b)return KG_STATE;
 if(!KGTry(d))return KG_BUSY;
 if(d->receipt.state!=KG_CONFIGURED_QUIESCED){KGUnlock(d);return KG_STATE;}
 int e=d->ops.worker(d->ops.cookie);uint32_t b=0x8000+d->lease.cb*4096;
 if(!e)e=KGR32(d,b+0x58,&d->receipt.fault_first);
 if(!e)e=KGR64(d,b+0x60,&d->receipt.fault_far);
 if(!e)e=KGR32(d,b+0x68,&d->receipt.fsynr0);
 if(!e)e=KGR32(d,b+0x6c,&d->receipt.fsynr1);
 if(!e)e=KGR32(d,0x1400+4*d->lease.cb,&d->receipt.frsynra);
 if(!e)e=KGR32(d,b+0x58,&d->receipt.fault_last);
 d->receipt.fault_unstable=d->receipt.fault_first!=d->receipt.fault_last;
 if(!e && (d->receipt.fault_first || d->receipt.fault_last))e=KG_FAULT;
 /* Observation cannot silently recover or free live resources. */
 d->receipt.result=e;KGUnlock(d);return e;
}
static int KebabGpuSmmuStop(KGDomain *d)
{
 if(!d || d->magic!=0x3153474b)return KG_STATE;
 if(!KGTry(d))return KG_BUSY;
 if(d->receipt.state!=KG_CONFIGURED_QUIESCED){KGUnlock(d);return KG_STATE;}
 int e=d->ops.worker(d->ops.cookie);if(!e)e=KGRestoreLocked(d);
 if(e){d->receipt.cleanup_error=e;d->receipt.state=KG_QUARANTINED;}
 return KGFinish(d,e);
}
static int KebabGpuSmmuRelease(KGDomain *d)
{
 if(!d || d->magic!=0x3153474b)return KG_STATE;
 if(!KGTry(d))return KG_BUSY;
 if((d->receipt.state!=KG_PREPARED && d->receipt.state!=KG_RESTORED) || d->touched || d->lease.held){KGUnlock(d);return KG_STATE;}
 int e=d->ops.worker(d->ops.cookie);if(e)return KGFinish(d,e);
 /* Device writes, if a later firmware owner ever unquiesces, must be synced
  * for the CPU after proven stop. No pool access is exposed while configured. */
 for(uint32_t i=0;i<=d->count;i++){e=d->ops.clean_poc(d->ops.cookie,&d->pool[i]);if(e)return KGFinish(d,e);}
 d->receipt.cleanup_error=KGDispose(d);if(d->receipt.cleanup_error)return KGFinish(d,KG_POOL);
 d->receipt.state=KG_RELEASED;return KGFinish(d,KG_OK);
}
static int KebabGpuSmmuSnapshot(KGDomain *d,KGReceipt *r)
{
 if(!d || !r)return KG_INVALID;if(!KGTry(d))return KG_BUSY;
 *r=d->receipt;KGUnlock(d);return KG_OK;
}
static int KebabGpuSmmuDiscoverySnapshot(KGDomain *d,KGDiscovery *out)
{
 if(!d || !out)return KG_INVALID;if(!KGTry(d))return KG_BUSY;
 if(!d->work.num_streams){KGUnlock(d);return KG_STATE;}
 *out=d->work;KGUnlock(d);return KG_OK;
}
#undef KG_TRY
#endif
