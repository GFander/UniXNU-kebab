/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host tests for the RPMh VRM encoder.  No MMIO, no kernel, no device.
 *
 * The sinks here are deliberately adversarial: one records, one refuses
 * anything it did not expect, one completes synchronously from inside the
 * call, and one reports failure.  Every negative case also asserts the sink
 * was not reached.
 */
#include "KebabRpmhVrm.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, what) do { \
	checks++; \
	if (cond) { \
		printf("  PASS  %s\n", what); \
	} else { \
		failures++; \
		printf("  FAIL  %s\n", what); \
	} \
} while (0)

static int sink_calls;
static int sink_refused;
static kebab_tcs *sync_tcs;

static kebab_rpmh_status recorder(const kebab_rpmh_cmd *cmd, void *ctx)
{
	(void)ctx;
	if (!kebab_rpmh_cmd_allowed(cmd)) {
		sink_refused++;
		return KEBAB_RPMH_ERR_NOT_ALLOWED;
	}
	sink_calls++;
	return KEBAB_RPMH_OK;
}

static kebab_rpmh_status failing_sink(const kebab_rpmh_cmd *cmd, void *ctx)
{
	(void)cmd;
	(void)ctx;
	sink_calls++;
	return KEBAB_RPMH_ERR_SINK_FAILED;
}

static kebab_rpmh_status sync_complete_sink(const kebab_rpmh_cmd *cmd,
    void *ctx)
{
	(void)cmd;
	(void)ctx;
	sink_calls++;
	kebab_tcs_complete(sync_tcs);
	return KEBAB_RPMH_OK;
}

static kebab_rpmh_status sync_fail_sink(const kebab_rpmh_cmd *cmd, void *ctx)
{
	(void)cmd;
	(void)ctx;
	sink_calls++;
	kebab_tcs_fail(sync_tcs);
	return KEBAB_RPMH_OK;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)((v >> 16) & 0xff);
	p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_id(uint8_t *p, const char *id)
{
	memset(p, 0, 8);
	memcpy(p, id, strlen(id));
}

/* One slave slot at index 0 with two entries: ldoa13, ldoc1. */
static void build_db(uint8_t *db, uint16_t slv, uint32_t ldoa13_addr,
    uint32_t ldoc1_addr)
{
	memset(db, 0, 2048);
	put32(db, 1);
	put32(db + 4, 0x0c0330dbu);
	put16(db + 8, slv);
	put16(db + 10, 0);
	put16(db + 14, 2);
	put_id(db + 144, "ldoa13");
	put32(db + 144 + 16, ldoa13_addr);
	put_id(db + 168, "ldoc1");
	put32(db + 168 + 16, ldoc1_addr);
}

