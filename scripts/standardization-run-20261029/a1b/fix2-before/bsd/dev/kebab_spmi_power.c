/* Read-only PMIC rail identity/state client of the SPMI arbiter. */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include <pexpert/pexpert.h>
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
#include "kebab_pmic_regs.h"

/*
 * The S3908 node names two rails that RPMh owns; this file cannot command
 * RPMh, but it can read the PMIC peripherals through the observer channel,
 * which needs no channel ownership and issues no peripheral write.
 *
 * The SPMI bus on this board carries PM8150 on SID 0/1, PM8150B on 2/3,
 * PM8150L on 4/5 and PMK8002 on 6/7.  The mapping survey below showed which
 * USID actually exposes each LDO page: 0x4c00 is mapped on SID 1 (not 0) and
 * 0x4000 on SID 5 (not 4), with SID 2 showing its own 0x4000 (PM8150B).  The
 * table uses those measured USIDs; the type/subtype answer is what finally
 * decides whether the peripheral is the expected LDO.  Lead with identity, and
 * only read the control registers after it is stable and not empty.
 */
struct kebab_spmi_power {
	const char *name;
	uint32_t    sid;
	uint32_t    base;
};

static const struct kebab_spmi_power kebab_spmi_power_targets[] = {
	{ "pm8150_l13", 1, 0x4c00u },
	{ "pm8150a_l1", 5, 0x4000u },
};

static int
kebab_spmi_power_read(uint32_t apid, uint32_t sid, uint32_t addr,
    const char *what, uint8_t *out)
{
	uint8_t a = 0, b = 0;

	if (kebab_spmi_obs_read_ex(apid, sid, addr, 1, false, false, &a) != 0 ||
	    kebab_spmi_obs_read_ex(apid, sid, addr, 2, false, false, &b) != 0) {
		printf("KEBAB SPMI: PWR: %s 0x%04x read failed\n", what, addr);
		return -1;
	}
	if (a != b) {
		printf("KEBAB SPMI: PWR: %s 0x%04x unstable 0x%02x/0x%02x\n",
		    what, addr, a, b);
		return -1;
	}
	*out = a;
	return 0;
}

static void
kebab_spmi_power_one(const struct kebab_spmi_power *t)
{
	struct kebab_spmi_match matches[8];
	uint32_t ppid = (t->sid << 8) | (t->base >> 8);
	uint32_t count, i, apid = 0;
	uint8_t type = 0, subtype = 0, enable = 0, range = 0, set = 0;

	printf("KEBAB SPMI: PWR: %s: SID %u base 0x%04x PPID 0x%03x\n",
	    t->name, t->sid, t->base, ppid);
	count = kebab_spmi_scan(ppid, matches, 8);
	if (count == 0) {
		printf("KEBAB SPMI: PWR: %s: no APID maps PPID 0x%03x; the SID candidate is wrong or the peripheral is absent\n",
		    t->name, ppid);
		return;
	}
	if (count > 8) {
		count = 8;
	}
	for (i = 0; i < count; i++) {
		printf("KEBAB SPMI: PWR: %s: APID 0x%03x write_ee 0x%x irq_owner %u\n",
		    t->name, matches[i].apid, matches[i].write_ee,
		    matches[i].irq_owner);
	}
	apid = matches[0].apid;
	if (kebab_spmi_power_read(apid, t->sid, t->base + 0x04u, "type", &type) != 0 ||
	    kebab_spmi_power_read(apid, t->sid, t->base + 0x05u, "subtype",
	    &subtype) != 0) {
		printf("KEBAB SPMI: PWR: %s: identity unreadable; control registers not read\n",
		    t->name);
		return;
	}
	printf("KEBAB SPMI: PWR: %s: type 0x%02x subtype 0x%02x\n",
	    t->name, type, subtype);
	if (type == 0x00u || type == 0xffu) {
		printf("KEBAB SPMI: PWR: %s: identity is empty (0x%02x); stopping\n",
		    t->name, type);
		return;
	}
	if (kebab_spmi_power_read(apid, t->sid, t->base + 0x46u, "enable",
	    &enable) != 0 ||
	    kebab_spmi_power_read(apid, t->sid, t->base + 0x40u, "range",
	    &range) != 0 ||
	    kebab_spmi_power_read(apid, t->sid, t->base + 0x41u, "set", &set) != 0) {
		printf("KEBAB SPMI: PWR: %s: control registers unreadable\n", t->name);
		return;
	}
	printf("KEBAB SPMI: PWR: %s: enable 0x%02x (bit7 %u), range 0x%02x, set 0x%02x\n",
	    t->name, enable, (enable >> 7) & 1u, range, set);
}

