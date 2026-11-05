/* SPDX-License-Identifier: MIT */
/* AFTER real FirmwareOwner.inc and BEFORE HfiOwner.inc, SAME canonical TU.
 * The sealed reset/prelaunch Owner.inc is unchanged. All evidence below is
 * its and FirmwareOwner's private production ledger, never a CAP argument. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuLaunchResourceAfterFw.h>
#else
#include "KebabGpuLaunchResourceAfterFw.h"
#endif
static int kebab_launch_after_fw_clock_gate_locked(void)
{
 int e;uint32_t v;
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) ||
    __atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE) || kebab_launch_ledger.quarantined)return 95;
 e=kebab_gpu_gmu_boot_gate_locked(kebab_launch_cold,kebab_launch_epoch);if(e)return e;
 if((e=kebab_gpu_bus_verify_locked()))return e;
 const unsigned region[3]={KLR_GCC,KLR_CC,KLR_CC};const uint32_t off[3]={0x71154,0x109c,0x1098};
 for(unsigned i=0;i<3;i++){
  e=kebab_launch_native_io.read32(kebab_launch_native_io.cookie,region[i],off[i],&v);if(e)return e;
  /* Only the post-FW CM3 running-status sample is not a permanent CSR gate.
   * Every owned control bit and request remains exactly held. */
  if((v&0x0fffffffu)!=kebab_gmu_boot_ledger.control[i] || !(v&1) || (i!=2 && !KebabGmuBootRunning(v)))return 11;
 }
 for(unsigned i=0;i<2;i++){
  const uint32_t off2[2]={0x1120,0x1124};e=kebab_launch_native_io.read32(kebab_launch_native_io.cookie,KLR_CC,off2[i],&v);if(e)return e;
  if(i?((v&0x10371fu)!=0x100602u || (v&0x3000u)):((v&0x800000f3u)!=2))return 11;
 }return 0;
}
int kebab_gpu_launch_resources_verify_after_fw_locked(const KebabGpuLaunchResourceToken *token,
 const KebabGpuColdToken *cold,uint64_t epoch,unsigned required)
{
 if(!required || (required&~(KLR_NEED_RSC|KLR_NEED_BW)))return 22;
 if(!__atomic_load_n(&kebab_rpmh_global_lock.busy,__ATOMIC_ACQUIRE) ||
    __atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE))return 95;
 if(!token || token!=kebab_launch_identity || cold!=kebab_launch_cold || epoch!=kebab_launch_epoch ||
    !token->held || !token->generation || token->generation!=kebab_launch_generation || token->namespace_epoch!=epoch ||
    kebab_launch_record.result || kebab_launch_record.state!=KLR_READY || (token->ready_mask&required)!=required ||
    token->ready_mask!=kebab_launch_ledger.ready_mask || token->bandwidth_mode!=KLR_BW_RETAIN_INHERITED_NO_WRITE)return 95;
 if(token!=kebab_gmu_firmware_resources || !kebab_gmu_firmware_attempted ||
    __atomic_load_n(&kebab_gmu_firmware_active,__ATOMIC_ACQUIRE) || !kebab_gmu_firmware_irq ||
    kebab_gmu_boot_ledger.phase!=KGB_HFI_QUEUES_READY || !kebab_gmu_boot_ledger.started ||
    kebab_gmu_firmware_evidence.phase!=KGB_HFI_QUEUES_READY || kebab_gmu_firmware_evidence.result ||
    kebab_gmu_firmware_evidence.owner_dead || kebab_gmu_firmware_evidence.quarantined ||
    kebab_gmu_firmware_evidence.namespace_epoch!=epoch || !kebab_gmu_firmware_evidence.firmware_start_requested ||
    !kebab_gmu_firmware_evidence.fw_init_ack || !kebab_gmu_firmware_evidence.hfi_ctrl_ack)return 95;
 int e=kebab_launch_after_fw_clock_gate_locked();
 if(e){kebab_launch_ledger.quarantined=1;kebab_gmu_boot_quarantine((unsigned)e);}return e;
}
