/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * RPMh VRM encoding for the two rails the kebab touch part needs.
 *
 * The module is free of MMIO and of kernel dependencies.  It never trusts a
 * caller-supplied resource structure: the encoder takes a name, looks it up in
 * its own read-only table, and checks the address and the voltage against that
 * table.  A struct with a valid address and an invented voltage therefore has
 * no path through this API.
 *
 * The address is the full 32-bit command DB word.  Bits 19:16 are the
 * accelerator type (4 = VREG) and are part of what a TCS must carry; adding
 * the register offset keeps them.
 */
#ifndef KEBAB_RPMH_VRM_H
#define KEBAB_RPMH_VRM_H

#include <stddef.h>
#include <stdint.h>

#define KEBAB_RPMH_MSGID_WRITE_ACK 0x00010108u

#define KEBAB_RPMH_REG_VOLTAGE 0x0u
#define KEBAB_RPMH_REG_ENABLE  0x4u
#define KEBAB_RPMH_REG_MODE    0x8u

#define KEBAB_RPMH_ACCEL_VREG  0x4u
#define KEBAB_RPMH_ACCEL_SHIFT 16

typedef enum {
	KEBAB_RPMH_OK = 0,
	KEBAB_RPMH_ERR_ARGUMENT,
	KEBAB_RPMH_ERR_RESOURCE,
	KEBAB_RPMH_ERR_ADDRESS,
	KEBAB_RPMH_ERR_VOLTAGE,
	KEBAB_RPMH_ERR_COMMAND_DB,
	KEBAB_RPMH_ERR_DUPLICATE,
	KEBAB_RPMH_ERR_TCS_BUSY,
	KEBAB_RPMH_ERR_TCS_STATE,
	KEBAB_RPMH_ERR_NO_SINK,
	KEBAB_RPMH_ERR_SINK_FAILED,
	KEBAB_RPMH_ERR_NOT_ALLOWED,
} kebab_rpmh_status;

typedef struct {
	const char *name;
	uint32_t    addr;          /* full command DB address */
	uint32_t    voltage_mv;    /* fixed by the vendor DTS */
} kebab_rpmh_resource;

typedef struct {
	uint32_t msgid;
	uint32_t addr;
	uint32_t data;
} kebab_rpmh_cmd;

/* A sink is the only place a command could ever reach hardware. */
typedef kebab_rpmh_status (*kebab_rpmh_sink)(const kebab_rpmh_cmd *cmd,
    void *ctx);

typedef enum {
	KEBAB_TCS_FREE = 0,
	KEBAB_TCS_BUSY,
	KEBAB_TCS_INFLIGHT,
	KEBAB_TCS_DONE,
	KEBAB_TCS_TIMEOUT,
	KEBAB_TCS_ERROR,
} kebab_tcs_state;

typedef struct {
	kebab_tcs_state state;
	uint32_t        owner;
	uint32_t        submits;
	kebab_rpmh_cmd  inflight;
} kebab_tcs;

/* Read-only table of the two allowed rails. */
const kebab_rpmh_resource *kebab_rpmh_resource_by_name(const char *name);
int kebab_rpmh_address_known(uint32_t addr);

/* Encode by name; the table, not the caller, decides address and voltage. */
kebab_rpmh_status kebab_rpmh_encode_enable(const char *name,
    kebab_rpmh_cmd *out);
kebab_rpmh_status kebab_rpmh_encode_voltage(const char *name, uint32_t mv,
    kebab_rpmh_cmd *out);

/* True only for a command this module could itself have produced. */
int kebab_rpmh_cmd_allowed(const kebab_rpmh_cmd *cmd);

/* Read-only parse of a command DB snapshot; name, address and VREG slave id
 * are all checked against the table. */
kebab_rpmh_status kebab_rpmh_cmd_db_lookup(const uint8_t *db, size_t len,
    const char *name, uint32_t *addr_out);

kebab_rpmh_status kebab_tcs_acquire(kebab_tcs *tcs, uint32_t owner);
kebab_rpmh_status kebab_tcs_submit(kebab_tcs *tcs, const kebab_rpmh_cmd *cmd,
    kebab_rpmh_sink sink, void *ctx);
void kebab_tcs_complete(kebab_tcs *tcs);
void kebab_tcs_timeout(kebab_tcs *tcs);
void kebab_tcs_fail(kebab_tcs *tcs);
void kebab_tcs_release(kebab_tcs *tcs);

#endif /* KEBAB_RPMH_VRM_H */
