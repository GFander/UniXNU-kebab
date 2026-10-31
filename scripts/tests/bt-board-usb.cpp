#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <initializer_list>
using boolean_t=bool;static unsigned bt,hw,clk,uart,pwr,board,calls;static struct{const char *inst_path;}ud;
static bool kdp;static bool kebab_usb_kdp_enabled(){return kdp;}static uint32_t ud_inst_command(const char *,uint32_t){return 0;}static uint32_t ud_vol_command(const char *,uint32_t){return 0;}
static int kebab_bluetooth_clock_prepare(...){++clk;return 0;}
static int kebab_bluetooth_firmware_report(...){++bt;return 0;}
static int kebab_bluetooth_hardware_report(...){++hw;return 0;}
static int kebab_bluetooth_power_report(...){++pwr;return 0;}
static int kebab_bluetooth_uart_loopback(...){++uart;return 0;}
static int kebab_board_report(...){++board;return 0;}
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
#define UD_REBOOT_BTPWR_REPORT 38u
#define UD_REBOOT_BTBOARD_REPORT 39u
#define printf(...) ((void)0)
static bool
ud_command_seen(const char *buf, uint32_t received, const char *command,
    uint32_t length)
{
	if (received < length) return false;
	for (uint32_t i = 0; i + length <= received; i++) {
		if (!memcmp(buf + i, command, length)) return true;
	}
	return false;
}
static uint32_t
ud_reboot_command(const char *buf, uint32_t received)
{
	if (!kebab_usb_kdp_enabled()) {
		/*
		 * FASTBOOT2 and FASTBOOT share a prefix; the longer command has
		 * to be tested first or its bytes would never reach it.
		 */
		if (ud_command_seen(buf, received, "REBOOT\n", sizeof("REBOOT\n") - 1)) {
			return UD_REBOOT_PLAIN;
		}
		if (ud_command_seen(buf, received, "SYNC\n", sizeof("SYNC\n") - 1)) {
			return UD_REBOOT_SYNC;
		}
		if (ud_command_seen(buf, received, "FASTBOOT2\n", sizeof("FASTBOOT2\n") - 1)) {
			return UD_REBOOT_FASTBOOT2;
		}
		if (ud_command_seen(buf, received, "FASTBOOT\n", sizeof("FASTBOOT\n") - 1)) {
			return UD_REBOOT_FASTBOOT;
		}
		if (ud_command_seen(buf, received, "PSCI?\n", sizeof("PSCI?\n") - 1)) {
			return UD_REBOOT_PSCI_REPORT;
		}
		if (ud_command_seen(buf, received, "PSCINEG?\n", sizeof("PSCINEG?\n") - 1)) {
			return UD_REBOOT_PSCI_NEGATIVE;
		}
		if (ud_command_seen(buf, received, "IMEM?\n", sizeof("IMEM?\n") - 1)) {
			return UD_REBOOT_IMEM_REPORT;
		}
		if (ud_command_seen(buf, received, "SPMI?\n", sizeof("SPMI?\n") - 1)) {
			return UD_REBOOT_SPMI_REPORT;
		}
		if (ud_command_seen(buf, received, "PON?\n", sizeof("PON?\n") - 1)) {
			return UD_REBOOT_PON_REPORT;
		}
		/*
		 * VROMON powers one rail per command: "VROMON ldoc1" must be
		 * tried before the bare form, and anything else that starts
		 * with VROMON is answered with the whitelist instead of being
		 * silently ignored.
		 */
		if (ud_command_seen(buf, received, "VRMON ldoc1\n",
		    sizeof("VRMON ldoc1\n") - 1)) {
			return UD_REBOOT_VRMON_LDOC1;
		}
		if (ud_command_seen(buf, received, "VRMON\n",
		    sizeof("VRMON\n") - 1)) {
			return UD_REBOOT_VRMON_LDOA13;
		}
		if (ud_command_seen(buf, received, "VRMON",
		    sizeof("VRMON") - 1)) {
			return UD_REBOOT_VRMON_OTHER;
		}
		if (ud_command_seen(buf, received, "TCS?\n",
		    sizeof("TCS?\n") - 1)) {
			return UD_REBOOT_TCS_REPORT;
		}
		if (ud_command_seen(buf, received, "TOUCH?\n",
		    sizeof("TOUCH?\n") - 1)) {
			return UD_REBOOT_TOUCH_PROBE;
		}
		if (ud_command_seen(buf, received, "TOUCHINIT\n",
		    sizeof("TOUCHINIT\n") - 1)) {
			return UD_REBOOT_TOUCH_INIT;
		}
		if (ud_command_seen(buf, received, "TOUCHCLK\n",
		    sizeof("TOUCHCLK\n") - 1)) {
			return UD_REBOOT_TOUCH_CLOCK;
		}
		if (ud_command_seen(buf, received, "TOUCHSE\n",
		    sizeof("TOUCHSE\n") - 1)) {
			return UD_REBOOT_TOUCH_SE;
		}
		if (ud_command_seen(buf, received, "TOUCHPING\n",
		    sizeof("TOUCHPING\n") - 1)) {
			return UD_REBOOT_TOUCH_PING;
		}
		/* BTPWR contains the older PWR token. Match the specific command
		 * before the completion scanner can find that suffix. */
		if (ud_command_seen(buf, received, "BTBOARD?\n", sizeof("BTBOARD?\n") - 1)) {
			return UD_REBOOT_BTBOARD_REPORT;
		}
		if (ud_command_seen(buf, received, "BTPWR?\n", sizeof("BTPWR?\n") - 1)) {
			return UD_REBOOT_BTPWR_REPORT;
		}
		if (ud_command_seen(buf, received, "PWR?\n",
		    sizeof("PWR?\n") - 1)) {
			return UD_REBOOT_PWR_REPORT;
		}
		if (ud_command_seen(buf, received, "RPMH?\n",
		    sizeof("RPMH?\n") - 1)) {
			return UD_REBOOT_RPMH_REPORT;
		}
		if (ud_command_seen(buf, received, "PROC?\n", sizeof("PROC?\n") - 1)) {
			return UD_REBOOT_PROC_REPORT;
		}
		if (ud_command_seen(buf, received, "PID1?\n", sizeof("PID1?\n") - 1)) {
			return UD_REBOOT_PID1_REPORT;
		}
		if (ud_command_seen(buf, received, "DRV?\n",
		    sizeof("DRV?\n") - 1)) {
			return UD_REBOOT_DRV_REPORT;
		}
		if (ud_command_seen(buf, received, "BTUART\n", sizeof("BTUART\n") - 1)) {
			return UD_REBOOT_BTUART_LOOPBACK;
		}
		if (ud_command_seen(buf, received, "BTCLK\n", sizeof("BTCLK\n") - 1)) {
			return UD_REBOOT_BTCLK_PREPARE;
		}
		if (ud_command_seen(buf, received, "BTHW?\n", sizeof("BTHW?\n") - 1)) {
			return UD_REBOOT_BTHW_REPORT;
		}
		if (ud_command_seen(buf, received, "BTFW?\n", sizeof("BTFW?\n") - 1)) {
			return UD_REBOOT_BTFW_REPORT;
		}
		if (ud_command_seen(buf, received, "QSEELOG?\n", sizeof("QSEELOG?\n") - 1)) {
			return UD_REBOOT_QSEELOG_REPORT;
		}
		/* "FWLOG? all\n" must be tried before the bare form. */
		if (ud_command_seen(buf, received, "FWLOG? all\n",
		    sizeof("FWLOG? all\n") - 1)) {
			return UD_REBOOT_FWLOG_ALL;
		}
		if (ud_command_seen(buf, received, "FWLOG?\n",
		    sizeof("FWLOG?\n") - 1)) {
			return UD_REBOOT_FWLOG_REPORT;
		}
		{
			uint32_t inst = ud_inst_command(buf, received);
			if (inst != 0) return inst;
		}
		{
			uint32_t vol = ud_vol_command(buf, received);
			if (vol != 0) return vol;
		}
		return 0;
	}
	if (received >= sizeof("REBOOT\n") - 1 &&
	    !memcmp(buf, "REBOOT\n", sizeof("REBOOT\n") - 1)) {
		return UD_REBOOT_PLAIN;
	}
	if (received >= sizeof("SYNC\n") - 1 &&
	    !memcmp(buf, "SYNC\n", sizeof("SYNC\n") - 1)) {
		return UD_REBOOT_SYNC;
	}
	if (received >= sizeof("FASTBOOT2\n") - 1 &&
	    !memcmp(buf, "FASTBOOT2\n", sizeof("FASTBOOT2\n") - 1)) {
		return UD_REBOOT_FASTBOOT2;
	}
	if (received >= sizeof("FASTBOOT\n") - 1 &&
	    !memcmp(buf, "FASTBOOT\n", sizeof("FASTBOOT\n") - 1)) {
		return UD_REBOOT_FASTBOOT;
	}
	if (received >= sizeof("PSCI?\n") - 1 &&
	    !memcmp(buf, "PSCI?\n", sizeof("PSCI?\n") - 1)) {
		return UD_REBOOT_PSCI_REPORT;
	}
	if (received >= sizeof("PSCINEG?\n") - 1 &&
	    !memcmp(buf, "PSCINEG?\n", sizeof("PSCINEG?\n") - 1)) {
		return UD_REBOOT_PSCI_NEGATIVE;
	}
	if (received >= sizeof("IMEM?\n") - 1 &&
	    !memcmp(buf, "IMEM?\n", sizeof("IMEM?\n") - 1)) {
		return UD_REBOOT_IMEM_REPORT;
	}
	if (received >= sizeof("SPMI?\n") - 1 &&
	    !memcmp(buf, "SPMI?\n", sizeof("SPMI?\n") - 1)) {
		return UD_REBOOT_SPMI_REPORT;
	}
	if (received >= sizeof("PON?\n") - 1 &&
	    !memcmp(buf, "PON?\n", sizeof("PON?\n") - 1)) {
		return UD_REBOOT_PON_REPORT;
	}
	if (received >= sizeof("TOUCH?\n") - 1 &&
	    !memcmp(buf, "TOUCH?\n", sizeof("TOUCH?\n") - 1)) {
		return UD_REBOOT_TOUCH_PROBE;
	}
	if (received >= sizeof("TOUCHINIT\n") - 1 &&
	    !memcmp(buf, "TOUCHINIT\n", sizeof("TOUCHINIT\n") - 1)) {
		return UD_REBOOT_TOUCH_INIT;
	}
	if (received >= sizeof("TOUCHCLK\n") - 1 &&
	    !memcmp(buf, "TOUCHCLK\n", sizeof("TOUCHCLK\n") - 1)) {
		return UD_REBOOT_TOUCH_CLOCK;
	}
	if (received >= sizeof("TOUCHSE\n") - 1 &&
	    !memcmp(buf, "TOUCHSE\n", sizeof("TOUCHSE\n") - 1)) {
		return UD_REBOOT_TOUCH_SE;
	}
	if (received >= sizeof("TOUCHPING\n") - 1 &&
	    !memcmp(buf, "TOUCHPING\n", sizeof("TOUCHPING\n") - 1)) {
		return UD_REBOOT_TOUCH_PING;
	}
	if (received >= sizeof("PWR?\n") - 1 &&
	    !memcmp(buf, "PWR?\n", sizeof("PWR?\n") - 1)) {
		return UD_REBOOT_PWR_REPORT;
	}
	if (received >= sizeof("VRMON ldoc1\n") - 1 &&
	    !memcmp(buf, "VRMON ldoc1\n", sizeof("VRMON ldoc1\n") - 1)) {
		return UD_REBOOT_VRMON_LDOC1;
	}
	if (received >= sizeof("VRMON\n") - 1 &&
	    !memcmp(buf, "VRMON\n", sizeof("VRMON\n") - 1)) {
		return UD_REBOOT_VRMON_LDOA13;
	}
	if (received >= sizeof("VRMON") - 1 &&
	    !memcmp(buf, "VRMON", sizeof("VRMON") - 1)) {
		return UD_REBOOT_VRMON_OTHER;
	}
	if (received >= sizeof("TCS?\n") - 1 &&
	    !memcmp(buf, "TCS?\n", sizeof("TCS?\n") - 1)) {
		return UD_REBOOT_TCS_REPORT;
	}
	if (received >= sizeof("RPMH?\n") - 1 &&
	    !memcmp(buf, "RPMH?\n", sizeof("RPMH?\n") - 1)) {
		return UD_REBOOT_RPMH_REPORT;
	}
	if (received >= sizeof("PROC?\n") - 1 && !memcmp(buf, "PROC?\n", sizeof("PROC?\n") - 1)) {
		return UD_REBOOT_PROC_REPORT;
	}
	if (received >= sizeof("PID1?\n") - 1 && !memcmp(buf, "PID1?\n", sizeof("PID1?\n") - 1)) {
		return UD_REBOOT_PID1_REPORT;
	}
	if (received >= sizeof("DRV?\n") - 1 &&
	    !memcmp(buf, "DRV?\n", sizeof("DRV?\n") - 1)) {
		return UD_REBOOT_DRV_REPORT;
	}
	if (received >= sizeof("QSEELOG?\n") - 1 &&
	    !memcmp(buf, "QSEELOG?\n", sizeof("QSEELOG?\n") - 1)) {
		return UD_REBOOT_QSEELOG_REPORT;
	}
	if (received >= sizeof("FWLOG? all\n") - 1 &&
	    !memcmp(buf, "FWLOG? all\n", sizeof("FWLOG? all\n") - 1)) {
		return UD_REBOOT_FWLOG_ALL;
	}
	if (received >= sizeof("FWLOG?\n") - 1 &&
	    !memcmp(buf, "FWLOG?\n", sizeof("FWLOG?\n") - 1)) {
		return UD_REBOOT_FWLOG_REPORT;
	}
	{
		/* The INST family carries arguments, so it has its own scanner.
		 * It has to run in this branch too: kebabkdp=1 only selects the
		 * matching style, and the installer must not depend on it. */
		uint32_t inst = ud_inst_command(buf, received);
		if (inst != 0) return inst;
	}
	{
		uint32_t vol = ud_vol_command(buf, received);
		if (vol != 0) return vol;
	}
	return 0;
}
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
	} else if (command == UD_REBOOT_BTBOARD_REPORT) {
		kebab_board_report();
	} else if (command == UD_REBOOT_BTPWR_REPORT) {
		kebab_bluetooth_power_report();
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
 kdp=false;
 for(auto row:std::initializer_list<std::pair<const char *,unsigned>>{
  {"BTBOARD?\n",UD_REBOOT_BTBOARD_REPORT},{"BTPWR?\n",UD_REBOOT_BTPWR_REPORT},{"PWR?\n",UD_REBOOT_PWR_REPORT},
  {"BTUART\n",UD_REBOOT_BTUART_LOOPBACK},{"BTCLK\n",UD_REBOOT_BTCLK_PREPARE},
  {"BTHW?\n",UD_REBOOT_BTHW_REPORT},{"BTFW?\n",UD_REBOOT_BTFW_REPORT}}){
  std::string bytes=row.first;assert(ud_reboot_command(bytes.data(),bytes.size())==row.second);
  for(unsigned n=0;n<bytes.size();++n)assert(!ud_reboot_command(bytes.data(),n));
  bytes="prior typing"+bytes;assert(ud_reboot_command(bytes.data(),bytes.size())==row.second);
 }
 kdp=true;assert(!ud_reboot_command("BTPWR?\n",7));assert(ud_reboot_command("PWR?\n",5)==UD_REBOOT_PWR_REPORT);assert(!ud_reboot_command("\xfa" "BTPWR?\n",8));kdp=false;
 for(unsigned command=4;command<=UD_REBOOT_BTBOARD_REPORT;++command){
  if(command==UD_REBOOT_FWLOG_REPORT||command==UD_REBOOT_FWLOG_ALL)continue;
  bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(command,true);assert(!bt&&!hw&&!clk&&!uart&&!pwr&&!board&&!calls);
 }
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTFW_REPORT,false);assert(bt==1&&!hw&&!clk&&!calls);
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTHW_REPORT,false);assert(hw==1&&!bt&&!clk&&!calls);
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTCLK_PREPARE,false);assert(clk==1&&!bt&&!hw&&!calls);
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTUART_LOOPBACK,false);assert(uart==1&&!bt&&!hw&&!clk&&!calls);
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTPWR_REPORT,false);assert(pwr==1&&!bt&&!hw&&!clk&&!uart&&!calls);
 bt=hw=clk=uart=pwr=board=calls=0;ud_run_reboot_command(UD_REBOOT_BTBOARD_REPORT,false);assert(board==1&&!bt&&!hw&&!clk&&!uart&&!pwr&&!calls);
 printf("PASS actual complete USB dispatcher: BTFW/BTHW/BTCLK/BTUART/BTPWR/BTBOARD normal-only, panic rejects all unsafe diagnostics\n");
}
