/*
 * SM8250 PMIC arbiter: resource mapping, APID/PPID lookup and bounded transfers.
 * Extracted unchanged from kebab_spmi.c in A1a. Boot preparation maps resources;
 * panic FASTBOOT uses only already mapped state, barriers and bounded polling.
 */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include "kebab_spmi_bus.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

struct kebab_spmi_bus_state kebab_spmi_bus;
uint32_t kebab_spmi_nonzero;

uint32_t
kebab_spmi_rd(vm_offset_t base, uint32_t offset)
{
	uint32_t value = *(volatile uint32_t *)(base + offset);

	/* Device memory: finish the read before its result is used. */
	__asm__ volatile ("dmb sy" ::: "memory");
	return value;
}

void
kebab_spmi_wr(vm_offset_t base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(base + offset) = value;
	/* The command must reach the arbiter before the status is polled. */
	__asm__ volatile ("dsb sy" ::: "memory");
}

const char *
kebab_spmi_family(uint32_t version)
{
	if (version >= 0x50000000u) {
		return "v5";
	}
	if (version >= 0x30000000u) {
		return "v3/v4";
	}
	if (version >= 0x20010000u) {
		return "v2";
	}
	return "v1";
}

bool
kebab_spmi_version_known(uint32_t version)
{
	if (version == 0u || version == 0xffffffffu) {
		return false;
	}
	return true;
}

uint32_t
kebab_spmi_scan(uint32_t ppid, struct kebab_spmi_match *matches,
    uint32_t max_matches)
{
	uint32_t entries, apid, found = 0;

	if ((KEBAB_SPMI_CORE_SIZE - KEBAB_SPMI_APID_MAP) / 4u < KEBAB_SPMI_APID_MAX) {
		entries = (KEBAB_SPMI_CORE_SIZE - KEBAB_SPMI_APID_MAP) / 4u;
	} else {
		entries = KEBAB_SPMI_APID_MAX;
	}
	kebab_spmi_nonzero = 0;

	for (apid = 0; apid < entries; apid++) {
		uint32_t map_offset = KEBAB_SPMI_APID_MAP + 4u * apid;
		uint32_t own_offset = KEBAB_SPMI_OWNERSHIP + 4u * apid;
		uint32_t map, own, this_ppid, write_ee, irq_owner, irq_ee;
		bool match;

		if (map_offset + 4u > KEBAB_SPMI_CORE_SIZE) {
			break;
		}
		map = kebab_spmi_rd(spmi.core, map_offset);
		if (map == 0) {
			continue;
		}
		if (own_offset + 4u > KEBAB_SPMI_CNFG_SIZE) {
			printf("KEBAB SPMI: APID 0x%03x ownership offset 0x%x is outside cnfg (0x%x); stopping\n",
			    apid, own_offset, KEBAB_SPMI_CNFG_SIZE);
			break;
		}
		kebab_spmi_nonzero++;
		own = kebab_spmi_rd(spmi.cnfg, own_offset);

		this_ppid = (map >> 8) & 0xfffu;
		irq_owner = (map >> 24) & 1u;
		write_ee = own & 0x7u;
		irq_ee = irq_owner ? write_ee : 0xffu;
		match = this_ppid == ppid;

		if (match) {
			if (found < max_matches) {
				matches[found].apid = apid;
				matches[found].ppid = this_ppid;
				matches[found].map_raw = map;
				matches[found].own_raw = own;
				matches[found].write_ee = write_ee;
				matches[found].irq_owner = irq_owner;
				matches[found].irq_ee = irq_ee;
			}
			found++;
		}
	}

	return found;
}

int
kebab_spmi_reader(uint32_t ppid, uint32_t *apid_out)
{
	struct kebab_spmi_match matches[4];
	struct kebab_spmi_reader *slot = NULL;
	uint32_t count, i, chosen = 0;
	bool owned = false;

	for (i = 0; i < KEBAB_SPMI_READERS; i++) {
		if (spmi.readers[i].valid && spmi.readers[i].ppid == ppid) {
			*apid_out = spmi.readers[i].apid;
			return 0;
		}
		if (slot == NULL && !spmi.readers[i].valid) {
			slot = &spmi.readers[i];
		}
	}
	if (slot == NULL) {
		/* Only three PPIDs are used; recycle the first slot if one leaks. */
		slot = &spmi.readers[0];
	}

	count = kebab_spmi_scan(ppid, matches, 4);
	if (count == 0) {
		return -1;
	}
	for (i = 0; i < count && i < 4; i++) {
		if (matches[i].write_ee == KEBAB_SPMI_EE) {
			chosen = matches[i].apid;
			owned = true;
			break;
		}
	}
	if (!owned) {
		chosen = matches[0].apid;
	}
	slot->ppid = ppid;
	slot->apid = chosen;
	slot->valid = true;
	printf("KEBAB SPMI: key source PPID 0x%03x -> observer APID 0x%03x%s\n",
	    ppid, chosen, owned ? " (owned by this EE)" : " (first mapping)");
	*apid_out = chosen;
	return 0;
}

