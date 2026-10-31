#include <cstdint>
#include <cassert>
#include <cstdio>
using boolean_t=bool;static unsigned bt,hw,clk,uart,calls;static struct{const char *inst_path;}ud;
static int kebab_bluetooth_clock_prepare(...){++clk;return 0;}
static int kebab_bluetooth_firmware_report(...){++bt;return 0;}
static int kebab_bluetooth_hardware_report(...){++hw;return 0;}
static int kebab_bluetooth_uart_loopback(...){++uart;return 0;}
static int kebab_driver_report(...){++calls;return 0;}
static int kebab_fwlog_report(...){++calls;return 0;}
static int kebab_install_check(...){++calls;return 0;}
static int kebab_install_status(...){++calls;return 0;}
static int kebab_install_sync_for_reset(...){++calls;return 1;}
static int kebab_install_sync_report(...){++calls;return 0;}
static int kebab_pid1_report(...){++calls;return 0;}
static int kebab_process_report(...){++calls;return 0;}
static int kebab_qsee_log_report(...){++calls;return 0;}
static int kebab_reboot_fastboot(...){++calls;return 0;}
static int kebab_reboot_imem_report(...){++calls;return 0;}
static int kebab_reboot_plain(...){++calls;return 0;}
static int kebab_reboot_psci_negative_report(...){++calls;return 0;}
static int kebab_reboot_psci_report(...){++calls;return 0;}
static int kebab_rpmh_report(...){++calls;return 0;}
static int kebab_spmi_pon_report(...){++calls;return 0;}
static int kebab_spmi_power_report(...){++calls;return 0;}
static int kebab_spmi_report(...){++calls;return 0;}
static int kebab_spmi_vrm_enable(...){++calls;return 0;}
static int kebab_tcs_report(...){++calls;return 0;}
static int kebab_touch_clock_enable(...){++calls;return 0;}
static int kebab_touch_init_now(...){++calls;return 0;}
static int kebab_touch_ping(...){++calls;return 0;}
static int kebab_touch_probe(...){++calls;return 0;}
static int kebab_touch_se(...){++calls;return 0;}
static int kebab_vol_status(...){++calls;return 0;}
static int ud_inst_arm(...){++calls;return 0;}
static int ud_vol_arm(...){++calls;return 0;}
#define UD_REBOOT_PLAIN         1u
#define UD_REBOOT_FASTBOOT      2u
#define UD_REBOOT_FASTBOOT2     3u
#define UD_REBOOT_PSCI_REPORT   4u
#define UD_REBOOT_IMEM_REPORT   5u
#define UD_REBOOT_PSCI_NEGATIVE 6u
#define UD_REBOOT_SPMI_REPORT   7u
#define UD_REBOOT_PON_REPORT    8u
#define UD_REBOOT_TOUCH_PROBE   9u
#define UD_REBOOT_TOUCH_INIT    10u
#define UD_REBOOT_TOUCH_CLOCK   11u
#define UD_REBOOT_TOUCH_SE      12u
#define UD_REBOOT_TOUCH_PING    13u
#define UD_REBOOT_PWR_REPORT    14u
#define UD_REBOOT_RPMH_REPORT   15u
#define UD_REBOOT_FWLOG_REPORT  16u
#define UD_REBOOT_FWLOG_ALL     17u
#define KEBAB_FWLOG_PANIC_MAX   0x8000u
#define UD_REBOOT_VRMON_LDOA13  18u
#define UD_REBOOT_VRMON_LDOC1   19u
#define UD_REBOOT_VRMON_OTHER   20u
#define UD_REBOOT_TCS_REPORT    21u
#define UD_REBOOT_INST_BEGIN    22u
#define UD_REBOOT_INST_DRY      23u
#define UD_REBOOT_INST_STATUS   24u
#define UD_REBOOT_INST_CHECK    25u
#define UD_REBOOT_VOL_BEGIN     26u
#define UD_REBOOT_VOL_DRY       27u
#define UD_REBOOT_VOL_STATUS    28u
#define UD_REBOOT_SYNC          29u
#define UD_REBOOT_DRV_REPORT    30u
#define UD_REBOOT_PROC_REPORT   31u
#define UD_REBOOT_PID1_REPORT   32u
#define UD_REBOOT_QSEELOG_REPORT 33u
#define UD_REBOOT_BTFW_REPORT 34u
#define UD_REBOOT_BTHW_REPORT 35u
#define UD_REBOOT_BTCLK_PREPARE 36u
#define UD_REBOOT_BTUART_LOOPBACK 37u
#define printf(...) ((void)0)
static void
ud_run_reboot_command(uint32_t command, boolean_t panicking)
{
	if (panicking) {
		if (command == UD_REBOOT_FASTBOOT || command == UD_REBOOT_FASTBOOT2 ||
		    command == UD_REBOOT_PLAIN) {
			printf("KEBAB PANIC: reset requested (%s); skipping filesystem sync\n",
			    command == UD_REBOOT_FASTBOOT ? "FASTBOOT" :
			    command == UD_REBOOT_FASTBOOT2 ? "FASTBOOT2" : "REBOOT");
			/* PON and IMEM were mapped during normal USB startup. Their
			 * bounded MMIO paths need no allocation or driver/VFS locks.
			 * Preserve the requested boot target instead of degrading
			 * FASTBOOT to an ordinary restart of the panicking kernel. */
			if (command == UD_REBOOT_PLAIN) {
				(void)kebab_reboot_plain();
			} else {
				kebab_reboot_fastboot(command == UD_REBOOT_FASTBOOT2);
			}
			return;
		}
		/*
		 * Reading the firmware ring is the one diagnostic that is safe
		 * here.  kebab_fwlog_attach() already ran at boot, so it is a
		 * no-op, and the report path only copies from the mapped window
		 * and writes to the console - no locks, no allocation, no sleep.
		 * Panicking would otherwise destroy the only record of what the
		 * kernel printed before the fault, because the ring is small and
		 * the panic dump overwrites it.
		 */
		if (command == UD_REBOOT_FWLOG_REPORT || command == UD_REBOOT_FWLOG_ALL) {
			kebab_fwlog_report(command == UD_REBOOT_FWLOG_ALL ? 0 :
			    KEBAB_FWLOG_PANIC_MAX);
			return;
		}
		printf("KEBAB PANIC: command 0x%x refused while panicking; it can "
		    "sleep or take a mutex, and only a reset is safe here\n", command);
		return;
	}
	/*
	 * A console-requested reset is the transition from the install phase to
	 * the next boot.  Make every written byte durable before the PSCI call:
	 * VFS_SYNC flushes the HFS+ metadata/data still in the UBC and
	 * SYNCHRONIZE CACHE flushes the drive's write cache.  This is the
	 * non-panic arm only; the panic pump takes the branch above and never
	 * waits for a filesystem sync.
	 */
	if (command == UD_REBOOT_FASTBOOT || command == UD_REBOOT_FASTBOOT2 ||
	    command == UD_REBOOT_PLAIN) {
		const char *why = command == UD_REBOOT_FASTBOOT ? "FASTBOOT" :
		    command == UD_REBOOT_FASTBOOT2 ? "FASTBOOT2" : "REBOOT";

		if (!kebab_install_sync_for_reset(why)) {
			/* Do not turn a failed flush into an unattributable boot. */
			return;
		}
	}
	if (command == UD_REBOOT_FASTBOOT) {
		kebab_reboot_fastboot(false);
	} else if (command == UD_REBOOT_FASTBOOT2) {
		kebab_reboot_fastboot(true);
	} else if (command == UD_REBOOT_PLAIN) {
		(void)kebab_reboot_plain();
	} else if (command == UD_REBOOT_PSCI_REPORT) {
		kebab_reboot_psci_report();
	} else if (command == UD_REBOOT_PSCI_NEGATIVE) {
		kebab_reboot_psci_negative_report();
	} else if (command == UD_REBOOT_IMEM_REPORT) {
		kebab_reboot_imem_report();
	} else if (command == UD_REBOOT_PROC_REPORT) {
		kebab_process_report();
	} else if (command == UD_REBOOT_PID1_REPORT) {
		kebab_pid1_report();
	} else if (command == UD_REBOOT_QSEELOG_REPORT) {
		kebab_qsee_log_report();
	} else if (command == UD_REBOOT_BTUART_LOOPBACK) {
		kebab_bluetooth_uart_loopback();
	} else if (command == UD_REBOOT_BTCLK_PREPARE) {
		kebab_bluetooth_clock_prepare();
	} else if (command == UD_REBOOT_BTHW_REPORT) {
		kebab_bluetooth_hardware_report();
	} else if (command == UD_REBOOT_BTFW_REPORT) {
		kebab_bluetooth_firmware_report();
	} else if (command == UD_REBOOT_DRV_REPORT) {
		kebab_driver_report();
	} else if (command == UD_REBOOT_SPMI_REPORT) {
		kebab_spmi_report();
	} else if (command == UD_REBOOT_PON_REPORT) {
		kebab_spmi_pon_report();
	} else if (command == UD_REBOOT_TOUCH_PROBE) {
		kebab_touch_probe();
	} else if (command == UD_REBOOT_TOUCH_INIT) {
		kebab_touch_init_now();
	} else if (command == UD_REBOOT_TOUCH_CLOCK) {
		kebab_touch_clock_enable();
	} else if (command == UD_REBOOT_TOUCH_SE) {
		kebab_touch_se();
	} else if (command == UD_REBOOT_TOUCH_PING) {
		kebab_touch_ping();
	} else if (command == UD_REBOOT_PWR_REPORT) {
		kebab_spmi_power_report();
	} else if (command == UD_REBOOT_RPMH_REPORT) {
		kebab_rpmh_report();
	} else if (command == UD_REBOOT_VRMON_LDOA13) {
		kebab_spmi_vrm_enable("ldoa13");
	} else if (command == UD_REBOOT_VRMON_LDOC1) {
		kebab_spmi_vrm_enable("ldoc1");
	} else if (command == UD_REBOOT_VRMON_OTHER) {
		/* Outside the whitelist: name the two forms, send nothing. */
		kebab_spmi_vrm_enable(NULL);
	} else if (command == UD_REBOOT_TCS_REPORT) {
		kebab_tcs_report();
	} else if (command == UD_REBOOT_FWLOG_REPORT) {
		kebab_fwlog_report(4096);
	} else if (command == UD_REBOOT_FWLOG_ALL) {
		kebab_fwlog_report(0);
	} else if (command == UD_REBOOT_SYNC) {
		kebab_install_sync_report();
	} else if (command == UD_REBOOT_INST_BEGIN) {
		ud_inst_arm(false);
	} else if (command == UD_REBOOT_INST_DRY) {
		ud_inst_arm(true);
	} else if (command == UD_REBOOT_INST_STATUS) {
		kebab_install_status();
	} else if (command == UD_REBOOT_INST_CHECK) {
		kebab_install_check(ud.inst_path);
	} else if (command == UD_REBOOT_VOL_BEGIN) {
		ud_vol_arm(false);
	} else if (command == UD_REBOOT_VOL_DRY) {
		ud_vol_arm(true);
	} else if (command == UD_REBOOT_VOL_STATUS) {
		kebab_vol_status();
	}
}

