/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_ZAP_AUTH_H
#define KEBAB_NATIVE_A650_ZAP_AUTH_H
#include <kern/thread.h>
#include "../../../bsd/kern/KebabScmPasWorker.h"
#include "KebabA650ZapAuthCore.h"
/* Included after the unchanged CAP43 implementation in the actual first
 * attach TU. No independent namespace, firmware allocator or SCM transport. */
struct KZA2Evidence {
 KZA2Receipt receipt;
 uint32_t prevm_checks,postcp_checks,checked_pages,copied_bytes,verified_bytes,barriers;
 uint32_t parent_pinned,namespace_released,HYP_protection_unknown,secure_mode_unknown;
 uint32_t firmware_retained,metadata_retained,quarantine_guard,reserved;
 uint8_t metadata_before_sha256[32],image_before_sha256[32];
 uint8_t image_logical_expected_sha256[32],image_logical_after_sha256[32];
 uint8_t ranges_sha256[3][32];
 KGDiscovery smmu[6];
};
static_assert(sizeof(KZA2Receipt)==448,"auth protocol ABI448");
static_assert(sizeof(KZA2Evidence)<=12264,"CAP45 bounded evidence");
static_assert(sizeof(kebab_zap_auth_observation)==12288,"CAP45 fixed12KiB");
static uint32_t kza2_state;
static uintptr_t kza2_thread;
static uint64_t kza2_namespace_cookie,kza2_epoch;
static KebabScmTransaction kza2_scm;
static KZA2Evidence *kza2_evidence;
static kebab_low_dma_nc_observation kza2_nc;
static uint64_t kza2_gmu_ranges[8][2];
static uint8_t *kza2_logical;
extern "C" int kebab_rpmh_gpu_zap_prevm(const KebabGpuColdToken *,uint64_t,unsigned);
extern "C" int kebab_rpmh_gpu_zap_postcp(const KebabGpuColdToken *,uint64_t,unsigned);
extern "C" int kebab_rpmh_gpu_zap_quarantine(unsigned);
extern "C" int kebab_a650_zap_original_gpu_chain(void);

static int kza2_worker(void)
{
 return panic_active() || KFNativeWorker(NULL) || !kza2_thread ||
  kza2_thread!=(uintptr_t)current_thread() || __atomic_load_n(&kza2_state,__ATOMIC_ACQUIRE)!=1?95:0;
}
/* Extra restriction, never an exception to KFReserve or the old PAS slot1
 * exclusion. Only this worker may enter original VM allocation after seal. */
static int KZANativeVmReservationAllowed(void)
{
 if(!__atomic_load_n(&kf_pas13_frozen,__ATOMIC_ACQUIRE))return __atomic_load_n(&kza2_state,__ATOMIC_ACQUIRE)?95:0;
 return kza2_worker() || !kza2_evidence || !kza2_evidence->receipt.prepared ||
  !kza2_evidence->receipt.image_frozen || kza2_evidence->receipt.phase!=KZA2_LOADED ||
  !kza2_evidence->namespace_released || !kebab_scm_transaction_shared_safe(&kza2_scm)?95:0;
}
extern "C" int kebab_a650_zap_render_gate(void)
{
 /* Actual authentication and independently completed hardware secure exit
  * are both required. The later canonical render gate checks live owners. */
 return KZANativeSecureExitReady();
}
static int kza2_ranges(kebab_pas13_resources_observation *o,unsigned count)
{
 static const uint32_t size[17]={0x100000,0x4000,0x10000,0x8000,0x14000,0xc000,0x4000,0x8000,0x4000,0x10000,0x10000,0x4000,0x4000,0x4000,0x4000,0x10000,0x4000};
 if(count!=10 && count!=17)return 22;
 o->checked_pairs=0;o->range_count=count;
 for(unsigned i=0;i<count;i++){
  const auto *p=&o->range[i];
  if(p->domain!=(i<2?1u:i<10?2u:3u) || p->index!=(i<2?i:i<10?i-2:i-10) ||
    p->bytes!=size[i] || !p->physical || (p->physical&16383) || (i==0 && (p->physical&0xfffff)) ||
    p->physical>UINT64_MAX-p->bytes ||
    (i<2?p->physical+p->bytes>0x100000000ULL:(p->physical<0x200000000ULL || p->physical+p->bytes>0x1000000000ULL)))return 95;
  for(unsigned j=0;j<i;j++){
   const auto *q=&o->range[j];
   if(p->physical<q->physical+q->bytes && q->physical<p->physical+p->bytes)return 95;
   ++o->checked_pairs;
  }
 }
 return 0;
}
static int kza2_unfaulted(const KGDiscovery *a,const KGDiscovery *b)
{
 if(a->gfsr || b->gfsr || a->cr0!=b->cr0 || memcmp(a->stream,b->stream,6*sizeof(KGStream)))return 95;
 for(unsigned i=0;i<5;i++)if(KFContextFaultBits(a->cb[i].fsr) || KFContextFaultBits(b->cb[i].fsr) ||
  !KVGConfigEqual(&a->cb[i],&b->cb[i]))return 95;
 return 0;
}
/* The caller owns canonical VRM globalbusy. No SMC, memory write, vote or
 * namespace mutation occurs inside this observation. */