void
kebab_spmi_reader_forget(uint32_t ppid)
{
	uint32_t i;

	for (i = 0; i < KEBAB_SPMI_READERS; i++) {
		if (spmi.readers[i].valid && spmi.readers[i].ppid == ppid) {
			spmi.readers[i].valid = false;
		}
	}
}

int
kebab_spmi_obs_read_ex(uint32_t apid, uint32_t sid, uint32_t addr,
    uint32_t attempt, bool verbose, bool quiet, uint8_t *value)
{
	uint32_t ppid = (sid << 8) | (addr >> 8);
	uint32_t map, map_ppid, offset, cmd, status = 0;
	uint32_t polls;

	if (apid >= KEBAB_SPMI_APID_MAX) {
		printf("KEBAB SPMI: APID 0x%x is out of range (max 0x%x); no read issued\n",
		    apid, KEBAB_SPMI_APID_MAX - 1u);
		return -1;
	}
	offset = KEBAB_SPMI_EE * 0x10000u + apid * 0x80u;
	if (offset + 0x20u > KEBAB_SPMI_OBSRV_SIZE) {
		printf("KEBAB SPMI: observer offset 0x%x is outside obsrvr (0x%x); no read issued\n",
		    offset, KEBAB_SPMI_OBSRV_SIZE);
		return -1;
	}
	map = kebab_spmi_rd(spmi.core, KEBAB_SPMI_APID_MAP + 4u * apid);
	map_ppid = (map >> 8) & 0xfffu;
	if (map == 0 || map_ppid != ppid) {
		printf("KEBAB SPMI: APID 0x%03x maps PPID 0x%03x, not PPID 0x%03x; refusing the read\n",
		    apid, map_ppid, ppid);
		return -1;
	}

	cmd = (KEBAB_SPMI_OP_READ << 27) | ((addr & 0xffu) << 4);
	kebab_spmi_wr(spmi.obsrvr, offset + KEBAB_SPMI_CMD, cmd);

	for (polls = 0; polls < KEBAB_SPMI_TIMEOUT_US; polls++) {
		status = kebab_spmi_rd(spmi.obsrvr, offset + KEBAB_SPMI_STATUS);
		if (status & KEBAB_SPMI_STATUS_DONE) {
			break;
		}
		delay(1);
	}
	if (!(status & KEBAB_SPMI_STATUS_DONE)) {
		if (!quiet) {
			printf("KEBAB SPMI: REG 0x%04x read %u TIMEOUT after %u us: cmd 0x%08x status 0x%08x\n",
			    addr, attempt, KEBAB_SPMI_TIMEOUT_US, cmd, status);
		}
		return -1;
	}
	if (status & (KEBAB_SPMI_STATUS_FAILURE | KEBAB_SPMI_STATUS_DENIED |
	    KEBAB_SPMI_STATUS_DROPPED)) {
		if (!quiet) {
			printf("KEBAB SPMI: REG 0x%04x read %u FAILED: cmd 0x%08x status 0x%08x%s%s%s\n",
			    addr, attempt, cmd, status,
			    (status & KEBAB_SPMI_STATUS_FAILURE) ? " FAILURE" : "",
			    (status & KEBAB_SPMI_STATUS_DENIED) ? " DENIED" : "",
			    (status & KEBAB_SPMI_STATUS_DROPPED) ? " DROPPED" : "");
		}
		return -1;
	}
	*value = (uint8_t)(kebab_spmi_rd(spmi.obsrvr, offset + KEBAB_SPMI_RDATA0) & 0xffu);
	if (verbose) {
		printf("KEBAB SPMI: REG 0x%04x read %u: APID 0x%03x obsrvr+0x%x cmd 0x%08x status 0x%08x -> 0x%02x\n",
		    addr, attempt, apid, offset, cmd, status, *value);
	}
	return 0;
}

