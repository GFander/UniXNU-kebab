/* PM8150 PON policy and restart reason; panic core stays lock-free. */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include <pexpert/pexpert.h>
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
#include "kebab_pmic_regs.h"

static struct { bool write_gate, write_gate_parsed, verbose; } pon;

/* ---------------------------------------------------------------- helpers */


/*
 * One gate for every PMIC write in this file.  Parsed once, in kebab_spmi_init()
 * at boot, so a command running from the panic loop only reads a boolean.
 * Default off: an unmodified boot never writes a PMIC register.
 */
bool
kebab_spmi_pon_writes_enabled(void)
{
	uint32_t enabled = 0, verbose = 0;

	if (!pon.write_gate_parsed) {
		pon.write_gate_parsed = true;
		pon.write_gate = PE_parse_boot_argn("kebab_pon_write", &enabled,
		    sizeof(enabled)) && enabled != 0;
		pon.verbose = PE_parse_boot_argn("kebab_pon_verbose", &verbose,
		    sizeof(verbose)) && verbose != 0;
		kebab_spmi_bus_set_verbose(pon.verbose);
		printf("KEBAB SPMI: kebab_pon_write=%u: PMIC writes %s",
		    enabled, pon.write_gate ? "ENABLED" : "disabled (default)");
		if (pon.verbose) {
			printf(", kebab_pon_verbose=1: raw register traffic shown");
		}
		printf("\n");
	}
	return pon.write_gate;
}

/*
 * The APID the local EE may write the PON block through.  Several APIDs can
 * map PPID 0x008 (one per execution environment); only the one whose
 * ownership register says write_ee == this EE is usable, which is the same
 * test pmic_arb_offset_v5() applies to a read/write channel.  Refusing here
 * keeps every later write from being a permission experiment.
 */
static int
kebab_spmi_pon_apid(uint32_t *apid_out)
{
	struct kebab_spmi_match matches[8];
	uint32_t count, i;

	count = kebab_spmi_scan(KEBAB_SPMI_PON_PPID, matches, 8);
	if (count == 0) {
		printf("KEBAB SPMI: no APID maps PPID 0x%03x; no PON write is possible\n",
		    KEBAB_SPMI_PON_PPID);
		return -1;
	}
	if (count > 8) {
		count = 8;
	}
	for (i = 0; i < count; i++) {
		if (matches[i].write_ee == KEBAB_SPMI_EE) {
			*apid_out = matches[i].apid;
			if (pon.verbose) {
				printf("KEBAB SPMI: PON write APID 0x%03x, write_ee 0x%x (this EE); chnls slice 0x%08x\n",
				    matches[i].apid, matches[i].write_ee,
				    (uint32_t)kebab_spmi_bus_channel_physical(matches[i].apid));
			}
			return 0;
		}
	}
	printf("KEBAB SPMI: PPID 0x%03x has %u APID(s), none with write_ee %u; refusing every PON write\n",
	    KEBAB_SPMI_PON_PPID, count, KEBAB_SPMI_EE);
	return -1;
}

/*
 * The observer APID for one PPID, remembered across polls.  The keys driver
 * reads three peripherals about sixty times a second; walking the 512-entry
 * mapping table for every sample would be most of the work.  The mapping
 * register is still re-read by kebab_spmi_obs_read_ex() for every transfer, so
 * a stale cached APID cannot silently become a read of another peripheral: the
 * transfer is refused and the caller drops the entry.
 *
 * A read does not need channel ownership; the observer path is available for
 * every mapped PPID.  When several APIDs map one PPID, the one owned by this
 * EE is preferred only so the answer matches what a write would use.
 */


/* ---------------------------------------------------------- observer reads */

/*
 * Read one PMIC register through the observer channel.  The command register
 * is written, status is polled with a bounded counter, and the data register
 * is read only after DONE without FAILURE/DENIED/DROPPED.
 *
 * The APID is re-checked against the PPID derived from sid/addr, so a stale
 * or wrong match cannot silently become a read of a different peripheral.
 * The success line is raw register traffic and is printed only when the
 * caller asks for it; failures print unless the caller is a poll that has its
 * own rate-limited error line (the keys driver).
 */


