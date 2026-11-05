/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_FIRST_ATTACH_CORE_H
#define KEBAB_GPU_FIRST_ATTACH_CORE_H
#include "KebabGpuSmmuCore.h"

/* Fixed A650 cold profile: SID5 kernel memory, SID4 deliberately FAULT with
 * no mirrored RAM. All pins/leases are kernel-private and pointer identified.
 * No global reset, ATS, CB5..7 access, fault ACK or RESUME exists here. */
enum { KF_EMPTY=0,KF_RESERVED=1,KF_ATTACHED_QUIET=2,KF_RUN_ALLOWED=3,
 KF_RESTORED=4,KF_FAILED=5,KF_QUARANTINED=6,KF_PARKED=7 };
enum { KF_ACCESS=1,KF_COLD=2,KF_DRAINED=4,KF_RUN=8 };
enum { KF_VENDOR_NG_COMPAT=1,KF_NS_VISIBLE_COMPAT=2 };
typedef struct {
 uint32_t schema,ready,id0,id1,id2,cr0,cb,kernel_smr,user_smr,vmid;
 uint32_t namespace_mode,controlled_cb_mask,absent_cb_mask,no_locked,initialize;
 uint8_t fdt_sha256[32],source_contract_sha256[32],ro_receipt_sha256[32];
} KFPolicy;
typedef struct {uint64_t epoch,sequence;uint32_t slot,held,cb,kernel_smr,user_smr;} KFToken;
typedef struct {
 uint32_t busy,initialized,mutation,quarantined;uint64_t epoch,sequence;
 KFToken *slot[2];uint64_t slot_sequence[2];uint32_t cb_mask,smr_mask[4];
} KFNamespace;
typedef struct {
 uint64_t generation,root_pa;uintptr_t table_identity;
 uint32_t table_bytes,regions,pages,cleaned;uint64_t region_pa[6];
 uint32_t region_iova[6],region_bytes[6],region_permissions[6],allocation_bytes[6];
 uintptr_t region_identity[6];
} KFBacking;
typedef struct {
 void *cookie;int (*worker)(void *);
 int (*hold)(void *,uint32_t *held);int (*check)(void *,uint32_t);int (*drain)(void *);int (*end)(void *);
 int (*backing_acquire)(void *,KFBacking *,uint32_t *held);int (*backing_check)(void *,const KFBacking *);
 int (*backing_publish)(void *);int (*backing_end)(void *);
 int (*map)(void *);void (*unmap)(void *);
 int (*read32)(void *,uint32_t,uint32_t *);int (*read64)(void *,uint32_t,uint64_t *);
 int (*write32)(void *,uint32_t,uint32_t);int (*write64)(void *,uint32_t,uint64_t);
 void (*barrier)(void *);void (*delay)(void *);
} KFOps;
typedef struct {
 uint32_t magic,schema,bytes,result,state,primary_error,cleanup_error;
 uint32_t reads,writes,tlb_polls,touched,published,publish_attempted,held,backing_held,reserved;
 uint32_t cb,kernel_smr,user_smr,asid,vmid,fault,dma_proven,fsynr0,fsynr1,frsynra,fault_last;
 uint64_t namespace_epoch,backing_generation,root_pa,fault_far;
} KFReceipt;
typedef struct {
 uint32_t busy,magic,mapped,saved,held,backing_held,published,publish_attempted,touched,cb_modified,run_permitted;
 KFNamespace *space;KFPolicy policy;KFToken token;KFOps ops;KFBacking backing;
 KFReceipt receipt;KGDiscovery before,work;KGContext programmed;
 uint32_t expected_smr[2],expected_s2cr[2];
} KFFirstAttach;
static int KFTry(uint32_t *v){return __sync_bool_compare_and_swap(v,0,1);}
static void KFUnlock(uint32_t *v){__sync_lock_release(v);}
static int KFNonzero(const uint8_t *p){uint8_t v=0;for(unsigned i=0;i<32;i++)v|=p[i];return !!v;}
static int KFPolicyBound(const KFPolicy *p)
{
 return p && p->schema==1 && p->ready==1 && p->cb<5 && p->kernel_smr<128 && p->user_smr<128 &&
 p->kernel_smr!=p->user_smr && p->vmid<=65535 && p->no_locked==1 && p->initialize<=1 &&
 (p->namespace_mode==KF_VENDOR_NG_COMPAT || p->namespace_mode==KF_NS_VISIBLE_COMPAT) &&
 !((p->controlled_cb_mask|p->absent_cb_mask)&~31u) && !(p->controlled_cb_mask&p->absent_cb_mask) &&
 KFNonzero(p->fdt_sha256) && KFNonzero(p->source_contract_sha256) && KFNonzero(p->ro_receipt_sha256);
}
static int KFNamespaceInit(KFNamespace *s)
{
 if(!s)return KG_INVALID;if(!KFTry(&s->busy))return KG_BUSY;
 if(s->initialized){KFUnlock(&s->busy);return KG_STATE;}s->initialized=1;
 __atomic_store_n(&s->epoch,1,__ATOMIC_RELEASE);KFUnlock(&s->busy);return 0;
}
/* Private generation check is safe under canonical VRM globalbusy: no MMIO,
 * BUS callback or SMMU busy acquisition. It is permission, never DMA proof. */