extern "C" int kebab_a650_zap_prevm_locked(void)
{
 if(kza2_worker() || !kza2_evidence || kf_pas13_frozen || !kf_pas13_busy ||
  kza2_namespace_cookie!=UINT64_MAX || __atomic_load_n(&kf_namespace.slot[1],__ATOMIC_ACQUIRE) || kvg_native_once ||
  kcp_native_once || kho_native_once || kf_attachment.token.slot ||
  KFNamespaceCheckPrivate(&kf_namespace,&kf_attachment.token))return 95;
 auto *e=kza2_evidence;auto *p=&e->receipt.before;
 kebab_pas13_resources_observation inventory={};uint64_t ranges[8][2]={};
 if(KFNativeBackingCheck(&kf_native,&kf_attachment.backing) ||
  kebab_a650_gmu_backing_owned_ranges(&kf_native.backing,ranges,8))return 95;
 inventory.range[0]={kza2_nc.image,kza2_nc.image_bytes,1,0};
 inventory.range[1]={kza2_nc.metadata,kza2_nc.metadata_bytes,1,1};
 for(unsigned i=0;i<8;i++)inventory.range[i+2]={ranges[i][0],ranges[i][1],2,i};
 if(kza2_ranges(&inventory,10))return 95;
 const uint64_t start=mach_absolute_time();
 int error=kpr_low_pages(&kza2_nc,&inventory,start);if(error)return error;
 if(inventory.checked_pages!=260 || inventory.checked_pairs!=45 || !kf_native.backing.generation)return 95;
 *p={kza2_nc.image,kza2_nc.metadata,kza2_nc.image_bytes,kza2_nc.metadata_bytes,1,kf_native.backing.generation,0,0,10,45,260,0,32,0x1ff};
 if(e->prevm_checks && memcmp(ranges,kza2_gmu_ranges,sizeof ranges))return 95;
 memcpy(kza2_gmu_ranges,ranges,sizeof ranges);
 kzs_hash(inventory.range,10*sizeof(inventory.range[0]),e->ranges_sha256[0]);
 e->checked_pages=inventory.checked_pages;
 return 0;
}
/* KFSnapshot checks the real cold owner on every read. Its original
 * check acquires VRM globalbusy, so call it only AFTER the outer canonical
 * wrapper has released that lock. The retained parent pin and separate PAS
 * namespace exclusion stay held throughout both complete snapshots. */