#undef printf

int main(){
 for(unsigned command=4;command<=UD_REBOOT_BTUART_LOOPBACK;++command){
  if(command==UD_REBOOT_FWLOG_REPORT||command==UD_REBOOT_FWLOG_ALL)continue;
  bt=hw=clk=uart=calls=0;ud_run_reboot_command(command,true);assert(!bt&&!hw&&!clk&&!uart&&!calls);
 }
 bt=hw=clk=uart=calls=0;ud_run_reboot_command(UD_REBOOT_BTFW_REPORT,false);assert(bt==1&&!hw&&!clk&&!calls);
 bt=hw=clk=uart=calls=0;ud_run_reboot_command(UD_REBOOT_BTHW_REPORT,false);assert(hw==1&&!bt&&!clk&&!calls);
 bt=hw=clk=uart=calls=0;ud_run_reboot_command(UD_REBOOT_BTCLK_PREPARE,false);assert(clk==1&&!bt&&!hw&&!calls);
 bt=hw=clk=uart=calls=0;ud_run_reboot_command(UD_REBOOT_BTUART_LOOPBACK,false);assert(uart==1&&!bt&&!hw&&!clk&&!calls);
 printf("PASS actual complete USB dispatcher: BTFW/BTHW/BTCLK/BTUART normal-only, panic rejects all unsafe diagnostics\n");
}