static int KFNamespaceCheckPrivate(const KFNamespace *s,const KFToken *t)
{
 if(!s || !t || !__atomic_load_n(&t->held,__ATOMIC_ACQUIRE) || t->slot>=2 || !t->epoch || !t->sequence ||
 __atomic_load_n(&s->quarantined,__ATOMIC_ACQUIRE) || __atomic_load_n(&s->mutation,__ATOMIC_ACQUIRE) ||
 __atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE)!=t->epoch ||
 __atomic_load_n(&s->slot[t->slot],__ATOMIC_ACQUIRE)!=t || __atomic_load_n(&s->slot_sequence[t->slot],__ATOMIC_ACQUIRE)!=t->sequence)return KG_OWNERSHIP;
 return 0;
}
static int KFReserve(KFNamespace *s,KFToken *t,const KFPolicy *p)
{
 if(!s || !t || !KFPolicyBound(p))return KG_CLOSED;if(!KFTry(&s->busy))return KG_BUSY;
 int e=KG_CLOSED;unsigned slot;
 if(!s->initialized || s->mutation || __atomic_load_n(&s->quarantined,__ATOMIC_ACQUIRE) || !__atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE))goto done;
 if(t->held || t->sequence || (s->cb_mask&(1u<<p->cb)) ||
 (s->smr_mask[p->kernel_smr/32]&(1u<<(p->kernel_smr%32))) ||
 (s->smr_mask[p->user_smr/32]&(1u<<(p->user_smr%32)))){e=KG_BUSY;goto done;}
 for(slot=0;slot<2 && s->slot[slot];slot++){}if(slot==2){e=KG_BUSY;goto done;}
 if(s->sequence==UINT64_MAX){e=KG_STATE;goto done;}
 memset(t,0,sizeof *t);t->epoch=__atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE);t->sequence=++s->sequence;t->slot=slot;t->held=1;
 t->cb=p->cb;t->kernel_smr=p->kernel_smr;t->user_smr=p->user_smr;__atomic_store_n(&s->slot_sequence[slot],t->sequence,__ATOMIC_RELEASE);
 s->cb_mask|=1u<<p->cb;s->smr_mask[p->kernel_smr/32]|=1u<<(p->kernel_smr%32);
 s->smr_mask[p->user_smr/32]|=1u<<(p->user_smr%32);
 __atomic_store_n(&s->slot[slot],t,__ATOMIC_RELEASE);e=0;
done:KFUnlock(&s->busy);return e;
}
static int KFUnreserve(KFNamespace *s,KFToken *t)
{
 if(!s || !t)return KG_INVALID;if(!KFTry(&s->busy))return KG_BUSY;
 int e=KFNamespaceCheckPrivate(s,t);if(!e){
 __atomic_store_n(&s->slot[t->slot],(KFToken *)NULL,__ATOMIC_RELEASE);__atomic_store_n(&s->slot_sequence[t->slot],0,__ATOMIC_RELEASE);
 s->cb_mask&=~(1u<<t->cb);s->smr_mask[t->kernel_smr/32]&=~(1u<<(t->kernel_smr%32));
 s->smr_mask[t->user_smr/32]&=~(1u<<(t->user_smr%32));__atomic_store_n(&t->held,0,__ATOMIC_RELEASE);}
 KFUnlock(&s->busy);return e;
}
/* Only external SMMU configuration/target-memory ownership mutations enter
 * this gate, BEFORE SCM/VRM locks. Busy has no side effects. */
static int KFMutationBegin(KFNamespace *s)
{
 if(!s)return KG_INVALID;if(!KFTry(&s->busy))return KG_BUSY;int e=KG_BUSY;
 if(s->initialized && !s->mutation && !__atomic_load_n(&s->quarantined,__ATOMIC_ACQUIRE) && !s->slot[0] && !s->slot[1] && __atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE)<UINT64_MAX){
  __atomic_store_n(&s->mutation,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->epoch,1,__ATOMIC_ACQ_REL);e=0;}
 KFUnlock(&s->busy);return e;
}
static int KFMutationEnd(KFNamespace *s)
{
 if(!s)return KG_INVALID;if(!KFTry(&s->busy))return KG_BUSY;int e=KG_STATE;
 if(s->mutation && !s->slot[0] && !s->slot[1]){__atomic_store_n(&s->mutation,0,__ATOMIC_RELEASE);e=0;}
 KFUnlock(&s->busy);return e;
}
enum { KF_OP_INFO=0,KF_OP_ROOT_QSEE=1,KF_OP_PAS13_SEPARATE_IMAGE=2,
 KF_OP_OWN_CM3_RESET=3,KF_OP_EXTERNAL_SMMU_CONFIG=4,KF_OP_TARGET_MEMORY_REASSIGN=5,
 KF_OP_PRODUCER_OWNERSHIP_CHANGE=6 };
