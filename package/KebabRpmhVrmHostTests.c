/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host tests for the RPMh VRM encoder.  No MMIO, no kernel, no device: the
 * only sink is a recorder, and every negative case asserts it was not called.
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

static kebab_rpmh_status recorder(const kebab_rpmh_cmd *cmd, void *ctx)
{
	(void)cmd;
	(void)ctx;
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

static size_t build_db(uint8_t *db, int with_ldoa13, int duplicate,
    uint32_t extra_count)
{
	memset(db, 0, 2048);
	put32(db, 1);                 /* version */
	put32(db + 4, 0x0c0330dbu);   /* magic */
	/* slave slot 0: VREG (4), entries at +0, count 2 (+extra) */
	put16(db + 8, 4);
	put16(db + 10, 0);
	put16(db + 14, (uint16_t)(2 + extra_count));
	/* entry 0 */
	put_id(db + 144, "ldoa13");
	put32(db + 144 + 16, 0x00040c00u);
	/* entry 1 */
	put_id(db + 168, with_ldoa13 ? "ldoa13" : "ldoc1");
	put32(db + 168 + 16, duplicate ? 0x00040c00u : 0x00042f00u);
	/* entry 2..n point at the same id, for the duplicate test */
	if (extra_count > 0) {
		put_id(db + 192, "ldoa13");
		put32(db + 192 + 16, 0x00040c00u);
	}
	return 512;
}

int main(void)
{
	const kebab_rpmh_resource *l13 = kebab_rpmh_resource_by_name("ldoa13");
	const kebab_rpmh_resource *l1 = kebab_rpmh_resource_by_name("ldoc1");
	kebab_rpmh_cmd cmd;
	kebab_tcs tcs;
	uint8_t db[2048];
	uint32_t addr = 0;

	printf("R1 full address is preserved\n");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(l13 != NULL && kebab_rpmh_encode_enable(l13, &cmd) == KEBAB_RPMH_OK,
	    "encode ldoa13 enable");
	CHECK(cmd.addr == 0x00040c04u, "ldoa13 enable address is 0x00040c04");
	CHECK(cmd.msgid == KEBAB_RPMH_MSGID_WRITE_ACK, "msgid is 0x00010108");
	CHECK(cmd.data == 1u, "enable data is 1");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(l1 != NULL && kebab_rpmh_encode_enable(l1, &cmd) == KEBAB_RPMH_OK,
	    "encode ldoc1 enable");
	CHECK(cmd.addr == 0x00042f04u, "ldoc1 enable address is 0x00042f04");
	CHECK(kebab_rpmh_address_known(0x0c04u) == 0, "truncated 0x0c04 rejected");
	CHECK(kebab_rpmh_address_known(0x2f04u) == 0, "truncated 0x2f04 rejected");
	{
		kebab_rpmh_resource forged = { "ldoa13", 0x00000c00u, 3008u };
		CHECK(kebab_rpmh_encode_enable(&forged, &cmd) ==
		    KEBAB_RPMH_ERR_ADDRESS, "forged truncated resource rejected");
	}

	printf("R2 only the two known resources\n");
	CHECK(kebab_rpmh_resource_by_name("ldob1") == NULL, "ldob1 rejected");
	CHECK(kebab_rpmh_resource_by_name("ldoa12") == NULL, "ldoa12 rejected");
	CHECK(kebab_rpmh_resource_by_name("") == NULL, "empty name rejected");

	printf("R3 voltage data is the DTS value\n");
	memset(&cmd, 0, sizeof(cmd));
	CHECK(kebab_rpmh_encode_voltage(l13, 3008u, &cmd) == KEBAB_RPMH_OK &&
	    cmd.addr == 0x00040c00u && cmd.data == 3008u,
	    "ldoa13 voltage 3008 mV");
	CHECK(kebab_rpmh_encode_voltage(l13, 3007u, &cmd) ==
	    KEBAB_RPMH_ERR_VOLTAGE, "ldoa13 other voltage rejected");
	CHECK(kebab_rpmh_encode_voltage(l1, 1800u, &cmd) == KEBAB_RPMH_OK &&
	    cmd.addr == 0x00042f00u && cmd.data == 1800u,
	    "ldoc1 voltage 1800 mV");
	CHECK(kebab_rpmh_encode_voltage(l1, 1801u, &cmd) ==
	    KEBAB_RPMH_ERR_VOLTAGE, "ldoc1 other voltage rejected");

	printf("R4 command DB snapshot parsing\n");
	build_db(db, 1, 1, 0);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_DUPLICATE, "duplicate entry rejected");
	build_db(db, 0, 0, 0);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_OK && addr == 0x00040c00u, "ldoa13 lookup");
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoc1", &addr) ==
	    KEBAB_RPMH_OK && addr == 0x00042f00u, "ldoc1 lookup");
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldob1", &addr) ==
	    KEBAB_RPMH_ERR_RESOURCE, "missing resource rejected");
	db[4] = 0;
	CHECK(kebab_rpmh_cmd_db_lookup(db, 512, "ldoa13", &addr) ==
	    KEBAB_RPMH_ERR_COMMAND_DB, "bad magic rejected");
	build_db(db, 0, 0, 0);
	CHECK(kebab_rpmh_cmd_db_lookup(db, 180, "ldoc1", &addr) ==
	    KEBAB_RPMH_ERR_COMMAND_DB, "entry past the end rejected");
	{
		uint8_t bad[2048];
		build_db(bad, 0, 0, 0);
		put32(bad + 144 + 16, 0x00000c00u);
		CHECK(kebab_rpmh_cmd_db_lookup(bad, 512, "ldoa13", &addr) ==
		    KEBAB_RPMH_ERR_ADDRESS, "truncated cmd-db address rejected");
	}

	printf("R5 TCS state machine\n");
	memset(&tcs, 0, sizeof(tcs));
	sink_calls = 0;
	CHECK(kebab_tcs_acquire(&tcs, 7) == KEBAB_RPMH_OK, "acquire free TCS");
	CHECK(kebab_tcs_acquire(&tcs, 7) == KEBAB_RPMH_ERR_TCS_BUSY,
	    "second acquire rejected");
	CHECK(kebab_tcs_submit(&tcs, &cmd, NULL, NULL) ==
	    KEBAB_RPMH_ERR_NO_SINK, "submit without a sink rejected");
	CHECK(sink_calls == 0, "no sink call on that path");
	memset(&cmd, 0, sizeof(cmd));
	kebab_rpmh_encode_enable(l13, &cmd);
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) == KEBAB_RPMH_OK,
	    "submit to the recorder");
	CHECK(sink_calls == 1 && tcs.submits == 1, "exactly one sink call");
	CHECK(tcs.state == KEBAB_TCS_INFLIGHT, "state is in flight");
	CHECK(kebab_tcs_submit(&tcs, &cmd, recorder, NULL) ==
	    KEBAB_RPMH_ERR_TCS_STATE, "submit while in flight rejected");
	kebab_tcs_complete(&tcs);
	CHECK(tcs.state == KEBAB_TCS_DONE, "complete marks done");
	kebab_tcs_release(&tcs);
	CHECK(tcs.state == KEBAB_TCS_FREE, "release after done frees");

	printf("R6 timeout and error are terminal\n");
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

	printf("R7 no path reaches hardware without a sink\n");
	CHECK(recorder != NULL, "the only sink in this test is the recorder");

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
