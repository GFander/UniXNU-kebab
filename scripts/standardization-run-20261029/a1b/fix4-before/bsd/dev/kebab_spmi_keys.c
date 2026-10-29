/* PM8150 key register client; no arbiter mappings or MMIO. */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include <pexpert/pexpert.h>
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
#include "kebab_pmic_regs.h"

/* ------------------------------------------------------- key registers */

/*
 * One look at the four PM8150 key bytes for bsd/dev/kebab_keys.c.  All four
 * reads must complete; a partial sample is not a key state.  raw[] is filled
 * only on success, and the caller treats a failure as "no observation".
 *
 * This is a read-only path: the only write is the observer command that asks
 * the arbiter for a transfer.  No PMIC register is changed.
 */
static int
kebab_spmi_keys_sample_unserialized(uint8_t raw[4])
{
	static const uint32_t addr[4] = {
		KEBAB_SPMI_KEYS_PON_RT,
		KEBAB_SPMI_KEYS_GPIO6_RT,
		KEBAB_SPMI_KEYS_GPIO7_RT,
		KEBAB_SPMI_KEYS_GPIO6_TYPE,
	};
	uint8_t value[4] = { 0, 0, 0, 0 };
	uint32_t i;

	if (raw == NULL) {
		return -1;
	}
	raw[0] = 0;
	raw[1] = 0;
	raw[2] = 0;
	raw[3] = 0;
	if (!kebab_spmi_bus_ready() || !kebab_spmi_version_known(kebab_spmi_bus_version()) ||
	    kebab_spmi_bus_version() < 0x50000000u) {
		return -1;
	}
	for (i = 0; i < 4; i++) {
		uint32_t ppid = (KEBAB_SPMI_PON_SID << 8) | (addr[i] >> 8);
		uint32_t apid;

		if (kebab_spmi_reader(ppid, &apid) != 0) {
			if (kebab_spmi_bus_verbose()) {
				printf("KEBAB SPMI: keys: no APID maps PPID 0x%03x (register 0x%04x)\n",
				    ppid, addr[i]);
			}
			return -1;
		}
		if (kebab_spmi_obs_read_ex(apid, KEBAB_SPMI_PON_SID, addr[i],
		    kebab_spmi_bus_next_attempt(), kebab_spmi_bus_verbose(), true, &value[i]) != 0) {
			kebab_spmi_reader_forget(ppid);
			return -1;
		}
	}
	memcpy(raw, value, sizeof(value));
	return 0;
}

/*
 * The pin setup of the two volume-key inputs, read once by
 * bsd/dev/kebab_keys.c before it starts polling.  Each array is
 * { mode, pull, enable } from the vendor pinctrl registers 0x40, 0x42 and 0x46
 * inside the GPIO's 0x100-byte page.
 *
 * The vendor device tree asks for an enabled input with a pull-up
 * (input-enable, bias-pull-up, power-source 1).  This kernel never writes the
 * PMIC, so the state has to come from the boot firmware; the keys driver
 * reports these bytes and withholds a key whose pin is not configured that
 * way, instead of turning a disabled input into a permanent press.
 */
static int
kebab_spmi_keys_config_unserialized(uint8_t gpio6[3], uint8_t gpio7[3])
{
	static const uint32_t addr[6] = {
		KEBAB_SPMI_KEYS_GPIO6_MODE,
		KEBAB_SPMI_KEYS_GPIO6_PULL,
		KEBAB_SPMI_KEYS_GPIO6_EN,
		KEBAB_SPMI_KEYS_GPIO7_MODE,
		KEBAB_SPMI_KEYS_GPIO7_PULL,
		KEBAB_SPMI_KEYS_GPIO7_EN,
	};
	uint8_t value[6] = { 0, 0, 0, 0, 0, 0 };
	uint32_t i;

	if (gpio6 == NULL || gpio7 == NULL) {
		return -1;
	}
	if (!kebab_spmi_bus_ready() || !kebab_spmi_version_known(kebab_spmi_bus_version()) ||
	    kebab_spmi_bus_version() < 0x50000000u) {
		return -1;
	}
	for (i = 0; i < 6; i++) {
		uint32_t ppid = (KEBAB_SPMI_PON_SID << 8) | (addr[i] >> 8);
		uint32_t apid;

		if (kebab_spmi_reader(ppid, &apid) != 0) {
			return -1;
		}
		if (kebab_spmi_obs_read_ex(apid, KEBAB_SPMI_PON_SID, addr[i],
		    kebab_spmi_bus_next_attempt(), kebab_spmi_bus_verbose(), true, &value[i]) != 0) {
			kebab_spmi_reader_forget(ppid);
			return -1;
		}
	}
	memcpy(gpio6, value, 3);
	memcpy(gpio7, value + 3, 3);
	return 0;
}

int
kebab_spmi_keys_sample(uint8_t raw[4])
{
	kebab_spmi_bus_lock();
	int result = kebab_spmi_keys_sample_unserialized(raw);
	kebab_spmi_bus_unlock();
	return result;
}

int
kebab_spmi_keys_config(uint8_t gpio6[3], uint8_t gpio7[3])
{
	kebab_spmi_bus_lock();
	int result = kebab_spmi_keys_config_unserialized(gpio6, gpio7);
	kebab_spmi_bus_unlock();
	return result;
}
#endif /* KEBAB */