static int KFMutationBeginFor(KFNamespace *s,unsigned operation,uint32_t *started)
{
 if(!started || operation>KF_OP_PRODUCER_OWNERSHIP_CHANGE)return KG_INVALID;*started=0;
 /* PAS13 only remains separate if its exact memory intervals do not change
  * the six backing/table allocations. The caller proves that range fact. */
 if(operation<=KF_OP_OWN_CM3_RESET)return 0;
 int e=KFMutationBegin(s);if(!e)*started=1;return e;
}
/* Emergency epoch revocation never releases pins or authorizes a reset. */
static void KFNamespaceRevoke(KFNamespace *s)
{
 if(s){__atomic_store_n(&s->quarantined,1,__ATOMIC_RELEASE);uint64_t old=__atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE);
 if(old<UINT64_MAX)__atomic_compare_exchange_n(&s->epoch,&old,old+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
}
static int KFCheck(KFFirstAttach *a,uint32_t flags)
{
 int e=KFNamespaceCheckPrivate(a->space,&a->token);if(e)return e;
 if(!a->held)return KG_CLOSED;e=a->ops.check(a->ops.cookie,flags);if(e)return e;
 if(a->backing_held)return a->ops.backing_check(a->ops.cookie,&a->backing);return 0;
}
static int KFR32(KFFirstAttach *a,uint32_t off,uint32_t *v)
{
 if(!KGReadAllowed(off,0,31))return KG_INVALID;int e=KFCheck(a,KF_ACCESS);if(e)return e;
 a->receipt.reads++;return a->ops.read32(a->ops.cookie,off,v);
}
static int KFR64(KFFirstAttach *a,uint32_t off,uint64_t *v)
{
 if(!KGReadAllowed(off,1,31))return KG_INVALID;int e=KFCheck(a,KF_ACCESS);if(e)return e;
 a->receipt.reads++;return a->ops.read64(a->ops.cookie,off,v);
}
static int KFWriteAllowed(const KFFirstAttach *a,uint32_t off,uint32_t wide)
{
 if(a->policy.cb>=5)return 0;
 if(!wide && (off==0x800+4*a->policy.kernel_smr || off==0x800+4*a->policy.user_smr ||
 off==0xc00+4*a->policy.kernel_smr || off==0xc00+4*a->policy.user_smr))return 1;
 if(a->policy.initialize && !wide && (off==0x1000+4*a->policy.cb || off==0x1800+4*a->policy.cb || off==0x8004+4096*a->policy.cb))return 1;
 return KGWriteAllowed(off,wide,a->policy.cb) || (!wide && off==0x8618+4096*a->policy.cb);
}
static int KFW(KFFirstAttach *a,uint32_t off,uint64_t v,uint32_t wide)
{
 if(!KFWriteAllowed(a,off,wide))return KG_INVALID;int e=KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return e;
 a->touched=1;a->receipt.writes++;e=wide?a->ops.write64(a->ops.cookie,off,v):a->ops.write32(a->ops.cookie,off,(uint32_t)v);
 return e?e:KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED);
}
#define KF_TRY(x) do{int kf_e=(x);if(kf_e)return kf_e;}while(0)
/* Arm IHI0062D.c9.6.3: MemAttr[19:16] resets UNKNOWN and is inactive
 * while MTCFG[20]==0. Only that inactive cold-profile field may vary.
 * Every other bit, active MemAttr and complete same-boot stability stay
 * exact. This function neither writes CR0 nor authorizes bypass. */
static int KFColdCR0Matches(uint32_t actual,uint32_t expected)
{
 if((actual|expected)&0x00100000u)return actual==expected;
 return !((actual^expected)&~0x000f0000u);
}
static int KFSnapshot(KFFirstAttach *a,KGDiscovery *s)
{
 memset(s,0,sizeof *s);KF_TRY(KFR32(a,0x20,&s->id0));KF_TRY(KFR32(a,0x24,&s->id1));KF_TRY(KFR32(a,0x28,&s->id2));
 s->num_streams=s->id0&255;s->num_cb=s->id1&255;s->num_s2=(s->id1>>16)&255;
 if(s->id0!=a->policy.id0 || s->id1!=a->policy.id1 || s->id2!=a->policy.id2 ||
 !(s->id0&(1u<<30)) || !(s->id0&(1u<<27)) || (s->id0&256) || !(s->id2&4096) ||
 !s->num_streams || s->num_streams>128 || !s->num_cb || s->num_cb>8 || s->num_s2>s->num_cb ||
 (s->id1&0xf0000000u)!=0x20000000u || ((s->id2>>4)&15)<1 || ((s->id2>>4)&15)>5 || (s->id2&15)<1 || (s->id2&15)>5)return KG_UNSUPPORTED;
 static const uint32_t widths[6]={32,36,40,42,44,48};
 s->stride=4096;s->cb_base=0x8000;s->pa_bits=widths[(s->id2>>4)&15];s->ipa_bits=widths[s->id2&15];
 uint32_t ubs=(s->id2>>8)&15;s->va_bits=ubs<6?widths[ubs]:0;if(s->va_bits<32)return KG_UNSUPPORTED;
 s->read_mask=((1u<<s->num_cb)-1)&31u;
 KF_TRY(KFR32(a,0,&s->cr0));if(!KFColdCR0Matches(s->cr0,a->policy.cr0) || (s->cr0&9))return KG_OWNERSHIP;
 KF_TRY(KFR32(a,0x48,&s->gfsr));for(unsigned j=0;j<3;j++)KF_TRY(KFR32(a,0x50+4*j,&s->gfsynr[j]));
 for(unsigned i=0;i<s->num_streams;i++){KF_TRY(KFR32(a,0x800+4*i,&s->stream[i].smr));KF_TRY(KFR32(a,0xc00+4*i,&s->stream[i].s2cr));}
 for(unsigned i=0;i<5;i++)if(s->read_mask&(1u<<i)){
  KGContext *c=&s->cb[i];uint32_t b=0x8000+4096*i;c->present=1;
  KF_TRY(KFR32(a,0x1000+4*i,&c->cbar));KF_TRY(KFR32(a,0x1800+4*i,&c->cba2r));
  c->type=(c->cbar>>16)&3;c->hypc=(c->cbar>>10)&1;c->monc=(c->cba2r>>1)&1;
  c->vmid=(s->cr0&0x80000000u)?c->cba2r>>16:c->cbar&255;
  KF_TRY(KFR32(a,b,&c->sctlr));KF_TRY(KFR32(a,b+4,&c->actlr));KF_TRY(KFR32(a,b+0x10,&c->tcr2));KF_TRY(KFR32(a,b+0x30,&c->tcr));
  KF_TRY(KFR64(a,b+0x20,&c->ttbr0));KF_TRY(KFR64(a,b+0x28,&c->ttbr1));
  KF_TRY(KFR32(a,b+0x38,&c->mair0));KF_TRY(KFR32(a,b+0x3c,&c->mair1));KF_TRY(KFR32(a,b+0x58,&c->fsr));
  KF_TRY(KFR64(a,b+0x60,&c->far));KF_TRY(KFR32(a,b+0x68,&c->fsynr0));KF_TRY(KFR32(a,b+0x6c,&c->fsynr1));KF_TRY(KFR32(a,0x1400+4*i,&c->frsynra));
 }
 return 0;
}
/* ARM IHI0062D.c 16.5.9: FSR[10:9] is read-only Format, not fault
 * status (0x400 means AArch64). Exact pinned vendor FSR_FAULT bitmap:
 * MULTI31 | SS30 | UUT8 | ASF7 | TLBLKF6 | TLBMCF5 | EF4 | PF3 | AFF2 | TF1.
 * Preserve every raw bit in snapshots; never ACK/RESUME here. */
