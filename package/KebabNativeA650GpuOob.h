/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_GPU_OOB_H
#define KEBAB_NATIVE_A650_GPU_OOB_H
/* End of FirstAttach TU, after actual586 NativeVM. Reuses its exact private
 * namespace/owner/BO/maps; no synthesized lease or second SMMU namespace. */
#include "KebabA650GpuOobNative.h"
#if defined(KERNEL)
#include <kern/KebabA650GpuOobOwner.h>
#include <kern/KebabA650GpuOobBinding.h>
#else
#include "KebabA650GpuOobOwner.h"
#include "KebabA650GpuOobBinding.h"
#endif
extern "C" int kebab_a650_gpu_oob_vm_ready_locked(const KebabA650GpuVmOwnerToken*vm)
{
 if(vm!=&kvg_native.owner || kebab_a650_gpu_vm_ready_locked(vm) ||
 __atomic_load_n(&kvg_engine.busy,__ATOMIC_ACQUIRE) || kvg_engine.record.result || kvg_engine.record.phase!=KVG_ATTACHED ||
 !kvg_engine.published || !kvg_engine.publish_attempted || !kvg_engine.held || !kvg_engine.backing_held ||
 !vm->published || !vm->held || !kvg_native.bo.held || !kvg_native.bo.generation ||
 kvg_native.bo.generation!=kvg_engine.backing.generation || !kvg_native.base || !kvg_native.clock[3])return 95;
 return 0;
}
static int KHO_VM_R32(const KebabA650GpuVmOwnerToken*vm,uint32_t off,uint32_t*out)
{int e=kebab_a650_gpu_oob_vm_ready_locked(vm);return e?e:KVGNativeRead32(&kvg_native,off,out);}
static int KHO_VM_R64(const KebabA650GpuVmOwnerToken*vm,uint32_t off,uint64_t*out)
{int e=kebab_a650_gpu_oob_vm_ready_locked(vm);return e?e:KVGNativeRead64(&kvg_native,off,out);}
extern "C" int kebab_a650_gpu_oob_vm_live_check_locked(const KebabA650GpuVmOwnerToken*vm)
{
 int e=kebab_a650_gpu_oob_vm_ready_locked(vm);if(e)return e;uint32_t v=0;uint64_t wide=0;
 if((e=KHO_VM_R32(vm,0x48,&v)))return e;if(v)return 5;
 for(unsigned n=0;n<5;n++){if((e=KHO_VM_R32(vm,0x8058+4096*n,&v)))return e;if(KFContextFaultBits(v))return 5;}
 for(unsigned n=0;n<3;n++){
  if((e=KHO_VM_R32(vm,0x800+4*n,&v)))return e;
  if((v^kvg_engine.after.stream[n].smr)&(n==2?0xffff7fffu:UINT32_MAX))return 13;
  if((e=KHO_VM_R32(vm,0xc00+4*n,&v)))return e;
  if((v^kvg_engine.after.stream[n].s2cr)&0x030300ffu)return 13;
 }
 for(unsigned n=0;n<2;n++){
  if((e=KHO_VM_R32(vm,0x8000+4096*n,&v)))return e;
  if((v^kvg_engine.after.cb[n].sctlr)&0x0fdf11f7u)return 13;
  if((e=KHO_VM_R64(vm,0x8020+4096*n,&wide)))return e;
  if((wide^kvg_engine.after.cb[n].ttbr0)&(KG_PTE_PA|0xffff000000000000ULL))return 13;
 }
 return 0;
}
extern "C" void kebab_a650_gpu_oob_vm_quarantine_locked(const KebabA650GpuVmOwnerToken*vm)
{if(vm==&kvg_native.owner && kvg_engine.space==&kf_namespace && kvg_engine.published)KFNamespaceRevoke(&kf_namespace);}
#define KHO_VM_TRY(x) do{int kho_e=(x);if(kho_e)return kho_e;}while(0)
static int KHO_VM_Snapshot(const KebabA650GpuVmOwnerToken*vm,KGDiscovery*s)
{
 memset(s,0,sizeof *s);KHO_VM_TRY(KHO_VM_R32(vm,0x20,&s->id0));KHO_VM_TRY(KHO_VM_R32(vm,0x24,&s->id1));KHO_VM_TRY(KHO_VM_R32(vm,0x28,&s->id2));
 if(s->id0!=0x4c017e06 || s->id1!=0x20000005 || s->id2!=0x5511)return 95;
 s->num_streams=6;s->num_cb=5;s->stride=4096;s->cb_base=0x8000;s->read_mask=31;s->pa_bits=s->ipa_bits=36;s->va_bits=48;
 KHO_VM_TRY(KHO_VM_R32(vm,0,&s->cr0));KHO_VM_TRY(KHO_VM_R32(vm,0x48,&s->gfsr));
 for(unsigned n=0;n<3;n++)KHO_VM_TRY(KHO_VM_R32(vm,0x50+4*n,&s->gfsynr[n]));
 for(unsigned n=0;n<6;n++){KHO_VM_TRY(KHO_VM_R32(vm,0x800+4*n,&s->stream[n].smr));KHO_VM_TRY(KHO_VM_R32(vm,0xc00+4*n,&s->stream[n].s2cr));}
 for(unsigned n=0;n<5;n++){
  auto*c=&s->cb[n];unsigned b=0x8000+4096*n;c->present=1;
  KHO_VM_TRY(KHO_VM_R32(vm,0x1000+4*n,&c->cbar));KHO_VM_TRY(KHO_VM_R32(vm,0x1800+4*n,&c->cba2r));
  c->type=(c->cbar>>16)&3;c->hypc=(c->cbar>>10)&1;c->monc=(c->cba2r>>1)&1;c->vmid=(s->cr0&0x80000000u)?c->cba2r>>16:c->cbar&255;
  KHO_VM_TRY(KHO_VM_R32(vm,b,&c->sctlr));KHO_VM_TRY(KHO_VM_R32(vm,b+4,&c->actlr));KHO_VM_TRY(KHO_VM_R32(vm,b+0x10,&c->tcr2));KHO_VM_TRY(KHO_VM_R32(vm,b+0x30,&c->tcr));
  KHO_VM_TRY(KHO_VM_R64(vm,b+0x20,&c->ttbr0));KHO_VM_TRY(KHO_VM_R64(vm,b+0x28,&c->ttbr1));KHO_VM_TRY(KHO_VM_R32(vm,b+0x38,&c->mair0));KHO_VM_TRY(KHO_VM_R32(vm,b+0x3c,&c->mair1));
  KHO_VM_TRY(KHO_VM_R32(vm,b+0x58,&c->fsr));KHO_VM_TRY(KHO_VM_R64(vm,b+0x60,&c->far));
  KHO_VM_TRY(KHO_VM_R32(vm,b+0x68,&c->fsynr0));KHO_VM_TRY(KHO_VM_R32(vm,b+0x6c,&c->fsynr1));KHO_VM_TRY(KHO_VM_R32(vm,0x1400+4*n,&c->frsynra));
 }
 int e=KVGValidate(&kvg_engine,s,1);if(e)return e;
 return KVGConfigEqual(&s->cb[1],&kvg_engine.after.cb[1])?0:13;
}
#undef KHO_VM_TRY
struct KHOAccess {
 KebabA650GpuOobOwnerToken owner;IOMemoryDescriptor *memory;IOMemoryMap *mapping;uintptr_t base;
 KebabA650GpuOobObservation *observation;
};
static KHOAccess kho_native;
static uint32_t kho_native_once;
static int kho_native_result=EAGAIN;
static int KHONativeBound(void)
{unsigned any=0;for(unsigned n=0;n<32;n++)any|=KebabA650GpuOobSourceContract[n];return KebabA650GpuOobNativeQualified==1&&any;}
static int KHONativeGate(void*cookie)
{auto*n=(KHOAccess*)cookie;if(n!=&kho_native || KFNativeWorker(NULL))return 95;return kebab_rpmh_gpu_oob_check_locked(&n->owner);}
static int KHONativeRead(void*cookie,unsigned region,uint32_t off,uint32_t*out)
{
 auto*n=(KHOAccess*)cookie;if(n!=&kho_native || !out || KFNativeWorker(NULL) || kebab_a650_gpu_oob_vm_ready_locked(&kvg_native.owner))return 95;
 uintptr_t base=0;
 if(!region && (off==0x14648 || off==0x1464c || off==0x24c14 || off==0x14340 || off==0x143b0))base=n->base;
 else if(region==1 && (off==0x100c || off==0x1010 || off==0x106c))base=kvg_native.clock[3];
 else return 22;
 if(!base)return 95;*out=*(volatile const uint32_t*)(base+off);return 0;
}
static int KHONativeWrite(void*cookie,unsigned region,uint32_t off,uint32_t value)
{
 auto*n=(KHOAccess*)cookie;
 if(n!=&kho_native || !KHONativeBound() || !n->base || region ||
 !((off==0x14650&&value==0x40000000u)||(off==0x14644&&value==0x80000000u)))return 95;
 int e=KHONativeGate(n);if(e)return e;uintptr_t address=n->base+off;
#if defined(__aarch64__)
 __asm__ volatile("str %w0, [%1]":"+r"(value):"r"(address):"memory");
#else
 *(volatile uint32_t*)address=value;
#endif
 return 0;
}
static void KHONativeBarrier(void*)
{
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 OSMemoryBarrier();
#endif
}
static void KHONativeDelay(void*,unsigned us){if(us==100)IODelay(100);}
static int KHONativeSnapshot(void*cookie,unsigned after)
{
 auto*n=(KHOAccess*)cookie;if(n!=&kho_native || !n->observation || after>1 || KFNativeWorker(NULL))return 95;
 return KHO_VM_Snapshot(&kvg_native.owner,after?&n->observation->vm_after:&n->observation->vm_before);
}
extern "C" int kebab_a650_gpu_oob_prepare(void)
{
 int e=KFNativeWorker(NULL);if(e)return e;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&kho_native_once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return expected==2?kho_native_result:EBUSY;
 auto*n=&kho_native;n->observation=(KebabA650GpuOobObservation*)IOMalloc(sizeof *n->observation);
 if(!n->observation){e=ENOMEM;goto done;}memset(n->observation,0,sizeof *n->observation);KebabA650GpuOobInit(&n->observation->receipt);
 if(!KHONativeBound()){e=95;goto done;}
 e=kebab_a650_gpu_oob_vm_ready_locked(&kvg_native.owner);if(e)goto done;
 n->memory=IOMemoryDescriptor::withPhysicalAddress(0x03d6a000,0x26000,kIODirectionInOut);if(!n->memory){e=ENOMEM;goto done;}
 n->mapping=n->memory->map(kIOMapAnywhere|kIOMapInhibitCache);
 if(!n->mapping || n->mapping->getLength()!=0x26000 || !(n->base=n->mapping->getVirtualAddress())){e=ENOMEM;goto done;}
 {KebabA650GpuOobOwnerIo io={{n,KHONativeGate,KHONativeRead,KHONativeWrite,KHONativeBarrier,KHONativeDelay,KVGNativeNow},KHONativeSnapshot};
  e=kebab_rpmh_gpu_oob_hold(&n->owner,&kvg_native.owner,&io,&n->observation->receipt);}
done:
 if(!n->owner.pinned){n->base=0;if(n->mapping){n->mapping->release();n->mapping=NULL;}if(n->memory){n->memory->release();n->memory=NULL;}}
 if(n->observation){auto*r=&n->observation->receipt;r->result=(uint32_t)e;if(e&&!r->phase)r->phase=KHO_FAILED;}
 kho_native_result=e;__atomic_store_n(&kho_native_once,2,__ATOMIC_RELEASE);return e;
}
extern "C" int kebab_a650_gpu_oob_snapshot(KebabA650GpuOobObservation*out)
{
 if(!out)return EINVAL;if(__atomic_load_n(&kho_native_once,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 if(kho_native.observation)memcpy(out,kho_native.observation,sizeof *out);
 else{memset(out,0,sizeof *out);KebabA650GpuOobInit(&out->receipt);out->receipt.result=(uint32_t)kho_native_result;out->receipt.phase=KHO_FAILED;}
 return 0;
}
#endif
