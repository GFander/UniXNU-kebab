#ifndef _KEBAB_SPMI_BUS_H
#define _KEBAB_SPMI_BUS_H
#include <sys/types.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resource order: core, observer, configuration, channel window (u64 pairs).
 * No mapped addresses are exposed. Normal clients must serialize complete
 * transactions; early/panic PON deliberately uses the bounded unlocked core. */
struct kebab_spmi_bus_info {
	uint64_t resources[8];
	uint32_t version, ee, write_apid;
	bool mapped, write_channel_mapped;
};
struct kebab_spmi_match {
	uint32_t apid;
	uint32_t ppid;
	uint32_t map_raw;
	uint32_t own_raw;
	uint32_t write_ee;
	uint32_t irq_owner;
	uint32_t irq_ee;
};

#define KEBAB_SPMI_APID_MAX 512u
const char *
kebab_spmi_family(uint32_t version);
bool
kebab_spmi_version_known(uint32_t version);
uint32_t
kebab_spmi_scan(uint32_t ppid, struct kebab_spmi_match *matches,
    uint32_t max_matches);
int
kebab_spmi_reader(uint32_t ppid, uint32_t *apid_out);
void
kebab_spmi_reader_forget(uint32_t ppid);
int
kebab_spmi_obs_read_ex(uint32_t apid, uint32_t sid, uint32_t addr,
    uint32_t attempt, bool verbose, bool quiet, uint8_t *value);
int
kebab_spmi_rw_write(uint32_t apid, uint32_t sid, uint32_t addr, uint8_t value);
void
kebab_spmi_match_line(const struct kebab_spmi_match *m);
void kebab_spmi_bus_lock(void);
void kebab_spmi_bus_unlock(void);
bool kebab_spmi_bus_ready(void);
uint32_t kebab_spmi_bus_version(void);
uint32_t kebab_spmi_bus_read_version(void);
uint32_t kebab_spmi_bus_next_attempt(void);
bool kebab_spmi_bus_verbose(void);
void kebab_spmi_bus_set_verbose(bool verbose);
uint32_t kebab_spmi_bus_mapping(uint32_t apid);
uint64_t kebab_spmi_bus_channel_physical(uint32_t apid);
void kebab_spmi_bus_get_info(struct kebab_spmi_bus_info *info);
bool
kebab_spmi_bus_init(void);
void
kebab_spmi_bus_prepare_write(uint32_t apid);
#ifdef __cplusplus
}
#endif
#endif