/*
 * The two candidates above can be "not mapped" for two very different
 * reasons: the SID is wrong, or the AP's SPMI channel genuinely has no
 * peripheral there.  This survey answers that without touching a peripheral:
 * it walks the same 512-entry APID table SPMI-2 uses, counts mappings per
 * SID, and looks for any PPID whose peripheral address falls in the two LDO
 * pages.
 */
static void
kebab_spmi_power_map_summary(void)
{
	uint32_t apid, i, ppid, sid;
	uint32_t per_sid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	uint32_t page_hits = 0;

	for (apid = 0; apid < KEBAB_SPMI_APID_MAX; apid++) {
		uint32_t map = kebab_spmi_bus_mapping(apid);
		if (map == 0) {
			continue;
		}
		ppid = (map >> 8) & 0xfffu;
		sid = ppid >> 8;
		if (sid < 8) {
			per_sid[sid]++;
		}
	}
	printf("KEBAB SPMI: PWR: mappings per SID:");
	for (i = 0; i < 8; i++) {
		printf(" %u:%u", i, per_sid[i]);
	}
	printf("\n");

	for (sid = 0; sid < 8; sid++) {
		static const uint32_t pages[] = { 0x40u, 0x4cu };
		for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
			ppid = (sid << 8) | pages[i];
			struct kebab_spmi_match hit[1];

			if (kebab_spmi_scan(ppid, hit, 1) > 0) {
				printf("KEBAB SPMI: PWR: mapping present for SID %u base 0x%04x (PPID 0x%03x)\n",
				    sid, pages[i] << 8, ppid);
				page_hits++;
			}
		}
	}
	if (page_hits == 0) {
		printf("KEBAB SPMI: PWR: no SID 0..7 maps an LDO page at 0x4000/0x4c00; the AP SPMI channel cannot reach these rails\n");
	}
}

/*
 * The enable byte PWR? prints, split out so the boot sequence can ask whether a
 * rail is already on before commanding it.  Same observer reads, same two
 * matching reads of the same register: this is the path the console has
 * already been measured through, not a second one.
 */
static const struct kebab_spmi_power *
kebab_spmi_rail_target(const char *peripheral)
{
	uint32_t i;

	for (i = 0; i < sizeof(kebab_spmi_power_targets) /
	    sizeof(kebab_spmi_power_targets[0]); i++) {
		if (strcmp(kebab_spmi_power_targets[i].name, peripheral) == 0) {
			return &kebab_spmi_power_targets[i];
		}
	}
	return NULL;
}

int
kebab_spmi_rail_enable_read_locked(const char *rail, const char *peripheral,
    uint8_t *out)
{
	const struct kebab_spmi_power *t = kebab_spmi_rail_target(peripheral);
	struct kebab_spmi_match matches[8];
	uint32_t ppid;

	if (t == NULL) {
		printf("KEBAB VRM: boot power: %s is not a peripheral this file knows\n",
		    rail);
		return -1;
	}
	ppid = (t->sid << 8) | (t->base >> 8);
	if (kebab_spmi_scan(ppid, matches, 8) == 0) {
		printf("KEBAB VRM: boot power: %s: no APID maps PPID 0x%03x\n",
		    rail, ppid);
		return -1;
	}
	return kebab_spmi_power_read(matches[0].apid, t->sid,
	    t->base + 0x46u, "enable", out);
}

static void
kebab_spmi_power_report_unserialized(void)
{
	uint32_t i;

	if (!kebab_spmi_bus_ready()) {
		printf("KEBAB SPMI: PWR: SPMI is not mapped\n");
		return;
	}
	printf("KEBAB SPMI: === PWR: touch rails, observer reads only ===\n");
	for (i = 0; i < sizeof(kebab_spmi_power_targets) /
	    sizeof(kebab_spmi_power_targets[0]); i++) {
		kebab_spmi_power_one(&kebab_spmi_power_targets[i]);
	}
	kebab_spmi_power_map_summary();
	printf("KEBAB SPMI: PWR: done; no peripheral write was issued\n");
}

void
kebab_spmi_power_report(void)
{
	kebab_spmi_bus_lock();
	kebab_spmi_power_report_unserialized();
	kebab_spmi_bus_unlock();
}
#endif /* KEBAB */
