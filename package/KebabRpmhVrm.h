/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * RPMh VRM encoding and the TCS1 send state machine for the two rails the
 * kebab touch part needs.
 *
 * The module is free of MMIO and of kernel dependencies.  It never trusts a
 * caller-supplied resource structure: the encoder takes a name, looks it up in
 * its own read-only table, and checks the address and the voltage against that
 * table.  The send path talks to an injected kebab_tcs_io, so the host tests
 * drive the same state machine against a virtual register file.
 *
 * The address is the full 32-bit command DB word.  Bits 19:16 are the
 * accelerator type (4 = VREG) and are part of what a TCS must carry.
 */
#ifndef KEBAB_RPMH_VRM_H
#define KEBAB_RPMH_VRM_H

#ifdef KEBAB_RPMH_KERNEL
#include <sys/types.h>
#include <libkern/libkern.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

#define KEBAB_RPMH_MSGID_WRITE_ACK 0x00010108u

#define KEBAB_RPMH_REG_VOLTAGE 0x0u
#define KEBAB_RPMH_REG_ENABLE  0x4u
#define KEBAB_RPMH_REG_MODE    0x8u

#define KEBAB_RPMH_ACCEL_VREG  0x4u
#define KEBAB_RPMH_ACCEL_SHIFT 16

/* TCS register offsets, from rpmh-rsc.c. */
#define KEBAB_TCS_CMD_WAIT     0x10u
#define KEBAB_TCS_CONTROL      0x14u
#define KEBAB_TCS_STATUS       0x18u
#define KEBAB_TCS_CMD_ENABLE   0x1cu
#define KEBAB_TCS_CMD_MSGID    0x30u
#define KEBAB_TCS_CMD_ADDR     0x34u
#define KEBAB_TCS_CMD_DATA     0x38u
#define KEBAB_TCS_CMD_STATUS   0x3cu
#define KEBAB_TCS_RESP_DATA    0x40u

#define KEBAB_TCS_AMC_ENABLE   (1u << 16)
#define KEBAB_TCS_AMC_TRIGGER  (1u << 24)
#define KEBAB_TCS_CMD_ISSUED   (1u << 8)
#define KEBAB_TCS_CMD_COMPL    (1u << 16)

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
	KEBAB_RPMH_PENDING,
	KEBAB_RPMH_ERR_TIMEOUT,
	KEBAB_RPMH_ERR_RECLAIM,
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

const kebab_rpmh_resource *kebab_rpmh_resource_by_name(const char *name);
int kebab_rpmh_address_known(uint32_t addr);
kebab_rpmh_status kebab_rpmh_encode_enable(const char *name,
    kebab_rpmh_cmd *out);
kebab_rpmh_status kebab_rpmh_encode_voltage(const char *name, uint32_t mv,
    kebab_rpmh_cmd *out);
int kebab_rpmh_cmd_allowed(const kebab_rpmh_cmd *cmd);
kebab_rpmh_status kebab_rpmh_cmd_db_lookup(const uint8_t *db, size_t len,
    const char *name, uint32_t *addr_out);

/* ---------------------------------------------------------- hardware model */

/* rd/wr return 0 on success and -1 on failure; an unset operation fails.
 * now_us is optional; when present every wait has a time budget as well as a
 * poll cap, so a stalled or jumping clock cannot spin forever. */
typedef struct {
	int (*rd)(void *ctx, uint32_t off, uint32_t *val);
	int (*wr)(void *ctx, uint32_t off, uint32_t val);
	uint64_t (*now_us)(void *ctx);
	void *ctx;
} kebab_tcs_io;

#define KEBAB_TCS_RB_TIMEOUT_US   10000u
#define KEBAB_TCS_CMPL_TIMEOUT_US 100000u

typedef enum {
	KEBAB_TCS_PHASE_IDLE = 0,
	KEBAB_TCS_PHASE_VALIDATED,
	KEBAB_TCS_PHASE_ARMED,
	KEBAB_TCS_PHASE_TRIGGERED,
	KEBAB_TCS_PHASE_COMPLETE,
	KEBAB_TCS_PHASE_FAILED_PRE_TRIGGER,
	KEBAB_TCS_PHASE_FAILED_POST_TRIGGER,
	KEBAB_TCS_PHASE_COMPLETE_UNCLEAN,
} kebab_tcs_phase;

/*
 * One-shot and dead state live outside the channel structure, in an object the
 * caller owns for the life of the boot: re-initialising a channel must not be
 * able to unlock another attempt.
 */
typedef struct {
	int dead;
	int used;
	int busy;      /* in-flight claim; a second caller is refused */
} kebab_rpmh_lock;

typedef struct {
	kebab_tcs_io     io;
	uint32_t         tcs;        /* only 1 is accepted */
	kebab_rpmh_lock *lock;
	kebab_tcs_phase  phase;
	uint32_t         last_status; /* CMD_STATUS at the end of the attempt */
} kebab_rpmh_tcs;

void kebab_rpmh_tcs_init(kebab_rpmh_tcs *t, kebab_tcs_io io,
    kebab_rpmh_lock *lock);
kebab_rpmh_status kebab_rpmh_tcs_send(kebab_rpmh_tcs *t,
    const kebab_rpmh_cmd *cmd);

/* Simulated channel used by the TCS state tests (not used by send). */
typedef enum {
	KEBAB_TCS_FREE = 0,
	KEBAB_TCS_BUSY,
	KEBAB_TCS_INFLIGHT,
	KEBAB_TCS_DONE,
	KEBAB_TCS_TIMEOUT,
	KEBAB_TCS_ERROR,
} kebab_tcs_state;

typedef kebab_rpmh_status (*kebab_rpmh_sink)(const kebab_rpmh_cmd *cmd,
    void *ctx);

typedef struct {
	kebab_tcs_state state;
	uint32_t        owner;
	uint32_t        submits;
	kebab_rpmh_cmd  inflight;
} kebab_tcs;

kebab_rpmh_status kebab_tcs_acquire(kebab_tcs *tcs, uint32_t owner);
kebab_rpmh_status kebab_tcs_submit(kebab_tcs *tcs, const kebab_rpmh_cmd *cmd,
    kebab_rpmh_sink sink, void *ctx);
void kebab_tcs_complete(kebab_tcs *tcs);
void kebab_tcs_timeout(kebab_tcs *tcs);
void kebab_tcs_fail(kebab_tcs *tcs);
void kebab_tcs_release(kebab_tcs *tcs);

#endif /* KEBAB_RPMH_VRM_H */