/* The PON? report always shows the raw reads; the write path does not. */
static int
kebab_spmi_obs_read(uint32_t apid, uint32_t sid, uint32_t addr,
    uint32_t attempt, uint8_t *value)
{
	return kebab_spmi_obs_read_ex(apid, sid, addr, attempt, true, false,
	    value);
}

/* -------------------------------------------------- read/write PON registers */

static int
kebab_spmi_pon_read(uint32_t apid, uint32_t addr, uint8_t *value)
{
	return kebab_spmi_obs_read_ex(apid, KEBAB_SPMI_PON_SID, addr,
	    kebab_spmi_bus_next_attempt(), pon.verbose, false, value);
}

/*
 * One PMIC byte write through the read/write channel slice mapped for apid.
 * The mapping and ownership registers are re-read here, not trusted from the
 * caller: a write is only issued when the APID still maps the PPID derived
 * from sid/addr and its write_ee is this EE.
 *
 * The caller must have read the old register value through the observer
 * first; this primitive does not do the read-modify-write by itself.
 */


/*
 * Gen2 restart reason: qpnp_pon_set_restart_reason() does a masked write of
 * GENMASK(7,1) with (reason << 1); reason 0x02 is PON_RESTART_REASON_BOOTLOADER,
 * so the target field value is 0x04 and bit 0 is outside the mask and must
 * survive unchanged.
 *
 * The old value is read first and printed; the write is issued only after the
 * APID ownership check inside kebab_spmi_rw_write(); the register is then read
 * back (bounded retries) and both the target bits and the untouched bits are
 * checked before this returns success.
 */
int
kebab_spmi_pon_reason_write(void)
{
	uint32_t apid, i, old, expect, readback = 0;
	uint8_t value = 0;
	bool readback_ok = false;
	int rc;

	if (!kebab_spmi_pon_writes_enabled()) {
		printf("KEBAB PON: kebab_pon_write is off; refusing to write SOFT_RB_SPARE\n");
		return -1;
	}
	if (kebab_spmi_pon_apid(&apid) != 0) {
		return -1;
	}
	if (kebab_spmi_pon_read(apid, KEBAB_SPMI_PON_SPARE, &value) != 0) {
		printf("KEBAB PON: could not read SOFT_RB_SPARE 0x%04x; not writing it\n",
		    KEBAB_SPMI_PON_SPARE);
		return -1;
	}
	old = value;
	expect = (old & ~KEBAB_SPMI_PON_REASON_MASK) |
	    KEBAB_SPMI_PON_REASON_FIELD;
	printf("KEBAB PON: restart reason: SOFT_RB_SPARE 0x%02x -> 0x%02x (mask 0x%02x, PON_RESTART_REASON_BOOTLOADER 0x%02x << 1 = 0x%02x)\n",
	    old, expect, KEBAB_SPMI_PON_REASON_MASK,
	    KEBAB_SPMI_PON_REASON_BOOTLOADER, KEBAB_SPMI_PON_REASON_FIELD);
	if (expect == old) {
		printf("KEBAB PON: the field already held the bootloader value; writing it again to prove the path\n");
	}
	rc = kebab_spmi_rw_write(apid, KEBAB_SPMI_PON_SID, KEBAB_SPMI_PON_SPARE,
	    (uint8_t)expect);
	if (rc != 0) {
		printf("KEBAB PON: SOFT_RB_SPARE write failed; nothing verified\n");
		return -1;
	}

	for (i = 0; i < 20u; i++) {
		if (kebab_spmi_pon_read(apid, KEBAB_SPMI_PON_SPARE, &value) == 0) {
			readback = value;
			readback_ok = true;
			if (value == expect) {
				break;
			}
		}
		delay(1000);
	}
	if (!readback_ok) {
		printf("KEBAB PON: FAIL: the write completed but SOFT_RB_SPARE could not be read back\n");
		return -1;
	}
	if ((readback & KEBAB_SPMI_PON_REASON_MASK) != KEBAB_SPMI_PON_REASON_FIELD) {
		printf("KEBAB PON: FAIL: read back 0x%02x, reason field is 0x%02x, not 0x%02x\n",
		    readback, readback & KEBAB_SPMI_PON_REASON_MASK,
		    KEBAB_SPMI_PON_REASON_FIELD);
		return -1;
	}
	if ((readback & ~KEBAB_SPMI_PON_REASON_MASK) !=
	    (old & ~KEBAB_SPMI_PON_REASON_MASK)) {
		printf("KEBAB PON: FAIL: bit 0 changed 0x%x -> 0x%x although it is outside mask 0x%02x\n",
		    old & 1u, readback & 1u, KEBAB_SPMI_PON_REASON_MASK);
		return -1;
	}
	printf("KEBAB PON: restart reason verified: read back 0x%02x, field 0x%02x, bit 0 unchanged 0x%x\n",
	    readback, readback & KEBAB_SPMI_PON_REASON_MASK, readback & 1u);
	return 0;
}