int
kebab_spmi_rw_write(uint32_t apid, uint32_t sid, uint32_t addr, uint8_t value)
{
	uint32_t ppid = (sid << 8) | (addr >> 8);
	uint32_t map, map_ppid, own, write_ee, cmd, status = 0, polls;

	if (!spmi.chnl_mapped || spmi.chnl == 0 || spmi.chnl_apid != apid) {
		printf("KEBAB SPMI: read/write channel for APID 0x%03x is not the mapped slice (mapped APID 0x%03x); no write issued\n",
		    apid, spmi.chnl_apid);
		return -1;
	}
	map = kebab_spmi_rd(spmi.core, KEBAB_SPMI_APID_MAP + 4u * apid);
	map_ppid = (map >> 8) & 0xfffu;
	own = kebab_spmi_rd(spmi.cnfg, KEBAB_SPMI_OWNERSHIP + 4u * apid);
	write_ee = own & 0x7u;
	if (map == 0 || map_ppid != ppid || write_ee != KEBAB_SPMI_EE) {
		printf("KEBAB SPMI: APID 0x%03x maps PPID 0x%03x write_ee 0x%x; refusing the write to 0x%04x (need PPID 0x%03x write_ee 0x%x)\n",
		    apid, map_ppid, write_ee, addr, ppid, KEBAB_SPMI_EE);
		return -1;
	}

	cmd = (KEBAB_SPMI_OP_WRITEL << 27) | ((addr & 0xffu) << 4);
	if (spmi.verbose) {
		printf("KEBAB SPMI: write REG 0x%04x: APID 0x%03x chnls-slice+0x%03x cmd 0x%08x wdata 0x%02x\n",
		    addr, apid, 0x10000u * apid, cmd, value);
	}
	kebab_spmi_wr(spmi.chnl, KEBAB_SPMI_WDATA0, value);
	kebab_spmi_wr(spmi.chnl, KEBAB_SPMI_CMD, cmd);

	for (polls = 0; polls < KEBAB_SPMI_TIMEOUT_US; polls++) {
		status = kebab_spmi_rd(spmi.chnl, KEBAB_SPMI_STATUS);
		if (status & KEBAB_SPMI_STATUS_DONE) {
			break;
		}
		delay(1);
	}
	if (!(status & KEBAB_SPMI_STATUS_DONE)) {
		printf("KEBAB SPMI: REG 0x%04x write TIMEOUT after %u us: cmd 0x%08x status 0x%08x\n",
		    addr, KEBAB_SPMI_TIMEOUT_US, cmd, status);
		return -1;
	}
	if (status & (KEBAB_SPMI_STATUS_FAILURE | KEBAB_SPMI_STATUS_DENIED |
	    KEBAB_SPMI_STATUS_DROPPED)) {
		printf("KEBAB SPMI: REG 0x%04x write FAILED: cmd 0x%08x status 0x%08x%s%s%s\n",
		    addr, cmd, status,
		    (status & KEBAB_SPMI_STATUS_FAILURE) ? " FAILURE" : "",
		    (status & KEBAB_SPMI_STATUS_DENIED) ? " DENIED" : "",
		    (status & KEBAB_SPMI_STATUS_DROPPED) ? " DROPPED" : "");
		return -1;
	}
	if (spmi.verbose) {
		printf("KEBAB SPMI: write REG 0x%04x status 0x%08x DONE\n", addr, status);
	}
	return 0;
}

void
kebab_spmi_bus_map_arbiter(void)
{
	spmi.core = ml_io_map(KEBAB_SPMI_CORE_PA, KEBAB_SPMI_CORE_SIZE);
	spmi.obsrvr = ml_io_map(KEBAB_SPMI_OBSRV_PA, KEBAB_SPMI_OBSRV_SIZE);
	spmi.cnfg = ml_io_map(KEBAB_SPMI_CNFG_PA, KEBAB_SPMI_CNFG_SIZE);
}

vm_offset_t
kebab_spmi_bus_map_channel(uint32_t apid)
{
	vm_offset_t slice = KEBAB_SPMI_CHNLS_PA + 0x10000u * apid;
	return ml_io_map(slice, KEBAB_SPMI_CHNL_SLICE);
}
#endif /* KEBAB */
