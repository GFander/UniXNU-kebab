/* SPDX-License-Identifier: MIT */
/* Include once AFTER actual GmuBootOwner.inc in the SAME canonical VRM TU.
 * Uses its globalbusy/dead, RootChild, exact cold identity/namespace/phase,
 * live bus/clock ledgers. No second RSC lock or owner/rail reference book. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuLaunchResourceOwner.h>
#else
#include "KebabGpuLaunchResourceOwner.h"
#endif
static KebabGpuLaunchResourceLedger kebab_launch_ledger;
static KebabGpuLaunchResourceRecord kebab_launch_record;
static KebabGpuLaunchResourceToken *kebab_launch_identity;
static const KebabGpuColdToken *kebab_launch_cold;
static uint64_t kebab_launch_epoch,kebab_launch_generation;
static KebabGpuLaunchResourceIo kebab_launch_native_io;
static int kebab_launch_clock_gate_locked(unsigned reset_held,KebabGpuLaunchResourceRecord *metrics)
{
 int e;uint32_t v;
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) ||
    __atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE) || kebab_launch_ledger.quarantined)return 95;
 e=reset_held?kebab_gpu_gmu_boot_reset_held_locked(kebab_launch_cold,kebab_launch_epoch):
  kebab_gpu_gmu_boot_gate_locked(kebab_launch_cold,kebab_launch_epoch);if(e)return e;
 if((e=kebab_gpu_bus_verify_locked()))return e;
 const unsigned region[3]={KLR_GCC,KLR_CC,KLR_CC};const uint32_t off[3]={0x71154,0x109c,0x1098};
 for(unsigned i=0;i<3;i++){
  if(metrics)metrics->reads++;e=kebab_launch_native_io.read32(kebab_launch_native_io.cookie,region[i],off[i],&v);
  if(e){if(metrics){metrics->io_error=e;metrics->failed_region=region[i];metrics->failed_offset=off[i];}return e;}
  if((v&0x0fffffffu)!=kebab_gmu_boot_ledger.control[i] || !(v&1) || !KebabGmuBootRunning(v))return 11;
 }
 for(unsigned i=0;i<2;i++){
  const uint32_t off2[2]={0x1120,0x1124};if(metrics)metrics->reads++;
  e=kebab_launch_native_io.read32(kebab_launch_native_io.cookie,KLR_CC,off2[i],&v);if(e){if(metrics)metrics->io_error=e;return e;}
  if(i?((v&0x10371fu)!=0x100602u || (v&0x3000u)):((v&0x800000f3u)!=2))return 11;
 }return 0;
}
static int kebab_launch_reset_gate(void *unused)
{(void)unused;return kebab_launch_clock_gate_locked(1,&kebab_launch_record);}
static int kebab_launch_read(void *unused,unsigned region,uint32_t off,uint32_t *out)
{(void)unused;return kebab_launch_native_io.read32(kebab_launch_native_io.cookie,region,off,out);}
static int kebab_launch_write(void *unused,unsigned region,uint32_t off,uint32_t value)
{(void)unused;return kebab_launch_native_io.write32(kebab_launch_native_io.cookie,region,off,value);}
static int kebab_launch_barrier(void *unused)
{(void)unused;return kebab_launch_native_io.barrier(kebab_launch_native_io.cookie);}
static int kebab_launch_delay(void *unused,unsigned us)
{(void)unused;return kebab_launch_native_io.delay_us(kebab_launch_native_io.cookie,us);}
static uint64_t kebab_launch_now(void *unused)
{(void)unused;return kebab_launch_native_io.now_us(kebab_launch_native_io.cookie);}
int kebab_rpmh_gpu_launch_resources_prepare(const KebabGpuColdToken *cold,uint64_t epoch,
 KebabGpuLaunchResourceToken *token,const KebabGpuLaunchResourceIo *io,KebabGpuLaunchResourceRecord *out)
{
 int e=95;if(!out || !cold || !token || !epoch || !io || !io->read32 || !io->write32 || !io->barrier || !io->delay_us || !io->now_us)return 22;
 KLRInit(out);if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)){out->result=16;return 16;}
 if(kebab_launch_ledger.attempted){
  if(token==kebab_launch_identity && cold==kebab_launch_cold && epoch==kebab_launch_epoch){*out=kebab_launch_record;e=(int)out->result;}
  goto done;
 }
 if(token->held || token->generation || kebab_launch_generation==UINT64_MAX){e=22;goto done;}
 kebab_launch_cold=cold;kebab_launch_epoch=epoch;kebab_launch_native_io=*io;
 e=kebab_launch_clock_gate_locked(1,NULL);if(e)goto done;
 kebab_launch_identity=token;token->held=1;token->generation=++kebab_launch_generation;token->namespace_epoch=epoch;
 {
  KebabGpuLaunchResourceIo actual={NULL,kebab_launch_reset_gate,kebab_launch_read,kebab_launch_write,kebab_launch_barrier,kebab_launch_delay,kebab_launch_now};
  e=KLRPrepareRsc(&actual,&kebab_launch_ledger,&kebab_launch_record);
 }
 if(kebab_launch_ledger.quarantined)kebab_gmu_boot_quarantine((unsigned)e);
 if(!e){
  /* Actual source treats initial bootstrap BW as nonfatal. This finite
   * boot mode preserves every inherited APPS/other-voter vote unchanged.
   * It does NOT grant NEED_BW or claim any new DDR/CNOC command/ACK. */
  token->ready_mask=KLR_NEED_RSC;token->bandwidth_mode=KLR_BW_RETAIN_INHERITED_NO_WRITE;
  kebab_launch_record.bandwidth_mode=KLR_BW_RETAIN_INHERITED_NO_WRITE;
  kebab_launch_record.bandwidth_qualified=0;kebab_launch_record.initial_bw_level=2;
  kebab_launch_record.ddr_ib_kbps=1171875;kebab_launch_record.cnoc_ib_kbps=100;
  kebab_launch_record.state=KLR_READY;
 }
 *out=kebab_launch_record;
done:
 out->result=e;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return e;
}
int kebab_gpu_launch_resources_verify_locked(const KebabGpuLaunchResourceToken *token,const KebabGpuColdToken *cold,uint64_t epoch,unsigned required)
{
 if(!required || (required&~(KLR_NEED_RSC|KLR_NEED_BW)))return 22;
 if(!token || token!=kebab_launch_identity || cold!=kebab_launch_cold || epoch!=kebab_launch_epoch ||
    !token->held || !token->generation || token->generation!=kebab_launch_generation || token->namespace_epoch!=epoch ||
    kebab_launch_record.result || kebab_launch_record.state!=KLR_READY || (token->ready_mask&required)!=required ||
    token->ready_mask!=kebab_launch_ledger.ready_mask || token->bandwidth_mode!=KLR_BW_RETAIN_INHERITED_NO_WRITE)return 95;
 int e=kebab_launch_clock_gate_locked(0,NULL);
 if(e && __atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE)){
  kebab_launch_ledger.quarantined=1;kebab_gmu_boot_quarantine((unsigned)e);
 }return e;
}
int kebab_rpmh_gpu_launch_resources_snapshot(KebabGpuLaunchResourceRecord *out)
{
 if(!out)return 22;if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL))return 16;
 if(!kebab_launch_ledger.attempted){KLRInit(out);out->result=35;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return 35;}
 *out=kebab_launch_record;__atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);return 0;
}
