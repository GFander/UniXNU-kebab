/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "KebabRpmhVrm.h"

#include <string.h>

static const kebab_rpmh_resource kebab_resources[] = {
	{ "ldoa13", 0x00040c00u, 3008u },
	{ "ldoc1",  0x00042f00u, 1800u },
};

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

const kebab_rpmh_resource *kebab_rpmh_resource_by_name(const char *name)
{
	size_t i;

	if (name == NULL) {
		return NULL;
	}
	for (i = 0; i < sizeof(kebab_resources) / sizeof(kebab_resources[0]);
	    i++) {
		if (strcmp(name, kebab_resources[i].name) == 0) {
			return &kebab_resources[i];
		}
	}
	return NULL;
}

int kebab_rpmh_address_known(uint32_t addr)
{
	size_t i;

	for (i = 0; i < sizeof(kebab_resources) / sizeof(kebab_resources[0]);
	    i++) {
		if (addr == kebab_resources[i].addr) {
			return 1;
		}
	}
	return 0;
}

/*
 * The only encoder.  It resolves the name against the table and then builds
 * the command from the table entry, never from caller data: a forged resource
 * cannot enter, and the voltage check cannot be satisfied by a struct field.
 */
static kebab_rpmh_status encode(const char *name, uint32_t offset,
    uint32_t data, kebab_rpmh_cmd *out)
{
	const kebab_rpmh_resource *res = kebab_rpmh_resource_by_name(name);

	if (res == NULL) {
		return KEBAB_RPMH_ERR_RESOURCE;
	}
	if (out == NULL) {
		return KEBAB_RPMH_ERR_ARGUMENT;
	}
	if (!kebab_rpmh_address_known(res->addr)) {
		return KEBAB_RPMH_ERR_ADDRESS;
	}
	if (((res->addr >> KEBAB_RPMH_ACCEL_SHIFT) & 0xfu) !=
	    KEBAB_RPMH_ACCEL_VREG) {
		return KEBAB_RPMH_ERR_ADDRESS;
	}
	out->msgid = KEBAB_RPMH_MSGID_WRITE_ACK;
	out->addr = res->addr + offset;
	out->data = data;
	return KEBAB_RPMH_OK;
}

kebab_rpmh_status kebab_rpmh_encode_enable(const char *name,
    kebab_rpmh_cmd *out)
{
	return encode(name, KEBAB_RPMH_REG_ENABLE, 1u, out);
}

kebab_rpmh_status kebab_rpmh_encode_voltage(const char *name, uint32_t mv,
    kebab_rpmh_cmd *out)
{
	const kebab_rpmh_resource *res = kebab_rpmh_resource_by_name(name);

	if (res == NULL) {
		return KEBAB_RPMH_ERR_RESOURCE;
	}
	if (mv != res->voltage_mv) {
		return KEBAB_RPMH_ERR_VOLTAGE;
	}
	return encode(name, KEBAB_RPMH_REG_VOLTAGE, res->voltage_mv, out);
}

/*
 * A command is allowed only if it is exactly one the table can produce: the
 * fixed msgid, one of the four addresses, and the data that belongs to that
 * address.  The TCS accepts nothing else, so a future MMIO sink is never
 * handed a command the encoder did not build.
 */
int kebab_rpmh_cmd_allowed(const kebab_rpmh_cmd *cmd)
{
	const kebab_rpmh_resource *res;
	size_t i;

	if (cmd == NULL || cmd->msgid != KEBAB_RPMH_MSGID_WRITE_ACK) {
		return 0;
	}
	for (i = 0; i < sizeof(kebab_resources) / sizeof(kebab_resources[0]);
	    i++) {
		res = &kebab_resources[i];
		if (cmd->addr == res->addr + KEBAB_RPMH_REG_ENABLE) {
			return cmd->data == 1u;
		}
		if (cmd->addr == res->addr + KEBAB_RPMH_REG_VOLTAGE) {
			return cmd->data == res->voltage_mv;
		}
	}
	return 0;
}

static int id_matches(const uint8_t *id, const char *name)
{
	size_t i, len = strlen(name);

	for (i = 0; i < 8; i++) {
		char c = (i < len) ? name[i] : '\0';

		if ((char)id[i] != c) {
			return 0;
		}
	}
	return 1;
}