static uint32_t KFContextFaultBits(uint32_t raw){return raw&0xc00001feu;}
static int KFIntersects(uint32_t smr,uint32_t sid)
{return (smr&0x80000000u) && !(((smr&0x7fff)^sid)&~(((smr>>16)&0x7fff)|0x400u)&0x7fff);}
static int KFValidateSnapshot(KFFirstAttach *a,const KGDiscovery *s)
{
 const KFPolicy *p=&a->policy;const KGContext *c=&s->cb[p->cb];
 if(p->initialize){
  /* Actual574 has no active NS route and five virgin ordinary CBs. Unknown
   * disabled-context words are observations, never valid inherited config. */
  if(s->num_cb>5 || s->num_s2 || p->cb>=s->num_cb || s->gfsr || p->vmid || !(s->cr0&(1u<<10)) ||
  (s->cr0&0x80000000u) || p->kernel_smr>=s->num_streams || p->user_smr>=s->num_streams)return KG_OWNERSHIP;
  for(unsigned i=0;i<s->num_streams;i++)if(s->stream[i].smr&0x80000000u)return KG_ROUTE;
  for(unsigned cb=0;cb<s->num_cb;cb++){
   const KGContext *o=&s->cb[cb];if(!o->present || o->type || o->hypc || o->monc || (o->cba2r&1) ||
   o->vmid || (o->sctlr&1) || KFContextFaultBits(o->fsr))return KG_OWNERSHIP;
  }
  return c->actlr==3?0:KG_OWNERSHIP;
 }
 if(p->cb>=s->num_cb || p->cb<s->num_s2 || !c->present || s->gfsr || KFContextFaultBits(c->fsr) || (c->sctlr&1) ||
 c->type!=1 || c->hypc || c->monc || (c->cba2r&3)!=1 || c->actlr!=0x32b || c->vmid!=p->vmid ||
 p->kernel_smr>=s->num_streams || p->user_smr>=s->num_streams)return KG_OWNERSHIP;
 if(p->namespace_mode==KF_NS_VISIBLE_COMPAT && s->num_cb>5)return KG_OWNERSHIP;
 for(unsigned cb=0;cb<5;cb++)if(s->read_mask&(1u<<cb)){
  const KGContext *o=&s->cb[cb];if(cb==p->cb || o->type!=1 || o->hypc || o->monc || o->vmid!=p->vmid)continue;
  if((o->sctlr&1) && ((o->ttbr0>>48)==p->cb+1 || (o->ttbr1>>48)==p->cb+1))return KG_OWNERSHIP;
  if(!(p->controlled_cb_mask&(1u<<cb)) && !(p->absent_cb_mask&(1u<<cb)))return KG_OWNERSHIP;
 }
 unsigned match[2]={0,0};
 for(unsigned i=0;i<s->num_streams;i++){
  uint32_t smr=s->stream[i].smr,s2cr=s->stream[i].s2cr,type=(s2cr>>16)&3;
  for(unsigned r=0;r<2;r++){
   uint32_t slot=r?p->user_smr:p->kernel_smr,sid=r?4:5;
   if(KFIntersects(smr,sid)){
    if(i!=slot || ((smr>>16)&0x7fff)!=0x400 || ((smr&0x7fff)&~0x400u)!=sid || type==3)return KG_ROUTE;
    if(type==0 && ((s2cr&255)>=5 || (!r && (s2cr&255)!=p->cb)))return KG_OWNERSHIP;
    match[r]++;
   }else if(i==slot && (smr&0x80000000u))return KG_ROUTE;
  }
  if((smr&0x80000000u) && type==0 && (s2cr&255)==p->cb && i!=p->kernel_smr)return KG_ROUTE;
 }
 if(match[0]>1 || match[1]>1)return KG_ROUTE;return 0;
}
static int KFSameUnowned(const KFFirstAttach *a,const KGDiscovery *s)
{
 const KGDiscovery *o=&a->before;if(s->id0!=o->id0 || s->id1!=o->id1 || s->id2!=o->id2 || s->cr0!=o->cr0 || s->num_streams!=o->num_streams)return 0;
 for(unsigned i=0;i<s->num_streams;i++)if(i!=a->policy.kernel_smr && i!=a->policy.user_smr){
  if(a->policy.initialize){if(s->stream[i].smr&0x80000000u)return 0;}
  else if(memcmp(&s->stream[i],&o->stream[i],sizeof(KGStream)))return 0;
 }
 const KGContext *c=&s->cb[a->policy.cb],*b=&o->cb[a->policy.cb];
 if(a->policy.initialize){
  for(unsigned cb=0;cb<s->num_cb;cb++)if(cb!=a->policy.cb){const KGContext *u=&s->cb[cb];
   if(u->type || u->hypc || u->monc || u->vmid || (u->cba2r&1) || (u->sctlr&1) || KFContextFaultBits(u->fsr))return 0;
  }
  return !c->hypc && !c->monc && !c->vmid && c->type<=1 && !KFContextFaultBits(c->fsr) && !s->gfsr;
 }
 return c->cbar==b->cbar && c->cba2r==b->cba2r && c->actlr==b->actlr && !KFContextFaultBits(c->fsr) && !s->gfsr;
}
static int KFRouteWrite(KFFirstAttach *a,unsigned which,uint32_t smr,uint32_t s2cr)
{
 uint32_t i=which?a->policy.user_smr:a->policy.kernel_smr,v;
 /* S2CR first; while new matching SMR appears, traffic can only fault. */
 KF_TRY(KFW(a,0xc00+4*i,s2cr,0));a->ops.barrier(a->ops.cookie);KF_TRY(KFR32(a,0xc00+4*i,&v));if((v^s2cr)&(a->policy.initialize?0x030300ffu:UINT32_MAX))return KG_IO;
 KF_TRY(KFW(a,0x800+4*i,smr,0));a->ops.barrier(a->ops.cookie);KF_TRY(KFR32(a,0x800+4*i,&v));
 if((v^smr)&(a->policy.initialize?((smr&0x80000000u)?0xffff7fffu:0x80000000u):UINT32_MAX))return KG_IO;
 a->expected_smr[which]=smr;a->expected_s2cr[which]=s2cr;return 0;
}
static int KFTlb(KFFirstAttach *a)
{
 uint32_t b=0x8000+4096*a->policy.cb,v;
 /* NS VMID-wide ALL clears inherited globals/old ASID; not a CB-local reset. */
 KF_TRY(KFW(a,b+0x618,0,0));a->ops.barrier(a->ops.cookie);
 KF_TRY(KFW(a,b+0x7f0,0,0));a->ops.barrier(a->ops.cookie);
 for(unsigned i=0;i<KG_POLL_LIMIT;i++){KF_TRY(KFR32(a,b+0x7f4,&v));a->receipt.tlb_polls++;if(!(v&1))return 0;a->ops.delay(a->ops.cookie);}return KG_TIMEOUT;
}
static int KFContext(KFFirstAttach *a,const KGContext *c)
{
 uint32_t b=0x8000+4096*a->policy.cb;
 KF_TRY(KFW(a,b+0x10,c->tcr2,0));KF_TRY(KFW(a,b+0x30,c->tcr,0));
 KF_TRY(KFW(a,b+0x20,c->ttbr0,1));KF_TRY(KFW(a,b+0x28,c->ttbr1,1));
 KF_TRY(KFW(a,b+0x38,c->mair0,0));KF_TRY(KFW(a,b+0x3c,c->mair1,0));
 a->ops.barrier(a->ops.cookie);return 0;
}
static int KFContextVerify(KFFirstAttach *a,const KGContext *c,uint32_t sctlr)
{
 uint32_t b=0x8000+4096*a->policy.cb,v;uint64_t q;
 KF_TRY(KFR32(a,b,&v));if((v^sctlr)&(a->policy.initialize?0x0fdf11f7u:UINT32_MAX))return KG_IO;
 if(a->policy.initialize){
  KF_TRY(KFR32(a,b+0x10,&v));if((v^c->tcr2)&0x00038017u)return KG_IO;
  KF_TRY(KFR32(a,b+0x30,&v));if((v^c->tcr)&0x00c0ffbfu)return KG_IO;
  KF_TRY(KFR64(a,b+0x20,&q));if((q^c->ttbr0)&(KG_PTE_PA|0xffff000000000000ULL))return KG_IO;
  KF_TRY(KFR32(a,b+0x38,&v));return ((v^c->mair0)&255)?KG_IO:0;
 }
 KF_TRY(KFR32(a,b+0x10,&v));if(v!=c->tcr2)return KG_IO;KF_TRY(KFR32(a,b+0x30,&v));if(v!=c->tcr)return KG_IO;
 KF_TRY(KFR64(a,b+0x20,&q));if(q!=c->ttbr0)return KG_IO;KF_TRY(KFR64(a,b+0x28,&q));if(q!=c->ttbr1)return KG_IO;
 KF_TRY(KFR32(a,b+0x38,&v));if(v!=c->mair0)return KG_IO;KF_TRY(KFR32(a,b+0x3c,&v));return v==c->mair1?0:KG_IO;
}
static int KFInitializeRegime(KFFirstAttach *a)
{
 uint32_t cb=a->policy.cb,b=0x8000+4096*cb,v;
 /* These fields belong to the virgin CB grant. Write defined software
  * values; never RMW UNKNOWN disabled TCR/SCTLR/S2CR reset words. */
 KF_TRY(KFW(a,b,0,0));a->ops.barrier(a->ops.cookie);KF_TRY(KFR32(a,b,&v));if(v&1)return KG_IO;
 KF_TRY(KFW(a,0x1800+4*cb,1,0));KF_TRY(KFW(a,0x1000+4*cb,0x1f300,0));
 KF_TRY(KFW(a,b+4,0x32b,0));a->ops.barrier(a->ops.cookie);
 KF_TRY(KFR32(a,0x1800+4*cb,&v));if((v&3)!=1)return KG_IO;
 KF_TRY(KFR32(a,0x1000+4*cb,&v));if((v&0x3ffff)!=0x1f300)return KG_IO;
 KF_TRY(KFR32(a,b+4,&v));if((v&0x32b)!=0x32b)return KG_IO;
 return 0;
}
static int KFParkInitial(KFFirstAttach *a)
{
 for(unsigned r=0;r<2;r++)KF_TRY(KFRouteWrite(a,r,0,0x20000));
 KF_TRY(KFInitializeRegime(a));KF_TRY(KFTlb(a));
 KGContext parked;memset(&parked,0,sizeof parked);parked.tcr=0x00802020;parked.tcr2=0x38011;
 parked.ttbr0=parked.ttbr1=(uint64_t)(a->policy.cb+1)<<48;parked.mair0=0x44;
 /* No private-broadcast ASIDPNE hint is requested. The defined mask
  * still verifies bit12==0; explicit local VMID maintenance remains used. */
 parked.sctlr=(3u<<22)|(1u<<5)|(1u<<2)|(1u<<1);
 KF_TRY(KFContext(a,&parked));KF_TRY(KFW(a,0x8000+4096*a->policy.cb,parked.sctlr,0));
 a->ops.barrier(a->ops.cookie);KF_TRY(KFContextVerify(a,&parked,parked.sctlr));KF_TRY(KFTlb(a));
 KF_TRY(KFSnapshot(a,&a->work));if(!KFSameUnowned(a,&a->work))return KG_OWNERSHIP;
 a->touched=0;a->receipt.state=KF_PARKED;return 0;
}
static int KFRestore(KFFirstAttach *a)
{
 if(!a->saved)return KG_STATE;KF_TRY(a->ops.drain(a->ops.cookie));KF_TRY(KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED));
 KF_TRY(KFSnapshot(a,&a->work));if(!KFSameUnowned(a,&a->work))return KG_OWNERSHIP;
 if(a->policy.initialize)return KFParkInitial(a);
 uint32_t cb=a->policy.cb,b=0x8000+4096*cb;
 for(unsigned r=0;r<2;r++){
  unsigned i=r?a->policy.user_smr:a->policy.kernel_smr;
  uint32_t old=a->before.stream[i].s2cr,now=a->work.stream[i].s2cr;
  /* The only new fields are CBNDX/TYPE/PRIVCFG. Never overwrite a foreign
   * SHCFG/implementation field or an unexpected StreamID match. */
  if((now&~0x030300ffu)!=(old&~0x030300ffu))return KG_OWNERSHIP;
  uint32_t sm=a->work.stream[i].smr,expected=0x84000000u+(r?4:5);
  if(sm!=a->before.stream[i].smr && sm!=expected)return KG_OWNERSHIP;
  KF_TRY(KFRouteWrite(a,r,sm,(now&~0x030300ffu)|0x20000u));
 }
 uint32_t disabled=a->work.cb[cb].sctlr&~1u;KF_TRY(KFW(a,b,disabled,0));a->ops.barrier(a->ops.cookie);
 uint32_t v;KF_TRY(KFR32(a,b,&v));if(v!=disabled)return KG_IO;KF_TRY(KFTlb(a));
 KF_TRY(KFContext(a,&a->before.cb[cb]));KF_TRY(KFW(a,b,a->before.cb[cb].sctlr,0));a->ops.barrier(a->ops.cookie);
 KF_TRY(KFContextVerify(a,&a->before.cb[cb],a->before.cb[cb].sctlr));KF_TRY(KFTlb(a));
 for(unsigned r=0;r<2;r++){
  unsigned i=r?a->policy.user_smr:a->policy.kernel_smr;uint32_t old=a->before.stream[i].s2cr;
  /* Restore SMR while still faulting; only then restore original routing. */
  KF_TRY(KFRouteWrite(a,r,a->before.stream[i].smr,(old&~0x030300ffu)|0x20000u));
  KF_TRY(KFW(a,0xc00+4*i,old,0));a->ops.barrier(a->ops.cookie);KF_TRY(KFR32(a,0xc00+4*i,&v));if(v!=old)return KG_IO;
 }
 KF_TRY(KFSnapshot(a,&a->work));if(!KFSameUnowned(a,&a->work))return KG_OWNERSHIP;
 a->touched=0;a->receipt.state=KF_RESTORED;return 0;
}
static int KFFinish(KFFirstAttach *a,int error)
{
 if(error)__atomic_store_n(&a->run_permitted,0,__ATOMIC_RELEASE);
 a->receipt.primary_error=error;
 if(error && a->touched){int e=KFRestore(a);if(e){a->receipt.cleanup_error=e;a->receipt.state=KF_QUARANTINED;KFNamespaceRevoke(a->space);error=KG_QUARANTINE;}}
 if(error && !a->publish_attempted && !a->touched){
  if(a->mapped){a->ops.unmap(a->ops.cookie);a->mapped=0;}
  if(a->backing_held){int e=a->ops.backing_end(a->ops.cookie);if(e)a->receipt.cleanup_error=e;else a->backing_held=0;}
  if(a->held){int e=a->ops.end(a->ops.cookie);if(e)a->receipt.cleanup_error=e;else a->held=0;}
  if(!a->backing_held && !a->held && a->token.held){int e=KFUnreserve(a->space,&a->token);if(e)a->receipt.cleanup_error=e;}
  if(a->receipt.cleanup_error){a->receipt.state=KF_QUARANTINED;error=KG_QUARANTINE;}else a->receipt.state=KF_FAILED;
 }
 if(error && a->receipt.state!=KF_QUARANTINED && a->receipt.state!=KF_RESTORED && a->receipt.state!=KF_PARKED)a->receipt.state=KF_FAILED;
 a->receipt.result=error;a->receipt.touched=a->touched;a->receipt.published=a->published;a->receipt.publish_attempted=a->publish_attempted;a->receipt.held=a->held;
 a->receipt.backing_held=a->backing_held;a->receipt.reserved=a->token.held;a->receipt.namespace_epoch=a->token.epoch;
 KFUnlock(&a->busy);return error;
}
static int KFOpsValid(const KFOps *o)
{
 return o && o->worker && o->hold && o->check && o->drain && o->end && o->backing_acquire && o->backing_check && o->backing_publish && o->backing_end &&
 o->map && o->unmap && o->read32 && o->read64 && o->write32 && o->write64 && o->barrier && o->delay;
}
static int KFBackingValid(const KFBacking *b)
{
 static const uint32_t va[6]={0x4000,0x144000,0x157000,0x60000000,0x60001000,0x60006000};
 static const uint32_t bytes[6]={0x8000,0x13000,0x9000,0x1000,0x5000,0x1000};
 if(!b->generation || !b->table_identity || b->table_bytes!=65536 || (b->root_pa&4095) ||
 b->root_pa<0x20000 || b->root_pa>KG_PA_LIMIT-65536 || b->regions!=6 || b->pages!=43 || b->cleaned!=1)return KG_POOL;
 for(unsigned i=0;i<6;i++){
  uint32_t allocation=(bytes[i]+16383)&~16383u,permissions=i<3?15:11;
  if(b->region_iova[i]!=va[i] || b->region_bytes[i]!=bytes[i] || b->allocation_bytes[i]!=allocation ||
  b->region_permissions[i]!=permissions || !b->region_identity[i] || b->region_identity[i]==b->table_identity ||
  (b->region_pa[i]&4095) || b->region_pa[i]<0x20000 || b->region_pa[i]>KG_PA_LIMIT-allocation ||
  KGOverlap(b->region_pa[i],allocation,b->root_pa,65536))return KG_POOL;
  for(unsigned j=0;j<i;j++)if(b->region_identity[i]==b->region_identity[j] || KGOverlap(b->region_pa[i],allocation,b->region_pa[j],b->allocation_bytes[j]))return KG_POOL;
 }
 return 0;
}
static int KFFirstAttachCold(KFFirstAttach *a,KFNamespace *s,const KFPolicy *p,const KFOps *o)
{
 if(!a || !s || !p || !KFOpsValid(o))return KG_INVALID;if(!KFTry(&a->busy))return KG_BUSY;
 if(a->magic){KFUnlock(&a->busy);return KG_STATE;}__atomic_store_n(&a->magic,0x3141464b,__ATOMIC_RELEASE);a->ops=*o;a->policy=*p;a->space=s;
 a->receipt.magic=a->magic;a->receipt.schema=1;a->receipt.bytes=sizeof a->receipt;a->receipt.state=KF_EMPTY;
 a->receipt.cb=p->cb;a->receipt.kernel_smr=p->kernel_smr;a->receipt.user_smr=p->user_smr;a->receipt.asid=p->cb+1;a->receipt.vmid=p->vmid;
 int e=o->worker(o->cookie);if(e)return KFFinish(a,e);e=KFReserve(s,&a->token,p);if(e)return KFFinish(a,e);
 a->receipt.state=KF_RESERVED;e=o->hold(o->cookie,&a->held);if(e)return KFFinish(a,e);if(!a->held)return KFFinish(a,KG_OWNERSHIP);
 e=o->backing_acquire(o->cookie,&a->backing,&a->backing_held);if(e)return KFFinish(a,e);if(!a->backing_held)return KFFinish(a,KG_POOL);
 e=KFBackingValid(&a->backing);if(e)return KFFinish(a,e);
 e=o->drain(o->cookie);if(e)return KFFinish(a,e);e=KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return KFFinish(a,e);
 e=o->map(o->cookie);if(e)return KFFinish(a,e);a->mapped=1;e=KFSnapshot(a,&a->before);if(e)return KFFinish(a,e);
 e=KFValidateSnapshot(a,&a->before);if(e)return KFFinish(a,e);a->saved=1;
 e=KFSnapshot(a,&a->work);if(e)return KFFinish(a,e);e=KFValidateSnapshot(a,&a->work);if(e)return KFFinish(a,e);
 if(a->before.cr0!=a->work.cr0 || (!p->initialize && memcmp(&a->before,&a->work,sizeof a->before)))return KFFinish(a,KG_OWNERSHIP);
 /* Publish is permanent pin in the current real backing implementation.
  * Mark uncertainty before calling: failure cannot trigger unpublish/free. */
 a->publish_attempted=1;e=o->backing_publish(o->cookie);if(e)return KFFinish(a,e);a->published=1;
 for(unsigned r=0;r<2;r++){
  unsigned i=r?p->user_smr:p->kernel_smr;uint32_t v=p->initialize?0x20000u:(a->before.stream[i].s2cr&~0x030300ffu)|0x20000u;
  e=KFRouteWrite(a,r,0x84000000u+(r?4:5),v);if(e)return KFFinish(a,e);
 }
 uint32_t b=0x8000+4096*p->cb;KGContext next=a->before.cb[p->cb];
 if(p->initialize){e=KFInitializeRegime(a);if(e)return KFFinish(a,e);}
 else {e=KFW(a,b,next.sctlr&~1u,0);if(e)return KFFinish(a,e);a->ops.barrier(o->cookie);}
 e=KFTlb(a);if(e)return KFFinish(a,e);
 next.tcr=0x00802020;next.tcr2=0x00038011;next.ttbr0=a->backing.root_pa|((uint64_t)(p->cb+1)<<48);
 next.ttbr1=(uint64_t)(p->cb+1)<<48;next.mair0=0x44;next.mair1=0;
 next.sctlr=(3u<<22)|(1u<<5)|(1u<<2)|(1u<<1);a->programmed=next;
 e=KFContext(a,&next);if(e)return KFFinish(a,e);e=KFW(a,b,next.sctlr,0);if(e)return KFFinish(a,e);a->ops.barrier(o->cookie);
 e=KFContextVerify(a,&next,next.sctlr);if(e)return KFFinish(a,e);
 /* Enable translation before routing the stream to this bank. Producer is
  * still held; a speculative request can only translate or abort, not bypass. */
 e=KFW(a,b,next.sctlr|1,0);if(e)return KFFinish(a,e);a->ops.barrier(o->cookie);
 e=KFContextVerify(a,&next,next.sctlr|1);if(e)return KFFinish(a,e);
 uint32_t route=p->initialize?p->cb:(a->before.stream[p->kernel_smr].s2cr&~0x030300ffu)|p->cb;
 e=KFRouteWrite(a,0,0x84000005u,route);if(e)return KFFinish(a,e);
 e=KFSnapshot(a,&a->work);if(e)return KFFinish(a,e);
 uint32_t route_mask=p->initialize?0x030300ffu:UINT32_MAX,smr_mask=p->initialize?0xffff7fffu:UINT32_MAX;
 if(!KFSameUnowned(a,&a->work) || ((a->work.stream[p->kernel_smr].s2cr^route)&route_mask) ||
 ((a->work.stream[p->user_smr].s2cr^a->expected_s2cr[1])&route_mask) ||
 ((a->work.stream[p->kernel_smr].smr^0x84000005u)&smr_mask) || ((a->work.stream[p->user_smr].smr^0x84000004u)&smr_mask))return KFFinish(a,KG_OWNERSHIP);
 e=KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED);if(e)return KFFinish(a,e);
 a->receipt.state=KF_ATTACHED_QUIET;a->receipt.root_pa=a->backing.root_pa;a->receipt.backing_generation=a->backing.generation;
 return KFFinish(a,0);
}
static int KFFirstAllowRun(KFFirstAttach *a)
{
 if(!a || !__atomic_load_n(&a->magic,__ATOMIC_ACQUIRE))return KG_STATE;if(!KFTry(&a->busy))return KG_BUSY;
 if(a->receipt.state!=KF_ATTACHED_QUIET){KFUnlock(&a->busy);return KG_STATE;}
 int e=KFCheck(a,KF_ACCESS|KF_COLD|KF_DRAINED);if(!e){a->receipt.state=KF_RUN_ALLOWED;__atomic_store_n(&a->run_permitted,1,__ATOMIC_RELEASE);}
 a->receipt.result=e;KFUnlock(&a->busy);return e;
}
static int KFFirstStopRestore(KFFirstAttach *a)
{
 if(!a || !__atomic_load_n(&a->magic,__ATOMIC_ACQUIRE))return KG_STATE;if(!KFTry(&a->busy))return KG_BUSY;
 if(a->receipt.state!=KF_ATTACHED_QUIET && a->receipt.state!=KF_RUN_ALLOWED){KFUnlock(&a->busy);return KG_STATE;}
 __atomic_store_n(&a->run_permitted,0,__ATOMIC_RELEASE);
 int e=KFRestore(a);if(e){a->receipt.cleanup_error=e;a->receipt.state=KF_QUARANTINED;KFNamespaceRevoke(a->space);e=KG_QUARANTINE;}
 a->receipt.result=e;a->receipt.touched=a->touched;KFUnlock(&a->busy);return e;
}
static int KFFirstRunCheckPrivate(const KFFirstAttach *a,uint64_t epoch)
{
 if(!a || !__atomic_load_n(&a->run_permitted,__ATOMIC_ACQUIRE) || !epoch || epoch!=a->token.epoch)return KG_CLOSED;
 return KFNamespaceCheckPrivate(a->space,&a->token);
}
static int KFFirstSnapshot(KFFirstAttach *a,KFReceipt *r)
{if(!a || !r)return KG_INVALID;if(!KFTry(&a->busy))return KG_BUSY;*r=a->receipt;KFUnlock(&a->busy);return 0;}
static int KFFirstFault(KFFirstAttach *a)
{
 if(!a || !__atomic_load_n(&a->magic,__ATOMIC_ACQUIRE))return KG_STATE;if(!KFTry(&a->busy))return KG_BUSY;
 if(a->receipt.state!=KF_ATTACHED_QUIET && a->receipt.state!=KF_RUN_ALLOWED){KFUnlock(&a->busy);return KG_STATE;}
 uint32_t b=0x8000+4096*a->policy.cb;int e=KFR32(a,b+0x58,&a->receipt.fault);
 if(!e)e=KFR64(a,b+0x60,&a->receipt.fault_far);if(!e)e=KFR32(a,b+0x68,&a->receipt.fsynr0);
 if(!e)e=KFR32(a,b+0x6c,&a->receipt.fsynr1);if(!e)e=KFR32(a,0x1400+4*a->policy.cb,&a->receipt.frsynra);
 if(!e)e=KFR32(a,b+0x58,&a->receipt.fault_last);
 if(!e && (KFContextFaultBits(a->receipt.fault) || KFContextFaultBits(a->receipt.fault_last)))e=KG_FAULT;
 if(e){__atomic_store_n(&a->run_permitted,0,__ATOMIC_RELEASE);a->receipt.state=KF_QUARANTINED;KFNamespaceRevoke(a->space);}
 a->receipt.result=e;KFUnlock(&a->busy);return e;
}
#undef KF_TRY
#endif
