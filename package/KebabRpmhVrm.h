/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * RPMh VRM encoding for the two rails the kebab touch part needs.
 *
 * This module is deliberately free of MMIO and of any kernel dependency: it
 * turns a validated command DB resource into the {msgid, addr, data} triple a
 * TCS would carry, and nothing else.  The host tests link it directly; the
 * kernel port will reuse the same encoding rules.
 *
 * The address is the full 32-bit command DB word.  Bits 19:16 are the
 * accelerator type (4 = VREG) and are part of the address a TCS must carry;
 * adding the register offset keeps them.  Truncating to the low 16 bits is a
 * bug this file refuses on purpose.
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

/* A sink is the only place a command could ever reach hardware.  Tests pass
 * NULL or a recorder; no other code path exists. */
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

/* Fixed table: ldoa13 (PM8150 L13, 3.008 V) and ldoc1 (PM8150L L1, 1.800 V).
 * Returns NULL for any other name, which is what keeps a console from
 * becoming an arbitrary-address interface. */
const kebab_rpmh_resource *kebab_rpmh_resource_by_name(const char *name);

/* True only for the two addresses above; a truncated 0x0c00/0x2f00 or any
 * other value is false. */
int kebab_rpmh_address_known(uint32_t addr);

kebab_rpmh_status kebab_rpmh_encode_enable(const kebab_rpmh_resource *res,
    kebab_rpmh_cmd *out);
kebab_rpmh_status kebab_rpmh_encode_voltage(const kebab_rpmh_resource *res,
    uint32_t mv, kebab_rpmh_cmd *out);

/* Minimal read-only parse of a command DB snapshot, mirroring the kernel
 * probe: magic, bounds, duplicate ids.  Used by the tests to prove the
 * rejection paths. */
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