kebab_rpmh_status kebab_rpmh_cmd_db_lookup(const uint8_t *db, size_t len,
    const char *name, uint32_t *addr_out)
{
	const kebab_rpmh_resource *expect = kebab_rpmh_resource_by_name(name);
	uint32_t i, j;
	int found = 0;
	uint32_t found_addr = 0;

	if (db == NULL || name == NULL || addr_out == NULL || len < 144u) {
		return KEBAB_RPMH_ERR_ARGUMENT;
	}
	if (expect == NULL) {
		return KEBAB_RPMH_ERR_RESOURCE;
	}
	if (rd32(db + 4) != 0x0c0330dbu || rd32(db) != 1u) {
		return KEBAB_RPMH_ERR_COMMAND_DB;
	}
	for (i = 0; i < 8; i++) {
		size_t hdr = 8u + (size_t)i * 16u;
		uint16_t slv = rd16(db + hdr);
		uint16_t hdr_off = rd16(db + hdr + 2u);
		uint16_t cnt = rd16(db + hdr + 6u);

		if (cnt == 0) {
			continue;
		}
		if (cnt > 1024u) {
			return KEBAB_RPMH_ERR_COMMAND_DB;
		}
		for (j = 0; j < cnt; j++) {
			size_t e = 144u + (size_t)hdr_off + (size_t)j * 24u;

			if (e + 24u > len) {
				return KEBAB_RPMH_ERR_COMMAND_DB;
			}
			if (!id_matches(db + e, name)) {
				continue;
			}
			if (found) {
				return KEBAB_RPMH_ERR_DUPLICATE;
			}
			/* The entry must sit in the VREG slave and carry the
			 * table address; a name pointing at another rail's
			 * legal address is a cross-mapping, not a hit. */
			if (slv != KEBAB_RPMH_ACCEL_VREG ||
			    rd32(db + e + 16u) != expect->addr) {
				return KEBAB_RPMH_ERR_ADDRESS;
			}
			found = 1;
			found_addr = rd32(db + e + 16u);
		}
	}
	if (!found) {
		return KEBAB_RPMH_ERR_RESOURCE;
	}
	*addr_out = found_addr;
	return KEBAB_RPMH_OK;
}

kebab_rpmh_status kebab_tcs_acquire(kebab_tcs *tcs, uint32_t owner)
{
	if (tcs == NULL) {
		return KEBAB_RPMH_ERR_ARGUMENT;
	}
	if (tcs->state != KEBAB_TCS_FREE) {
		return KEBAB_RPMH_ERR_TCS_BUSY;
	}
	tcs->state = KEBAB_TCS_BUSY;
	tcs->owner = owner;
	return KEBAB_RPMH_OK;
}

kebab_rpmh_status kebab_tcs_submit(kebab_tcs *tcs, const kebab_rpmh_cmd *cmd,
    kebab_rpmh_sink sink, void *ctx)
{
	kebab_rpmh_status rc;

	if (tcs == NULL || cmd == NULL) {
		return KEBAB_RPMH_ERR_ARGUMENT;
	}
	if (!kebab_rpmh_cmd_allowed(cmd)) {
		return KEBAB_RPMH_ERR_NOT_ALLOWED;
	}
	/*
	 * A timed-out or failed channel is never retried in place: the caller
	 * has to reset it deliberately after recording the state.
	 */
	if (tcs->state == KEBAB_TCS_TIMEOUT || tcs->state == KEBAB_TCS_ERROR) {
		return KEBAB_RPMH_ERR_TCS_STATE;
	}
	if (tcs->state != KEBAB_TCS_BUSY) {
		/* Covers both "not acquired" and "already in flight". */
		return KEBAB_RPMH_ERR_TCS_STATE;
	}
	if (sink == NULL) {
		return KEBAB_RPMH_ERR_NO_SINK;
	}
	/*
	 * Enter INFLIGHT before the sink runs.  A sink that reports completion
	 * synchronously may call kebab_tcs_complete() from inside the call; the
	 * state is already in flight there, and the final assignment below can
	 * no longer overwrite a DONE.
	 */
	tcs->inflight = *cmd;
	tcs->submits++;
	tcs->state = KEBAB_TCS_INFLIGHT;
	rc = sink(cmd, ctx);
	if (rc != KEBAB_RPMH_OK) {
		tcs->state = KEBAB_TCS_ERROR;
		return KEBAB_RPMH_ERR_SINK_FAILED;
	}
	return KEBAB_RPMH_OK;
}

void kebab_tcs_complete(kebab_tcs *tcs)
{
	if (tcs != NULL && tcs->state == KEBAB_TCS_INFLIGHT) {
		tcs->state = KEBAB_TCS_DONE;
	}
}

void kebab_tcs_timeout(kebab_tcs *tcs)
{
	if (tcs != NULL && tcs->state == KEBAB_TCS_INFLIGHT) {
		tcs->state = KEBAB_TCS_TIMEOUT;
	}
}

void kebab_tcs_fail(kebab_tcs *tcs)
{
	if (tcs != NULL && tcs->state == KEBAB_TCS_INFLIGHT) {
		tcs->state = KEBAB_TCS_ERROR;
	}
}

void kebab_tcs_release(kebab_tcs *tcs)
{
	if (tcs == NULL) {
		return;
	}
	/* Only a completed channel returns to free; a timed-out one stays
	 * recorded until whoever owns the policy clears it. */
	if (tcs->state == KEBAB_TCS_DONE) {
		tcs->state = KEBAB_TCS_FREE;
		tcs->owner = 0;
	}
}