/*
 * SPMI-3: read the PON subtype and SOFT_RB_SPARE through the observer, twice
 * each, and require both reads of a register to agree.  This is still a
 * read-only gate: the observer command register is written to issue a read,
 * no PMIC register is written.
 */
static void
kebab_spmi_pon_report_unserialized(void)
{
	struct kebab_spmi_match matches[8];
	uint32_t count, i, chosen = 0, version;
	bool have_owned = false;
	uint8_t subtype[2] = { 0, 0 }, spare[2] = { 0, 0 };
	int rc1, rc2;
	bool subtype_stable, spare_stable, subtype_known;

	if (!kebab_spmi_bus_ready()) {
		printf("KEBAB SPMI: SPMI-3 FAIL: the arbiter registers are not mapped\n");
		return;
	}
	version = kebab_spmi_bus_read_version();
	if (!kebab_spmi_version_known(version)) {
		printf("KEBAB SPMI: SPMI-3 FAIL: arbiter version 0x%08x is not recognised; no read attempted\n",
		    version);
		return;
	}
	if (version < 0x50000000u) {
		printf("KEBAB SPMI: SPMI-3 FAIL: arbiter is %s, not v5; the v5 observer arithmetic is not used\n",
		    kebab_spmi_family(version));
		return;
	}

	printf("KEBAB SPMI: === SPMI-3: PON registers through the observer ===\n");
	count = kebab_spmi_scan(KEBAB_SPMI_PON_PPID, matches, 8);
	if (count == 0) {
		printf("KEBAB SPMI: SPMI-3 FAIL: PPID 0x%03x has no APID\n",
		    KEBAB_SPMI_PON_PPID);
		return;
	}
	for (i = 0; i < count && i < 8; i++) {
		kebab_spmi_match_line(&matches[i]);
		if (!have_owned && matches[i].write_ee == KEBAB_SPMI_EE) {
			chosen = matches[i].apid;
			have_owned = true;
		}
	}
	if (!have_owned) {
		/*
		 * pmic_arb_read_apid_map_v5() falls back to the first mapping
		 * when no APID is owned by this EE, and the observer path does
		 * not check write_ee.  Say so instead of failing the read.
		 */
		chosen = matches[0].apid;
		printf("KEBAB SPMI: no APID for PPID 0x%03x is owned by EE %u; using the first mapping (observer reads are not ownership-checked)\n",
		    KEBAB_SPMI_PON_PPID, KEBAB_SPMI_EE);
	}
	printf("KEBAB SPMI: using APID 0x%03x for PPID 0x%03x, SID %u, ee %u\n",
	    chosen, KEBAB_SPMI_PON_PPID, KEBAB_SPMI_PON_SID, KEBAB_SPMI_EE);

	rc1 = kebab_spmi_obs_read(chosen, KEBAB_SPMI_PON_SID,
	    KEBAB_SPMI_PON_SUBTYPE, 1, &subtype[0]);
	rc2 = kebab_spmi_obs_read(chosen, KEBAB_SPMI_PON_SID,
	    KEBAB_SPMI_PON_SUBTYPE, 2, &subtype[1]);
	subtype_stable = rc1 == 0 && rc2 == 0 && subtype[0] == subtype[1];
	printf("KEBAB SPMI: PON subtype 0x%04x reads 0x%02x / 0x%02x -> %s\n",
	    KEBAB_SPMI_PON_SUBTYPE, subtype[0], subtype[1],
	    subtype_stable ? "STABLE" : "NOT STABLE");

	rc1 = kebab_spmi_obs_read(chosen, KEBAB_SPMI_PON_SID,
	    KEBAB_SPMI_PON_SPARE, 1, &spare[0]);
	rc2 = kebab_spmi_obs_read(chosen, KEBAB_SPMI_PON_SID,
	    KEBAB_SPMI_PON_SPARE, 2, &spare[1]);
	spare_stable = rc1 == 0 && rc2 == 0 && spare[0] == spare[1];
	printf("KEBAB SPMI: SOFT_RB_SPARE 0x%04x reads 0x%02x / 0x%02x -> %s\n",
	    KEBAB_SPMI_PON_SPARE, spare[0], spare[1],
	    spare_stable ? "STABLE" : "NOT STABLE");

	subtype_known = subtype_stable && subtype[0] >= 0x01u && subtype[0] <= 0x05u;
	if (subtype_known) {
		if (subtype[0] == 0x04u || subtype[0] == 0x05u) {
			printf("KEBAB SPMI: PON generation: Gen2 (subtype 0x%02x); the Gen2 SOFT_RB_SPARE layout would apply\n",
			    subtype[0]);
		} else {
			printf("KEBAB SPMI: PON generation: Gen1/1REG (subtype 0x%02x); the Gen2 SOFT_RB_SPARE layout does not apply\n",
			    subtype[0]);
		}
	} else {
		printf("KEBAB SPMI: PON generation: UNKNOWN (subtype 0x%02x is not a documented PON subtype, or the reads did not repeat)\n",
		    subtype[0]);
	}
	if (subtype_stable && spare_stable && subtype_known) {
		printf("KEBAB SPMI: SPMI-3 PASS\n");
	} else {
		printf("KEBAB SPMI: SPMI-3 FAIL: subtype %s, SOFT_RB_SPARE %s, subtype %s\n",
		    subtype_stable ? "stable" : "unstable",
		    spare_stable ? "stable" : "unstable",
		    subtype_known ? "recognised" : "not recognised");
	}
}

