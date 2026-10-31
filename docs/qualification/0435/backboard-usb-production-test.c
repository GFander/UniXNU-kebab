#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static bool kdp;
static bool kebab_usb_kdp_enabled(void){return kdp;}
static uint32_t ud_inst_command(const char *p,uint32_t n){(void)p;(void)n;return 0;}
static uint32_t ud_vol_command(const char *p,uint32_t n){(void)p;(void)n;return 0;}
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
#define UD_REBOOT_BBOARD_REPORT 40u
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
		if (ud_command_seen(buf, received, "BBOARD?\n", sizeof("BBOARD?\n") - 1)) {
			return UD_REBOOT_BBOARD_REPORT;
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
	if (received >= sizeof("BBOARD?\n") - 1 && !memcmp(buf, "BBOARD?\n", sizeof("BBOARD?\n") - 1)) {
		return UD_REBOOT_BBOARD_REPORT;
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

int main(void){
{const char command[]="BBOARD?\n";const char binary[]="\xfa" "BBOARD?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?40:40));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:40));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="PROC?\n";const char binary[]="\xfa" "PROC?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?31:31));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:31));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="PID1?\n";const char binary[]="\xfa" "PID1?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?32:32));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:32));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="DRV?\n";const char binary[]="\xfa" "DRV?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?30:30));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:30));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="FASTBOOT\n";const char binary[]="\xfa" "FASTBOOT\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?2:2));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:2));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="FASTBOOT2\n";const char binary[]="\xfa" "FASTBOOT2\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?3:3));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:3));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="FWLOG?\n";const char binary[]="\xfa" "FWLOG?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?16:16));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:16));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="BTFW?\n";const char binary[]="\xfa" "BTFW?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?0:34));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:34));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="BTBOARD?\n";const char binary[]="\xfa" "BTBOARD?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?0:39));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:39));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
{const char command[]="BTPWR?\n";const char binary[]="\xfa" "BTPWR?\n";
 for(int mode=0;mode<2;mode++){kdp=mode;assert(ud_reboot_command(command,sizeof(command)-1)==(mode?0:38));assert(ud_reboot_command(binary,sizeof(binary)-1)==(mode?0:38));for(unsigned n=0;n<sizeof(command)-1;n++)assert(!ud_reboot_command(command,n));}}
puts("PASS actual console parser: fixedBBOARD and existing commands, short packets, normal scanner/KDP binary exclusion");}