int main(void)
{
	kebab_rpmh_cmd cmd;
	kebab_tcs tcs;
	uint8_t db[2048];
	uint32_t addr = 0;

	printf("R1 full address is preserved\n");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(kebab_rpmh_encode_enable("ldoa13", &cmd) == KEBAB_RPMH_OK,
	    "encode ldoa13 enable");
	CHECK(cmd.addr == 0x00040c04u, "ldoa13 enable address is 0x00040c04");
	CHECK(cmd.msgid == KEBAB_RPMH_MSGID_WRITE_ACK, "msgid is 0x00010108");
	CHECK(cmd.data == 1u, "enable data is 1");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(kebab_rpmh_encode_enable("ldoc1", &cmd) == KEBAB_RPMH_OK,
	    "encode ldoc1 enable");
	CHECK(cmd.addr == 0x00042f04u, "ldoc1 enable address is 0x00042f04");
	CHECK(kebab_rpmh_address_known(0x0c04u) == 0, "truncated 0x0c04 rejected");
	CHECK(kebab_rpmh_address_known(0x2f04u) == 0, "truncated 0x2f04 rejected");

	printf("R2 only the two known resources\n");
	CHECK(kebab_rpmh_resource_by_name("ldob1") == NULL, "ldob1 rejected");
	CHECK(kebab_rpmh_resource_by_name("ldoa12") == NULL, "ldoa12 rejected");
	CHECK(kebab_rpmh_resource_by_name("") == NULL, "empty name rejected");
	CHECK(kebab_rpmh_encode_enable(NULL, &cmd) == KEBAB_RPMH_ERR_RESOURCE,
	    "NULL name rejected");
	CHECK(kebab_rpmh_encode_enable("ldoc1", NULL) ==
	    KEBAB_RPMH_ERR_ARGUMENT, "NULL output rejected");

	printf("R3 voltage comes from the table, not the caller\n");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(kebab_rpmh_encode_voltage("ldoa13", 3008u, &cmd) ==
	    KEBAB_RPMH_OK && cmd.addr == 0x00040c00u && cmd.data == 3008u,
	    "ldoa13 voltage 3008 mV");
	CHECK(kebab_rpmh_encode_voltage("ldoa13", 3300u, &cmd) ==
	    KEBAB_RPMH_ERR_VOLTAGE, "ldoa13 3300 mV rejected");
	CHECK(kebab_rpmh_encode_voltage("ldoc1", 1800u, &cmd) ==
	    KEBAB_RPMH_OK && cmd.addr == 0x00042f00u && cmd.data == 1800u,
	    "ldoc1 voltage 1800 mV");
	CHECK(kebab_rpmh_encode_voltage("ldoc1", 3300u, &cmd) ==
	    KEBAB_RPMH_ERR_VOLTAGE, "ldoc1 3300 mV rejected");

	printf("R4 only encoder commands are accepted by a channel\n");
	{
		kebab_rpmh_cmd forged = { KEBAB_RPMH_MSGID_WRITE_ACK,
			0x00040c00u, 3300u };

		CHECK(kebab_rpmh_cmd_allowed(&forged) == 0,
		    "forged 3300 mV command rejected");
		forged.addr = 0x00040c04u;
		forged.data = 0u;
		CHECK(kebab_rpmh_cmd_allowed(&forged) == 0,
		    "enable with data 0 rejected");
		forged.addr = 0x00010c00u;   /* legal-looking, not a rail */
		forged.data = 1u;
		CHECK(kebab_rpmh_cmd_allowed(&forged) == 0,
		    "other accelerator address rejected");
		forged.addr = 0x00040c04u;
		forged.msgid = 0x00010100u;
		CHECK(kebab_rpmh_cmd_allowed(&forged) == 0,
		    "wrong msgid rejected");
	}

	printf("R5 command DB name/address/slave must agree\n");
	build_db(db, 4, 0x00040c00u, 0x00042f00u);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_OK && addr == 0x00040c00u, "ldoa13 lookup");
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoc1", &addr) ==
	    KEBAB_RPMH_OK && addr == 0x00042f00u, "ldoc1 lookup");
	build_db(db, 4, 0x00042f00u, 0x00042f00u);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_ADDRESS, "cross-mapped ldoa13 address rejected");
	build_db(db, 2, 0x00040c00u, 0x00042f00u);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_ADDRESS, "entry outside the VREG slave rejected");
	build_db(db, 4, 0x00040c00u, 0x00042f00u);
	db[4] = 0;
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_COMMAND_DB, "bad magic rejected");
	build_db(db, 4, 0x00040c00u, 0x00042f00u);
	put16(db + 14, 1);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoc1", &addr) ==
	    KEBAB_RPMH_ERR_RESOURCE, "missing resource rejected");
	build_db(db, 4, 0x00040c00u, 0x00042f00u);
	put16(db + 14, 4);
	put_id(db + 192, "ldoa13");
	put32(db + 192 + 16, 0x00040c00u);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_DUPLICATE, "duplicate entry rejected");

	printf("R6 TCS state machine and adversarial sinks\n");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	sink_refused = 0;
	CHECK(kebab_tcs_acquire(&tcs, 7) == KEBAB_RPMH_OK, "acquire free TCS");
	CHECK(kebab_tcs_acquire(&tcs, 7) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "second acquire rejected");
	CHECK(kebab_tcs_submit(&tcs, &cmd, NULL, NULL) ==
	    KEBAB_RPMH_ERR_NO_SINK, "submit without a sink rejected");
	CHECK(sink_calls == 0, "no sink call on that path");
	memset(&cmd, 0, sizeof(cmd));
	kebab_rpmh_encode_enable("ldoa13", &cmd);
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) == KEBAB_RPMH_OK,
	    "submit to the recorder");
	CHECK(sink_calls == 1 && tcs.submits == 1, "exactly one sink call");
	CHECK(tcs.state == KEBAB_TCS_INFLIGHT, "state is in flight");
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) ==
	    KEBAB_RPMH_ERR_TCS_STATE, "submit while in flight rejected");
	kebab_tcs_complete(&tcs);
	CHECK(tcs.state == KEBAB_TCS_DONE, "complete marks done");
	kebab_tcs_complete(&tcs);
	CHECK(tcs.state == KEBAB_TCS_DONE, "duplicate complete is ignored");
	kebab_tcs_release(&tcs);
	CHECK(tcs.state == KEBAB_TCS_FREE, "release after done frees");

	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	kebab_tcs_acquire(&tcs, 7);
	{
		kebab_rpmh_cmd forged = { KEBAB_RPMH_MSGID_WRITE_ACK,
			0x00040c00u, 3300u };

		CHECK(kebab_tcs_submit(&tcs, &forged, recorder, NULL) ==
		    KEBAB_RPMH_ERR_NOT_ALLOWED, "forged command never submitted");
		CHECK(sink_calls == 0 && tcs.state == KEBAB_TCS_BUSY,
		    "channel untouched by the rejected command");
	}

	printf("R7 synchronous completion inside the sink\n");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	sync_tcs = &tcs;
	kebab_tcs_acquire(&tcs, 7);
	CHECK(kebab_tcs_submit(&tcs, &cmd, sync_complete_sink, NULL) ==
	    KEBAB_RPMH_OK, "synchronous sink returns success");
	CHECK(tcs.state == KEBAB_TCS_DONE,
	    "state is DONE, not overwritten to INFLIGHT");
	CHECK(sink_calls == 1, "one sink call");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	kebab_tcs_acquire(&tcs, 7);
	CHECK(kebab_tcs_submit(&tcs, &cmd, sync_fail_sink, NULL) ==
	    KEBAB_RPMH_OK, "sink that fails the channel returns success");
	CHECK(tcs.state == KEBAB_TCS_ERROR, "channel is in error");
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) ==
	    KEBAB_RPMH_ERR_TCS_STATE, "no retry after in-sink failure");

	printf("R8 timeout and error are terminal\n");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	kebab_tcs_acquire(&tcs, 7);
	kebab_tcs_submit(&tcs, &cmd, recorder, NULL);
	kebab_tcs_timeout(&tcs);
	CHECK(tcs.state == KEBAB_TCS_TIMEOUT, "timeout recorded");
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) ==
	    KEBAB_RPMH_ERR_TCS_STATE, "no retry after timeout");
	kebab_tcs_release(&tcs);
	CHECK(tcs.state == KEBAB_TCS_TIMEOUT, "release does not clear a timeout");
	CHECK(sink_calls == 1, "still exactly one sink call");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	kebab_tcs_acquire(&tcs, 7);
	CHECK(kebab_tcs_submit(&tcs, &cmd, failing_sink, NULL) ==
	    KEBAB_RPMH_ERR_SINK_FAILED, "sink failure reported");
	CHECK(tcs.state == KEBAB_TCS_ERROR, "sink failure leaves error state");
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) ==
	    KEBAB_RPMH_ERR_TCS_STATE, "no retry after error");
	CHECK(sink_calls == 1, "failing sink called once, nothing after");

	printf("R9 the recorder refuses what the encoder did not build\n");
	sink_calls = 0;
	sink_refused = 0;
	{
		kebab_rpmh_cmd forged = { KEBAB_RPMH_MSGID_WRITE_ACK,
			0x00042f04u, 0u };

		CHECK(recorder(&forged, NULL) == KEBAB_RPMH_ERR_NOT_ALLOWED,
		    "recorder rejects a bad command");
		CHECK(sink_refused == 1 && sink_calls == 0,
		    "refusal counted, no accepted call");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