void
kebab_spmi_pon_report(void)
{
	kebab_spmi_bus_lock();
	kebab_spmi_pon_report_unserialized();
	kebab_spmi_bus_unlock();
}

int
kebab_spmi_pon_reason_write_normal(void)
{
	kebab_spmi_bus_lock();
	int result = kebab_spmi_pon_reason_write();
	kebab_spmi_bus_unlock();
	return result;
}

/* Default-off failure injection: PANIC! invokes this on the normal console
 * thread. Holding the ordinary bus mutex across the panic proves that the
 * independent panic FASTBOOT path cannot wait on the interrupted owner. */
void
kebab_spmi_test_panic_lock(void)
{
	uint32_t enabled = 0;
	if (PE_parse_boot_argn("kebab_spmi_lockpanic", &enabled, sizeof(enabled)) && enabled) {
		kebab_spmi_bus_lock();
		printf("KEBAB SPMI TEST: normal bus mutex held for intentional panic\n");
		panic("kebab usb console test (normal SPMI lock held)");
	}
}

/* Policy remains in PON; the bus maps only the selected owned channel. */
void
kebab_spmi_pon_prepare(void)
{
	struct kebab_spmi_bus_info info;
	kebab_spmi_bus_get_info(&info);
	if (!kebab_spmi_pon_writes_enabled()) {
		printf("KEBAB SPMI: chnls 0x%08x not mapped; the write path is off\n",
		    (uint32_t)info.resources[6]);
		return;
	}
	if (kebab_spmi_version_known(info.version) && info.version >= 0x50000000u) {
		uint32_t apid;
		if (kebab_spmi_pon_apid(&apid) == 0) {
			kebab_spmi_bus_prepare_write(apid);
		}
	} else {
		printf("KEBAB SPMI: arbiter is not v5; the write path is not implemented for it\n");
	}
}
#endif /* KEBAB */
