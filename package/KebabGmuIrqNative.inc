/* SPDX-License-Identifier: MIT */
/* Include inside pe_fiq.c HAS_GIC_V3 / KEBAB, after _gic helpers. */
#include <kern/KebabGmuIrqOwner.h>
#include <kern/KebabGmuIrqBinding.h>
#include <kern/KebabGpuFirstAttachNative.h>
#include <kern/KebabGpuCcCore.h>
extern int get_preemption_level(void); /* actual arm/cpu_data.h declaration */
static KebabGmuIrqState kebab_gmu_irq_state;
static KebabGmuIrqToken *kebab_gmu_irq_identity;
static vm_offset_t kebab_gmu_irq_gcc,kebab_gmu_irq_cc,kebab_gmu_irq_hfi,kebab_gmu_irq_ao;
static uint32_t kebab_gmu_irq_attempt,kebab_gmu_irq_installed,kebab_gmu_irq_worker_busy;
static uint32_t kebab_gmu_irq_gdscr,kebab_gmu_irq_controls[5];
static uint32_t kebab_gmu_irq_mmio_reads,kebab_gmu_irq_mmio_writes;
static uint32_t kebab_gmu_irq_raw(vm_offset_t p)
{__atomic_add_fetch(&kebab_gmu_irq_mmio_reads,1,__ATOMIC_RELAXED);return _gic_read32(p);}
static int kebab_gmu_irq_worker(void)
{return ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled()?95:0;}
static int kebab_gmu_irq_running(uint32_t v)
{return !(v&0x80000000u) || (v&0x70000000u)==0x20000000u;}
static int kebab_gmu_irq_private(void)
{
 const KebabGmuIrqToken *t=__atomic_load_n(&kebab_gmu_irq_identity,__ATOMIC_ACQUIRE);
 if(!t || !__atomic_load_n(&t->held,__ATOMIC_ACQUIRE) || t->reserved || t->epoch!=kebab_gmu_irq_state.epoch || !t->generation)return 95;
 return kebab_gpu_first_ready_locked(t->cold,t->epoch);
}
static int kebab_gmu_irq_power(void *unused)
{
 (void)unused;if(kebab_gmu_irq_private() || !kebab_gmu_irq_gcc || !kebab_gmu_irq_cc)return 95;
 uint32_t v=kebab_gmu_irq_raw(kebab_gmu_irq_gcc+4);
 if(!(v&1) || (v&0x80000000u) || (kebab_gmu_irq_raw(kebab_gmu_irq_gcc)&1))return 95;
 v=kebab_gmu_irq_raw(kebab_gmu_irq_cc+0x6c);
 if((v&KebabGpuCcControlMask(2))!=kebab_gmu_irq_gdscr || (v&1) || !(kebab_gmu_irq_raw(kebab_gmu_irq_cc+0x540)&0x80000000u))return 95;
 const vm_offset_t p[5]={kebab_gmu_irq_cc+0x78,kebab_gmu_irq_cc+0x98,kebab_gmu_irq_cc+0x9c,kebab_gmu_irq_gcc+0x154,kebab_gmu_irq_gcc+0xc};
 /* CM3 may idle normally; retain/verify its owned request, not a permanent
  * CLK_OFF invariant. Bus/AHB/CXO dependencies still need a live ACK. */
 for(unsigned n=0;n<5;n++){v=kebab_gmu_irq_raw(p[n]);if((v&0x0fffffffu)!=kebab_gmu_irq_controls[n] || !(v&1) || (n!=1 && !kebab_gmu_irq_running(v)))return 95;}
 return 0;
}
static int kebab_gmu_irq_address(unsigned region,uint32_t off,vm_offset_t *p)
{
 if(!p || (off&3))return 22;
 if(region==0){
  if(off==0 || off==4 || off==0x128 || off==0x1a8 || off==0x228 || off==0x328 || off==0xc54 || off==0x550 ||
   off==0x6a80 || off==0x6a84 || off==0x6a88 || off==0x6a8c){if(!gicd_base)return 95;*p=gicd_base+off;return 0;}
 }else if(region==1){
  if(kebab_gmu_irq_power(NULL))return 95;
  if(off==0x14000 || off==0x14644 || off==0x14648 || off==0x1464c){if(!kebab_gmu_irq_hfi)return 95;*p=kebab_gmu_irq_hfi+off-0x12000;return 0;}
  if(off==0x24c10 || off==0x24c14 || off==0x24c18){if(!kebab_gmu_irq_ao)return 95;*p=kebab_gmu_irq_ao+off-0x24000;return 0;}
 }
 return 22;
}
static int kebab_gmu_irq_read(void *unused,unsigned region,uint32_t off,uint32_t *v)
{(void)unused;vm_offset_t p;int e=kebab_gmu_irq_address(region,off,&p);if(e || !v)return e?e:22;*v=kebab_gmu_irq_raw(p);return 0;}
static int kebab_gmu_irq_write(void *unused,unsigned region,uint32_t off,uint32_t value)
{
 (void)unused;vm_offset_t p;int e=kebab_gmu_irq_address(region,off,&p);if(e)return e;
 if(region==0){if((off!=0x128 && off!=0x1a8) || value!=0x30000u)return 22;}
 else if(off==0x14644){if(!value || (value&~KGI_HFI_OWNED))return 22;}
 else if(off==0x24c10){if(!value || (value&~KGI_AO_OWNED))return 22;}
 else if(off==0x1464c || off==0x24c18){uint32_t mask=off==0x1464c?KGI_HFI_OWNED:KGI_AO_OWNED;
  uint32_t fresh;
  if(__atomic_load_n(&kebab_gmu_irq_state.initial_mask_step,__ATOMIC_ACQUIRE)){
   /* This extra permission exists only during the one normal-worker
    * PREPARING initial set-mask. ISR/later writers cannot claim it. */
   if(__atomic_load_n(&kebab_gmu_irq_state.phase,__ATOMIC_ACQUIRE)!=KGI_PREPARING || kebab_gmu_irq_worker() ||
    !kebab_gmu_irq_hfi || !kebab_gmu_irq_ao || kebab_gmu_irq_private())return 95;
   if(!(kebab_gmu_irq_raw(kebab_gmu_irq_hfi+0x2000)&1))return 95;
   uint32_t h=kebab_gmu_irq_raw(kebab_gmu_irq_hfi+0x2648),a=kebab_gmu_irq_raw(kebab_gmu_irq_ao+0xc14);
   __atomic_store_n(&kebab_gmu_irq_state.last_hfi,h,__ATOMIC_RELEASE);__atomic_store_n(&kebab_gmu_irq_state.last_ao,a,__ATOMIC_RELEASE);
   __atomic_store_n(&kebab_gmu_irq_state.arm_status_valid,1,__ATOMIC_RELEASE);if(h || a)return 13;
   mask=off==0x1464c?KGI_HFI_KNOWN:KGI_AO_KNOWN;
   fresh=kebab_gmu_irq_raw(p); /* last read after prepare/reset/status gates */
   if((fresh^value)&~mask || (fresh|value)!=value || (value&mask)!=mask)return 13;
  }else {fresh=kebab_gmu_irq_raw(p);if((fresh^value)&~mask)return 13;}}
 else return 22;
 __atomic_add_fetch(&kebab_gmu_irq_mmio_writes,1,__ATOMIC_RELAXED);_gic_write32(p,value);return 0;
}
static int kebab_gmu_irq_write64(void *unused,unsigned region,uint32_t off,uint64_t value)
{
 (void)unused;if(region || (off!=0x6a80 && off!=0x6a88) || !gicd_base || value!=kebab_gmu_irq_state.route)return 22;
 __atomic_add_fetch(&kebab_gmu_irq_mmio_writes,1,__ATOMIC_RELAXED);*(volatile uint64_t *)(gicd_base+off)=value;return 0;
}
static int kebab_gmu_irq_write8(void *unused,unsigned region,uint32_t off,uint8_t value)
{(void)unused;if(region || (off!=0x550 && off!=0x551) || value!=0xa0 || !gicd_base)return 22;__atomic_add_fetch(&kebab_gmu_irq_mmio_writes,1,__ATOMIC_RELAXED);*(volatile uint8_t *)(gicd_base+off)=value;return 0;}
static void kebab_gmu_irq_barrier(void *unused){(void)unused;__builtin_arm_dsb(DSB_SY);}
static uint64_t kebab_gmu_irq_now(void *unused){(void)unused;return mach_absolute_time();}
static const KebabGmuIrqIo kebab_gmu_irq_io={NULL,kebab_gmu_irq_read,kebab_gmu_irq_write,kebab_gmu_irq_write64,kebab_gmu_irq_write8,kebab_gmu_irq_barrier,kebab_gmu_irq_power,kebab_gmu_irq_now};
int kebab_gpu_gmu_irq_acquire(const KebabGpuColdToken *cold,uint64_t epoch,KebabGmuIrqToken *token)
{
 int e=kebab_gmu_irq_worker();if(e)return e;if(!cold || !epoch || !token)return 22;
 if(!KebabGmuIrqNativeQualified)return 95;
 if(__atomic_exchange_n(&kebab_gmu_irq_attempt,1,__ATOMIC_ACQ_REL))return 16;
 /* Complete source-bound parent grant precedes every GMU access. Any later
  * failure leaves that pin, MMIO mappings and failed evidence retained. */
 e=kebab_gpu_gmu_irq_parent_pin(cold,epoch,token);if(e)return e;
 kebab_gmu_irq_state.epoch=epoch;__atomic_store_n(&kebab_gmu_irq_identity,token,__ATOMIC_RELEASE);if(!gicd_base)return 95;
 kebab_gmu_irq_gcc=ml_io_map(0x00171000,0x2000);kebab_gmu_irq_cc=ml_io_map(0x03d91000,0x1000);
 if(!kebab_gmu_irq_gcc || !kebab_gmu_irq_cc)return 12;
 kebab_gmu_irq_gdscr=kebab_gmu_irq_raw(kebab_gmu_irq_cc+0x6c)&KebabGpuCcControlMask(2);
 const vm_offset_t p[5]={kebab_gmu_irq_cc+0x78,kebab_gmu_irq_cc+0x98,kebab_gmu_irq_cc+0x9c,kebab_gmu_irq_gcc+0x154,kebab_gmu_irq_gcc+0xc};
 for(unsigned n=0;n<5;n++)kebab_gmu_irq_controls[n]=kebab_gmu_irq_raw(p[n])&0x0fffffffu;
 if(kebab_gmu_irq_power(NULL))return 95;
 kebab_gmu_irq_hfi=ml_io_map(0x03d7c000,0x4000);kebab_gmu_irq_ao=ml_io_map(0x03d8e000,0x1000);
 if(!kebab_gmu_irq_hfi || !kebab_gmu_irq_ao)return 12;
 /* dispatch is linked before acquisition; release-publish installation
  * before device/GIC unmask. No firmware was released by this operation. */
 __atomic_store_n(&kebab_gmu_irq_installed,1,__ATOMIC_RELEASE);
 uint64_t mpidr=__builtin_arm_rsr64("MPIDR_EL1"),route=(mpidr&0xffffffULL)|(mpidr&0xff00000000ULL);
 return KebabGmuIrqArm(&kebab_gmu_irq_io,&kebab_gmu_irq_state,route,epoch);
}
int kebab_gpu_gmu_irq_healthy(const KebabGmuIrqToken *token)
{
 if(!token || token!=__atomic_load_n(&kebab_gmu_irq_identity,__ATOMIC_ACQUIRE) || !__atomic_load_n(&kebab_gmu_irq_installed,__ATOMIC_ACQUIRE) ||
  __atomic_load_n(&kebab_gmu_irq_state.phase,__ATOMIC_ACQUIRE)!=KGI_ARMED)return 95;
 return kebab_gmu_irq_private();
}
int kebab_gpu_gmu_irq_verify_locked(const KebabGmuIrqToken *token,const KebabGpuColdToken *cold,uint64_t epoch)
{
 int e=kebab_gpu_gmu_irq_parent_check_locked(token,cold,epoch);if(e)return e;
 e=kebab_gpu_gmu_irq_healthy(token);if(e)return e;
 if(kebab_gmu_irq_power(NULL))return 95;
 uint32_t en,pri,cf,h,a,hm,am,lo,hi;
 if(kebab_gmu_irq_read(NULL,0,0x128,&en) || kebab_gmu_irq_read(NULL,0,0x550,&pri) || kebab_gmu_irq_read(NULL,0,0xc54,&cf))return 5;
 if((en&0x30000)!=0x30000 || (pri&0xffff)!=0xa0a0 || (cf&0xa))return 13;
 for(unsigned n=0;n<2;n++){if(kebab_gmu_irq_read(NULL,0,0x6a80+8*n,&lo) || kebab_gmu_irq_read(NULL,0,0x6a84+8*n,&hi))return 5;
  if(lo!=(uint32_t)kebab_gmu_irq_state.route || hi!=(uint32_t)(kebab_gmu_irq_state.route>>32))return 13;}
 if(kebab_gmu_irq_read(NULL,1,0x14648,&h) || kebab_gmu_irq_read(NULL,1,0x24c14,&a) ||
  kebab_gmu_irq_read(NULL,1,0x1464c,&hm) || kebab_gmu_irq_read(NULL,1,0x24c18,&am))return 5;
 if((h&~0xffff0007u) || (a&~0x12u))return 14;
 if(hm!=(kebab_gmu_irq_state.hfi_mask_other|6u) || am!=kebab_gmu_irq_state.ao_mask_other)return 13;
 return kebab_gpu_gmu_irq_healthy(token);
}
int kebab_gpu_gmu_irq_dispatch(uint32_t intid)
{return KebabGmuIrqHandle(&kebab_gmu_irq_io,&kebab_gmu_irq_state,intid);}
int kebab_gpu_gmu_irq_mask(const KebabGmuIrqToken *token)
{
 int e=kebab_gmu_irq_worker();if(e)return e;if(!token || token!=__atomic_load_n(&kebab_gmu_irq_identity,__ATOMIC_ACQUIRE))return 13;
 if(__atomic_exchange_n(&kebab_gmu_irq_worker_busy,1,__ATOMIC_ACQ_REL))return 16;
 e=KebabGmuIrqMask(&kebab_gmu_irq_io,&kebab_gmu_irq_state);__atomic_store_n(&kebab_gmu_irq_worker_busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_gpu_gmu_irq_take(const KebabGmuIrqToken *token,KebabGmuIrqEvent *out)
{int e=kebab_gmu_irq_worker();if(e)return e;if(!token || token!=__atomic_load_n(&kebab_gmu_irq_identity,__ATOMIC_ACQUIRE) || !out)return 22;return KebabGmuIrqTake(&kebab_gmu_irq_state,out);}
int kebab_gpu_gmu_irq_snapshot(KebabGmuIrqRecord *r)
{
 if(!r)return 22;memset(r,0,sizeof *r);r->magic=0x494d474b;r->schema=1;r->bytes=sizeof *r;
 uint32_t start=__atomic_load_n(&kebab_gmu_irq_state.dispatches,__ATOMIC_ACQUIRE),busy=__atomic_load_n(&kebab_gmu_irq_state.irq_busy,__ATOMIC_ACQUIRE);
 r->phase=__atomic_load_n(&kebab_gmu_irq_state.phase,__ATOMIC_ACQUIRE);r->reason=__atomic_load_n(&kebab_gmu_irq_state.reason,__ATOMIC_ACQUIRE);
 r->installed=__atomic_load_n(&kebab_gmu_irq_installed,__ATOMIC_ACQUIRE);r->armed=r->phase==KGI_ARMED;r->intid_hfi=336;r->intid_ao=337;
 r->touched=__atomic_load_n(&kebab_gmu_irq_state.touched,__ATOMIC_ACQUIRE);r->reads=__atomic_load_n(&kebab_gmu_irq_state.reads,__ATOMIC_RELAXED);
 r->writes=__atomic_load_n(&kebab_gmu_irq_state.writes,__ATOMIC_RELAXED);r->dispatches=__atomic_load_n(&kebab_gmu_irq_state.dispatches,__ATOMIC_RELAXED);
 r->handler_observed=r->dispatches!=0;r->overflow=__atomic_load_n(&kebab_gmu_irq_state.overflow,__ATOMIC_RELAXED);
 r->containment_ack=__atomic_load_n(&kebab_gmu_irq_state.containment_ack,__ATOMIC_ACQUIRE);
 uint32_t tail=__atomic_load_n(&kebab_gmu_irq_state.pending_tail,__ATOMIC_ACQUIRE),head=__atomic_load_n(&kebab_gmu_irq_state.pending_head,__ATOMIC_ACQUIRE);
 r->pending_events=head-tail;if(r->pending_events>KGI_RING)r->pending_events=KGI_RING;
 r->hfi_status=__atomic_load_n(&kebab_gmu_irq_state.last_hfi,__ATOMIC_ACQUIRE);r->ao_status=__atomic_load_n(&kebab_gmu_irq_state.last_ao,__ATOMIC_ACQUIRE);
 const KebabGmuIrqToken *identity=__atomic_load_n(&kebab_gmu_irq_identity,__ATOMIC_ACQUIRE);if(identity){r->epoch=identity->epoch;r->generation=identity->generation;}
 /* Pure GIC RO qualification works even with binding0/no clocks. Snapshot
  * calls never access GMU before the retained powered owner exists. */
 if(gicd_base){r->gic_ctlr=kebab_gmu_irq_raw(gicd_base);r->gic_typer=kebab_gmu_irq_raw(gicd_base+4);r->enable=kebab_gmu_irq_raw(gicd_base+0x128);r->pending=kebab_gmu_irq_raw(gicd_base+0x228);r->active=kebab_gmu_irq_raw(gicd_base+0x328);
  r->config=kebab_gmu_irq_raw(gicd_base+0xc54);r->priorities=kebab_gmu_irq_raw(gicd_base+0x550);for(unsigned n=0;n<2;n++){r->route_low[n]=kebab_gmu_irq_raw(gicd_base+0x6a80+8*n);r->route_high[n]=kebab_gmu_irq_raw(gicd_base+0x6a84+8*n);}r->valid_mask|=1;}
 if(identity && kebab_gmu_irq_hfi && kebab_gmu_irq_ao && !kebab_gmu_irq_power(NULL)){
  r->hfi_mask=kebab_gmu_irq_raw(kebab_gmu_irq_hfi+0x264c);r->ao_mask=kebab_gmu_irq_raw(kebab_gmu_irq_ao+0xc18);r->valid_mask|=2;}
 if(r->handler_observed)r->valid_mask|=4;
 if(__atomic_load_n(&kebab_gmu_irq_state.arm_status_valid,__ATOMIC_ACQUIRE))r->valid_mask|=8;
 r->mmio_reads=__atomic_load_n(&kebab_gmu_irq_mmio_reads,__ATOMIC_RELAXED);r->mmio_writes=__atomic_load_n(&kebab_gmu_irq_mmio_writes,__ATOMIC_RELAXED);
 if(busy || __atomic_load_n(&kebab_gmu_irq_state.irq_busy,__ATOMIC_ACQUIRE) || start!=__atomic_load_n(&kebab_gmu_irq_state.dispatches,__ATOMIC_ACQUIRE) ||
  r->phase!=__atomic_load_n(&kebab_gmu_irq_state.phase,__ATOMIC_ACQUIRE)){r->result=16;r->armed=0;return 16;}
 r->result=0;return 0;
}