static int kza2_prevm_snapshots(void)
{
 if(kza2_worker() || !kza2_evidence || !kza2_evidence->parent_pinned ||
  kf_pas13_frozen || !kf_pas13_busy || kza2_namespace_cookie!=UINT64_MAX ||
  __atomic_load_n(&kf_namespace.slot[1],__ATOMIC_ACQUIRE) ||
  KFNamespaceCheckPrivate(&kf_namespace,&kf_attachment.token))return 95;
 auto *e=kza2_evidence;int error=0;
 error=KFSnapshot(&kf_attachment,&e->smmu[0]);if(error)return error;
 error=KFSnapshot(&kf_attachment,&e->smmu[1]);if(error)return error;
 error=kza2_unfaulted(&e->smmu[0],&e->smmu[1]);if(error)return error;
 for(unsigned sample=0;sample<2;sample++){
  const auto *s=&e->smmu[sample];
  if(s->cr0!=kf_attachment.work.cr0 || s->cb[1].sctlr&1 ||
    memcmp(s->stream,kf_attachment.work.stream,6*sizeof(KGStream)))return 95;
  for(unsigned i=0;i<5;i++)if(!KVGConfigEqual(&s->cb[i],&kf_attachment.work.cb[i]))return 95;
 }
 if(kza2_worker() || !kf_pas13_busy || kza2_namespace_cookie!=UINT64_MAX ||
  KFNamespaceCheckPrivate(&kf_namespace,&kf_attachment.token))return 95;
 e->receipt.before.quiet_samples=2;++e->prevm_checks;return 0;
}
extern "C" int kebab_a650_zap_postcp_locked(unsigned final)
{
 if(final>1 || kza2_worker() || !kza2_evidence || !kf_pas13_frozen || kf_pas13_busy ||
  !kza2_evidence->receipt.prepared || !kza2_evidence->receipt.image_frozen ||
  !kebab_scm_transaction_shared_safe(&kza2_scm))return 95;
 auto *e=kza2_evidence;kebab_pas13_resources_observation inv={};uint64_t ranges[8][2]={};
 if(KFNativeBackingCheck(&kf_native,&kf_attachment.backing) ||
  kebab_a650_gmu_backing_owned_ranges(&kf_native.backing,ranges,8) ||
  memcmp(ranges,kza2_gmu_ranges,sizeof ranges) || kebab_a650_gpu_backing_check(&kvg_native.bo) ||
  KVGBackingValid(&kvg_engine.backing) || kvg_engine.token.slot!=1 ||
  KFNamespaceCheckPrivate(&kf_namespace,&kvg_engine.token))return 95;
 inv.range[0]={kza2_nc.image,kza2_nc.image_bytes,1,0};inv.range[1]={kza2_nc.metadata,kza2_nc.metadata_bytes,1,1};
 for(unsigned i=0;i<8;i++)inv.range[i+2]={ranges[i][0],ranges[i][1],2,i};
 inv.range[10]={kvg_engine.backing.root_physical,kvg_engine.backing.table_bytes,3,0};
 for(unsigned i=0;i<6;i++){const auto *r=&kvg_engine.backing.region[i];inv.range[i+11]={r->physical,r->allocation_bytes,3,i+1};}
 int error=kza2_ranges(&inv,17);if(error)return error;
 /* Descriptor/AT proof only. Never dereference either frozen low buffer. */
 error=kpr_low_pages(&kza2_nc,&inv,mach_absolute_time());if(error)return error;
 KGDiscovery *a=&e->smmu[2+final*2],*b=a+1;
 error=KHO_VM_Snapshot(&kvg_native.owner,a);if(error)return error;
 error=kpr_idle(&inv);if(error)return error;
 error=KHO_VM_Snapshot(&kvg_native.owner,b);if(error)return error;
 error=kza2_unfaulted(a,b);if(error)return error;
 error=kpr_idle(&inv);if(error)return error;
 if(inv.checked_pages!=260 || inv.checked_pairs!=136)return 95;
 kzs_hash(inv.range,sizeof inv.range,e->ranges_sha256[1+final]);
 if(final && memcmp(e->ranges_sha256[1],e->ranges_sha256[2],32))return 95;
 KZA2Proof proof={kza2_nc.image,kza2_nc.metadata,kza2_nc.image_bytes,kza2_nc.metadata_bytes,1,
  kf_native.backing.generation,kvg_native.bo.generation,kcp_native.owner.generation,17,136,260,2,32,0x3ff};
 if(final){if(memcmp(&proof,&e->receipt.after,sizeof proof))return 95;}
 else e->receipt.after=proof;
 ++e->postcp_checks;return 0;
}
static int kza2_preflight(void *,KZA2Proof *out)
{
 if(kza2_worker() || !kza2_evidence || out!=&kza2_evidence->receipt.before)return 95;
 kebab_zap_stage_observation stage={};kebab_low_dma_observation low={};
 if(kebab_zap_stage_snapshot(&stage,sizeof stage) || stage.magic!=KEBAB_ZAP_STAGE_MAGIC ||
  stage.schema!=1 || stage.bytes!=sizeof stage || stage.state!=2 || stage.result || stage.phase!=4 ||
  stage.flags!=63 || stage.guard || stage.checked_pages!=260 || stage.copied_bytes!=1064960 ||
  stage.verified_bytes!=1064960 || stage.barriers!=2 || stage.input_metadata_bytes!=6860 ||
  stage.load_bytes!=1676 || stage.logical_bytes!=4096 || stage.allocation_bytes!=0x100000 || !kzs_input ||
  kebab_low_dma_snapshot(&low,sizeof low) || low.magic!=KEBAB_LOW_DMA_MAGIC || low.schema!=1 ||
  low.bytes!=sizeof low || low.state!=KLDC_DONE || low.result!=KLDC_MEASURED_RETAINED || low.flags!=831 ||
  kebab_low_dma_nc_snapshot(&kza2_nc,sizeof kza2_nc) || kza2_nc.magic!=KEBAB_LOW_DMA_NC_MAGIC ||
  kza2_nc.schema!=1 || kza2_nc.bytes!=sizeof kza2_nc || kza2_nc.state!=2 || kza2_nc.result ||
  kza2_nc.flags!=15 || kza2_nc.checked_pages!=260 || kza2_nc.image!=low.image ||
  kza2_nc.metadata!=low.metadata || kza2_nc.image_bytes!=low.image_bytes || kza2_nc.metadata_bytes!=low.metadata_bytes ||
  kza2_nc.image_bytes!=0x100000 || kza2_nc.metadata_bytes!=0x4000 ||
  stage.image!=kza2_nc.image || stage.metadata!=kza2_nc.metadata ||
  stage.image_bytes!=kza2_nc.image_bytes || stage.metadata_bytes!=kza2_nc.metadata_bytes)return 95;
 uint8_t input_hash[32];kzs_hash(kzs_input,6860,input_hash);
 if(memcmp(input_hash,stage.input_metadata_sha256,32))return 95;
 kzs_hash(kzs_input+6860,1676,input_hash);if(memcmp(input_hash,stage.input_load_sha256,32))return 95;
 KebabZapInputs inputs={{kzs_input,6860},{{kzs_input,148},{kzs_input+148,6712},{kzs_input+6860,1676}}};KebabZapPlan plan={};
 if(KebabA650ZapPlan(&inputs,&plan) || plan.logical_region_bytes!=4096 || plan.allocation_bytes!=0x100000 ||
  plan.metadata_bytes!=6860 || plan.load_file_bytes!=1676)return 95;
 kza2_logical=(uint8_t *)IOMalloc(4096);if(!kza2_logical)return 12;
 memset(kza2_logical,0,4096);memcpy(kza2_logical,kzs_input+6860,1676);
 kzs_hash(kza2_logical,4096,kza2_evidence->image_logical_expected_sha256);
 int error=kebab_gpu_namespace_change_begin(KF_OP_PAS13_SEPARATE_IMAGE,&kza2_namespace_cookie);
 if(error || kza2_namespace_cookie!=UINT64_MAX)return error?error:95;
 const KebabGpuColdToken *cold=kebab_gpu_first_run_lease(&kza2_epoch);
 if(cold!=&kf_native.cold || kza2_epoch!=1)return 95;
 error=kebab_rpmh_gpu_zap_prevm(cold,kza2_epoch,1);if(error)return error;
 kza2_evidence->parent_pinned=1;
 error=kza2_prevm_snapshots();if(error)return error;
 kzs_hash((const void *)kza2_nc.metadata_va,0x4000,kza2_evidence->metadata_before_sha256);
 kzs_hash((const void *)kza2_nc.image_va,0x100000,kza2_evidence->image_before_sha256);
 return memcmp(kza2_evidence->metadata_before_sha256,stage.staged_metadata_sha256,32) ||
  memcmp(kza2_evidence->image_before_sha256,stage.staged_image_sha256,32)?95:0;
}
static int kza2_begin(void *){return kebab_scm_transaction_begin(&kza2_scm,KST_PAS13);}
static int kza2_fresh(void *,const KZA2Proof *proof)
{
 if(kza2_worker() || proof!=&kza2_evidence->receipt.before ||
  !kebab_scm_transaction_shared_safe(&kza2_scm))return 95;
 int error=kebab_rpmh_gpu_zap_prevm(&kf_native.cold,kza2_epoch,0);
 return error?error:kza2_prevm_snapshots();
}
static int kza2_invoke(void *,unsigned index,const KebabPasRegs *request,KebabPasResult *out,uint32_t *guard)
{
 if(index>2 || kza2_worker() || !kza2_evidence ||
  !kebab_scm_transaction_shared_safe(&kza2_scm))return 95;
 auto *r=&kza2_evidence->receipt;
 if(index==0){if(kf_pas13_frozen || __atomic_load_n(&kf_namespace.slot[1],__ATOMIC_ACQUIRE) || !kf_pas13_busy || !r->metadata_frozen)return 95;
  __atomic_store_n(&kf_pas13_frozen,1,__ATOMIC_RELEASE);}
 else if(!kf_pas13_frozen)return 95;
 if(index==2 && (!r->cp_ready || !r->prepared || !r->image_frozen || !kza2_evidence->namespace_released))return 95;
 printf("KEBAB ZAPAUTH: call=%u function=%x begin; metadata frozen, no retry\n",index,(unsigned)request->x[0]);
 const uint64_t start=KVGNativeNow(NULL);
 KebabScmPasRawResult raw={};int error=kebab_scm_pas13_call(&kza2_scm,request->x,&raw);
 const uint64_t end=KVGNativeNow(NULL);
 printf("KEBAB ZAPAUTH: call=%u returned; x0=%llx x1=%llx attempts=%u guard=%u us=%llu\n",
  index,(unsigned long long)raw.status,(unsigned long long)raw.value,raw.attempts,raw.guard,
  (unsigned long long)(end>=start?end-start:UINT64_MAX));
 out->status_raw=raw.status;out->value=raw.value;out->type=raw.type;out->data=raw.data;
 out->cookie=raw.cookie;out->attempts=raw.attempts;out->returned=raw.attempts?1:0;
 out->interrupted_exhausted=raw.status==1 && raw.attempts==32;*guard=raw.guard;
 return error;
}
static int kza2_load(void *,const KZA2Proof *proof)
{
 if(kza2_worker() || proof!=&kza2_evidence->receipt.before || !kza2_logical || !kf_pas13_frozen ||
  !kf_pas13_busy || __atomic_load_n(&kf_namespace.slot[1],__ATOMIC_ACQUIRE) || kza2_namespace_cookie!=UINT64_MAX ||
  !kebab_scm_transaction_shared_safe(&kza2_scm))return 95;
 auto *e=kza2_evidence;auto *r=&e->receipt;
 if(r->phase!=KZA2_MEM_RETURNED || !r->metadata_frozen || r->image_frozen ||
  !KebabPas13OperationSucceeded(&r->operation[0].raw) || !KebabPas13OperationSucceeded(&r->operation[1].raw))return 95;
 /* Only the logical image span is writable after MEM_SETUP. No metadata
  * access and no whole-allocation restaging. The trusted private input and
  * its expected padded hash were prepared before the first PAS attempt. */
 volatile uint8_t *destination=(volatile uint8_t *)kza2_nc.image_va;
 for(unsigned i=0;i<4096;i++){destination[i]=kza2_logical[i];++e->copied_bytes;}
 __asm__ volatile("dsb sy" ::: "memory");++e->barriers;
 for(unsigned i=0;i<4096;i++){if(destination[i]!=kza2_logical[i])return 5;++e->verified_bytes;}
 kzs_hash((const void *)kza2_nc.image_va,4096,e->image_logical_after_sha256);
 return memcmp(e->image_logical_expected_sha256,e->image_logical_after_sha256,32)?5:0;
}
static int kza2_chain(void *,const KZA2Proof *before,KZA2Proof *after)
{
 if(kza2_worker() || before!=&kza2_evidence->receipt.before || after!=&kza2_evidence->receipt.after ||
  !kza2_evidence->receipt.prepared || !kza2_evidence->receipt.image_frozen || !kf_pas13_frozen ||
  !kebab_scm_transaction_shared_safe(&kza2_scm))return 95;
 int error=kebab_gpu_namespace_change_end(kza2_namespace_cookie,0);if(error)return error;
 kza2_namespace_cookie=0;kza2_evidence->namespace_released=1;
 error=kebab_a650_zap_original_gpu_chain();if(error)return error;
 return kebab_rpmh_gpu_zap_postcp(&kf_native.cold,kza2_epoch,0);
}
static int kza2_final(void *,const KZA2Proof *proof)
{
 if(kza2_worker() || proof!=&kza2_evidence->receipt.after)return 95;
 return kebab_rpmh_gpu_zap_postcp(&kf_native.cold,kza2_epoch,1);
}
static int kza2_end(void *){return kebab_scm_transaction_end(&kza2_scm);}
extern "C" int kebab_zap_auth_prepare(void)
{
 if(panic_active() || KFNativeWorker(NULL))return 2;
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kza2_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return expected==2?0:16;
 kza2_thread=(uintptr_t)current_thread();kza2_evidence=(KZA2Evidence *)IOMalloc(sizeof(KZA2Evidence));
 if(kza2_evidence){
  memset(kza2_evidence,0,sizeof(*kza2_evidence));
  kza2_evidence->HYP_protection_unknown=kza2_evidence->secure_mode_unknown=1;
  kza2_evidence->firmware_retained=kza2_evidence->metadata_retained=1;
  const KZA2Ops ops={NULL,KVGNativeNow,kza2_preflight,kza2_begin,kza2_fresh,kza2_invoke,kza2_load,kza2_chain,kza2_final,kza2_end};
  int error=KZA2Run(&ops,&kza2_evidence->receipt);
  if(kza2_namespace_cookie){int end=kebab_gpu_namespace_change_end(kza2_namespace_cookie,0);
   if(!end)kza2_namespace_cookie=0;
   if(end && !error)kza2_evidence->receipt.result=end;}
  if(kza2_evidence->receipt.result && kf_pas13_frozen)
   kza2_evidence->quarantine_guard=kebab_rpmh_gpu_zap_quarantine(kza2_evidence->receipt.result);
  printf("KEBAB ZAPAUTH: terminal; result=%u phase=%u authenticated=%u end-guard=%u quarantine-guard=%u; memory retained\n",
   kza2_evidence->receipt.result,kza2_evidence->receipt.phase,kza2_evidence->receipt.authenticated,
   kza2_evidence->receipt.end_error,kza2_evidence->quarantine_guard);
 }
 __atomic_store_n(&kza2_state,2,__ATOMIC_RELEASE);return 0;
}
extern "C" int kebab_zap_auth_snapshot(kebab_zap_auth_observation *out,uint32_t bytes)
{
 if(!out || bytes!=sizeof(*out) || (uintptr_t)out>UINTPTR_MAX-bytes)return 22;
 if(__atomic_load_n(&kza2_state,__ATOMIC_ACQUIRE)!=2)return 35;
 if(kza2_evidence && (uintptr_t)out<(uintptr_t)kza2_evidence+sizeof(*kza2_evidence) &&
  (uintptr_t)kza2_evidence<(uintptr_t)out+bytes)return 22;
 memset(out,0,sizeof(*out));out->magic=KEBAB_ZAP_AUTH_MAGIC;out->schema=1;
 out->bytes=sizeof(*out);out->state=2;
 if(kza2_evidence){out->result=kza2_evidence->receipt.result;memcpy(out->payload,kza2_evidence,sizeof(*kza2_evidence));}
 else out->result=12;
 return 0;
}
#endif
