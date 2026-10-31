/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define _GNU_SOURCE
/*
 * The TCM framing, payload layouts, report program, report decoder and tracker
 * against synthetic byte streams.  No I2C, no kernel, no chip: every byte the
 * test feeds in is written down here, so a wrong assumption about the wire
 * format is a failing line rather than a quiet misread on the board.
 *
 * The byte streams are the measured ones where a measurement exists (the
 * identify payload) and hand-packed ones elsewhere (the 40-bit contact records,
 * computed once with an independent bit packer and written here as hex).
 */
#include "KebabTouchTcm.h"

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

/* The measured S3908 identify payload: 24 bytes, part number at offset 4. */
static const uint8_t identify_measured[24] = {
	0xa5, 0x03, 0x01, 0x01,
	0x73, 0x33, 0x39, 0x30, 0x38, 0x2d, 0x31, 0x35, 0x2e, 0x30, 0x2e, 0x30,
	0x00, 0x00, 0x00, 0x00,
	0xb9, 0xba, 0x3c, 0x00,
};

/* Contact 3, finger, x 708, y 1440, z 127: index 3, class 1, x 0x2c4, y 0x5a0,
 * z 0x7f.  Every field sits where it does only if index is 4 bits at 0, class 4
 * bits at 4, x 12 bits at 8, y 12 bits at 20 and z 8 bits at 32. */
static const uint8_t finger_a[5] = { 0x13, 0xc4, 0x02, 0x5a, 0x7f };

/* The same contact moved one pixel right: only bit 0 of x changes. */
static const uint8_t finger_a_moved[5] = { 0x13, 0xc5, 0x02, 0x5a, 0x7f };

/* finger_a shifted one bit later in the stream.  A decoder that starts at the
 * wrong bit produces a different contact, which is the point of the test. */
static const uint8_t finger_a_shifted[6] = { 0x26, 0x88, 0x05, 0xb4, 0xfe, 0x00 };

/* Two contacts: slot 0 at (100,200) pressure 40, slot 1 at (540,1200) 200. */
static const uint8_t two_contacts[10] = {
	0x10, 0x64, 0x80, 0x0c, 0x28,
	0x11, 0x1c, 0x02, 0x4b, 0xc8,
};

/* Slot 1 alone, same bytes as the second contact above. */
static const uint8_t contact_b[5] = { 0x11, 0x1c, 0x02, 0x4b, 0xc8 };

/* Index 9, class 6, x 0xabc, y 0x123, z 0x5a: chosen so every field straddles a
 * byte boundary and a swapped pair cannot survive. */
static const uint8_t finger_straddle[5] = { 0x69, 0xbc, 0x3a, 0x12, 0x5a };

/* The pressure program and the vendor program, as arrays rather than as bytes
 * built by the code under test. */
static const uint8_t pressure_program[13] = {
	1, 6, 4, 7, 4, 8, 12, 9, 12, 10, 8, 3, 0,
};
static const uint8_t vendor_program[11] = {
	1, 6, 4, 7, 4, 8, 12, 9, 12, 3, 0,
};

/* Assemble one wire message: marker, code, little-endian length, payload, pad.
 * This is the test's own writer, not the module's command builder. */
static uint32_t
frame(uint8_t *out, uint8_t code, const uint8_t *payload, uint32_t len)
{
	out[0] = KEBAB_TCM_MARKER;
	out[1] = code;
	out[2] = (uint8_t)(len & 0xffu);
	out[3] = (uint8_t)((len >> 8) & 0xffu);
	if (len != 0) {
		memcpy(&out[4], payload, len);
	}
	out[4 + len] = KEBAB_TCM_PADDING;
	return 5u + len;
}

static void
dump(const char *what, const uint8_t *p, uint32_t n)
{
	uint32_t i;

	printf("  %s (%u):", what, n);
	for (i = 0; i < n; i++) {
		printf(" %02x", p[i]);
	}
	printf("\n");
}

int
main(void)
{
	uint8_t wire[512];
	uint8_t program[KEBAB_TCM_PROGRAM_MAX];
	uint8_t payload[KEBAB_TCM_REPORT_CONFIG_MAX];
	kebab_tcm_header header;
	kebab_tcm_report report;
	kebab_tcm_tracker tracker;
	kebab_tcm_event events[2 * KEBAB_TCM_MAX_OBJECTS];
	uint32_t consumed;
	uint32_t n;

	printf("KebabTouchTcm host tests: framing, payloads, program, decode, "
	    "tracking, transform\n");
	printf("sizeof(kebab_tcm_report) %u, max objects %u\n",
	    (unsigned)sizeof(report), KEBAB_TCM_MAX_OBJECTS);

	printf("C1 the command write is {command, length_lo, length_hi, payload}\n");
	{
		static const uint8_t body[3] = { 0x11, 0x22, 0x33 };
		uint8_t out[8];

		n = kebab_tcm_build_command(out, sizeof(out), 0x26, body, 3);
		CHECK(n == 6, "a 3-byte payload makes a 6-byte write");
		CHECK(out[0] == 0x26, "the command byte is first");
		CHECK(out[1] == 3 && out[2] == 0,
		    "the payload length is little endian, low byte first");
		CHECK(out[3] == 0x11 && out[4] == 0x22 && out[5] == 0x33,
		    "the payload follows the length");
		CHECK(kebab_tcm_build_command(out, 5, 0x26, body, 3) == 0,
		    "a buffer that cannot hold it is refused, not truncated");
		CHECK(kebab_tcm_build_command(out, sizeof(out), 0x02, NULL, 0) == 3,
		    "identify is three bytes and needs no payload");
		CHECK(kebab_tcm_build_command(NULL, 8, 0x02, NULL, 0) == 0,
		    "a null buffer is refused");
		CHECK(kebab_tcm_build_command(out, sizeof(out), 0x26, NULL, 3) == 0,
		    "a null payload with a length is refused");
	}

	printf("C2 a message is header + payload + one 0x5a padding byte\n");
	{
		uint8_t msg[8];
		kebab_tcm_status st;

		n = frame(msg, KEBAB_TCM_STATUS_OK, NULL, 0);
		CHECK(n == 5, "a status message with no payload is five bytes");
		st = kebab_tcm_message_parse(msg, n, &header, &consumed);
		CHECK(st == KEBAB_TCM_OK, "it parses");
		CHECK(header.code == KEBAB_TCM_STATUS_OK && header.payload_len == 0,
		    "code 0x01, no payload");
		CHECK(consumed == 5, "consumed includes the padding byte");
		CHECK(kebab_tcm_message_bytes(&header) == 5,
		    "the message size agrees with the frame");
		CHECK(kebab_tcm_expect_status(&header) == KEBAB_TCM_OK,
		    "a status is accepted where a status is expected");
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_IDENTIFY) ==
		    KEBAB_TCM_ERR_CODE, "and is not the identify report");

		n = frame(msg, KEBAB_TCM_REPORT_TOUCH, two_contacts, 10);
		st = kebab_tcm_message_parse(msg, n, &header, &consumed);
		CHECK(st == KEBAB_TCM_OK && header.payload_len == 10,
		    "a ten-byte touch report parses");
		CHECK(consumed == 15, "4 + 10 + 1");
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_TOUCH) ==
		    KEBAB_TCM_OK, "code 0x11 is the touch report");

		msg[14] = 0x00;   /* last byte of the message is the padding */
		CHECK(kebab_tcm_message_parse(msg, 15, &header, &consumed) ==
		    KEBAB_TCM_ERR_PADDING, "a message without 0x5a is rejected");
		msg[14] = KEBAB_TCM_PADDING;

		CHECK(kebab_tcm_message_parse(msg, 12, &header, &consumed) ==
		    KEBAB_TCM_ERR_SHORT, "a message shorter than its length is rejected");
		CHECK(kebab_tcm_message_parse(msg, 3, &header, &consumed) ==
		    KEBAB_TCM_ERR_SHORT, "three bytes cannot hold a header");
		CHECK(kebab_tcm_message_parse(NULL, 15, &header, &consumed) ==
		    KEBAB_TCM_ERR_ARGUMENT, "a null buffer is rejected");
	}

	printf("C3 the measured 23040-byte length is rejected, not trusted\n");
	{
		/* The previous bring-up read a5 01 00 5a as a header: 0x5a00 =
		 * 23040 payload bytes.  Whatever that message really is, it is
		 * not one this driver can read, and the four bytes must survive
		 * for the log. */
		static const uint8_t bad[4] = { 0xa5, 0x01, 0x00, 0x5a };
		uint8_t copy[4];

		memcpy(copy, bad, sizeof(copy));
		CHECK(kebab_tcm_header_parse(bad, &header) == KEBAB_TCM_ERR_LENGTH,
		    "a 0x5a00 length is rejected");
		CHECK(memcmp(copy, bad, 4) == 0, "the raw four bytes are untouched");
		dump("rejected header", bad, 4);

		CHECK(kebab_tcm_header_parse(copy, &header) == KEBAB_TCM_ERR_LENGTH,
		    "and again from a copy");
		copy[0] = 0x00;
		CHECK(kebab_tcm_header_parse(copy, &header) == KEBAB_TCM_ERR_MARKER,
		    "a missing 0xa5 is a different error, so the log can tell them apart");

		/* A stray byte in front of a real header is skipped by a slide. */
		memcpy(wire, "\x00\x5a", 2);
		n = frame(&wire[2], KEBAB_TCM_STATUS_OK, NULL, 0);
		CHECK(kebab_tcm_find_marker(wire, n + 2, 0) == 2,
		    "the marker is found after two stray bytes");
		CHECK(kebab_tcm_find_marker(wire, n + 2, 3) == n + 2,
		    "and searching past it finds nothing");
	}

	printf("C4 the measured identify response decodes to application mode\n");
	{
		kebab_tcm_identify id;
		kebab_tcm_status st;

		n = frame(wire, KEBAB_TCM_REPORT_IDENTIFY, identify_measured,
		    sizeof(identify_measured));
		CHECK(n == 29, "4 header + 24 payload + 1 padding");
		st = kebab_tcm_message_parse(wire, n, &header, &consumed);
		CHECK(st == KEBAB_TCM_OK, "the message parses");
		CHECK(header.code == KEBAB_TCM_REPORT_IDENTIFY &&
		    header.payload_len == 24, "message 0x10 with 24 payload bytes");
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_IDENTIFY) ==
		    KEBAB_TCM_OK, "it is the answer identify waits for");

		st = kebab_tcm_parse_identify(identify_measured,
		    sizeof(identify_measured), &id);
		CHECK(st == KEBAB_TCM_OK, "the payload decodes");
		CHECK(id.mode == KEBAB_TCM_MODE_APPLICATION,
		    "mode 0x01 is application mode");
		CHECK(id.version == 0x01, "version 0x01");
		CHECK(strcmp(id.part, "s3908-15.0.0") == 0,
		    "part number s3908-15.0.0");
		CHECK(id.build_known && id.build == 0x003cbab9u,
		    "build 0x003cbab9 (b9 ba 3c 00 little endian)");
		CHECK(id.prefix_len == 2, "two bytes sit in front of version/mode");
		CHECK(id.prefix[0] == 0xa5 && id.prefix[1] == 0x03,
		    "that prefix is a5 03 and is kept, not interpreted");
		printf("  decoded: part \"%s\" mode %s build 0x%08x prefix %u\n",
		    id.part, id.mode == KEBAB_TCM_MODE_APPLICATION ?
		    "application" : "not application", id.build, id.prefix_len);
	}

	printf("C5 an identify payload with the vendor layout has no prefix\n");
	{
		uint8_t vendor[24];
		kebab_tcm_identify id;

		memset(vendor, 0, sizeof(vendor));
		vendor[0] = 0x01;   /* version */
		vendor[1] = 0x01;   /* mode */
		memcpy(&vendor[2], "s3908-15.0.0", 12);
		vendor[18] = 0xb9;
		vendor[19] = 0xba;
		vendor[20] = 0x3c;
		vendor[21] = 0x00;
		CHECK(kebab_tcm_parse_identify(vendor, sizeof(vendor), &id) ==
		    KEBAB_TCM_OK, "the vendor layout decodes");
		CHECK(id.prefix_len == 0, "nothing in front of version/mode");
		CHECK(id.version == 0x01 && id.mode == 0x01, "version and mode");
		CHECK(strcmp(id.part, "s3908-15.0.0") == 0, "part number");

		/* A part number at offset 0 leaves no room for version/mode. */
		memcpy(wire, "abcd", 4);
		CHECK(kebab_tcm_parse_identify(wire, 4, &id) ==
		    KEBAB_TCM_ERR_LAYOUT, "a payload with no room is rejected");

		/* No printable run at all. */
		wire[0] = 0xa5; wire[1] = 0x03; wire[2] = 0x01; wire[3] = 0x01;
		CHECK(kebab_tcm_parse_identify(wire, 4, &id) ==
		    KEBAB_TCM_ERR_LAYOUT, "a payload with no part number is rejected");

		/* Short part number: decode what is there and no build id. */
		wire[0] = 0x01; wire[1] = 0x01;
		memcpy(&wire[2], "s3908", 5);
		CHECK(kebab_tcm_parse_identify(wire, 7, &id) == KEBAB_TCM_OK,
		    "a part number that ends early still decodes");
		CHECK(strcmp(id.part, "s3908") == 0, "part s3908");
		CHECK(id.build_known == 0, "build id is not invented when absent");
		CHECK(kebab_tcm_parse_identify(NULL, 24, &id) ==
		    KEBAB_TCM_ERR_ARGUMENT, "a null payload is rejected");
	}

	printf("C6 the application info is read at the vendor struct's offsets\n");
	{
		uint8_t info[KEBAB_TCM_APP_INFO_SIZE];
		kebab_tcm_app_info app;
		uint32_t i;

		for (i = 0; i < sizeof(info); i++) {
			info[i] = (uint8_t)i;   /* field k = k | ((k+1) << 8) */
		}
		CHECK(kebab_tcm_parse_app_info(info, sizeof(info), &app) ==
		    KEBAB_TCM_OK, "the 46-byte payload decodes");
		CHECK(app.version == 0x0100, "version at 0");
		CHECK(app.status == 0x0302, "status at 2");
		CHECK(app.static_config_size == 0x0504, "static config size at 4");
		CHECK(app.dynamic_config_size == 0x0706, "dynamic config size at 6");
		CHECK(app.app_config_start_write_block == 0x0908,
		    "app config start block at 8");
		CHECK(app.app_config_size == 0x0b0a, "app config size at 10");
		CHECK(app.max_touch_report_config_size == 0x0d0c,
		    "max touch report config size at 12");
		CHECK(app.max_touch_report_payload_size == 0x0f0e,
		    "max touch report payload size at 14");
		CHECK(app.customer_config_id[0] == 16 &&
		    app.customer_config_id[15] == 31,
		    "customer config id is the 16 bytes at 16");
		CHECK(app.max_x == 0x2120, "max_x at 32");
		CHECK(app.max_y == 0x2322, "max_y at 34");
		CHECK(app.max_objects == 0x2524, "max_objects at 36");
		CHECK(app.num_of_buttons == 0x2726, "num_of_buttons at 38");
		CHECK(app.num_of_image_rows == 0x2928, "num_of_image_rows at 40");
		CHECK(app.num_of_image_cols == 0x2b2a, "num_of_image_cols at 42");
		CHECK(app.has_hybrid_data == 0x2d2c, "has_hybrid_data at 44");
		CHECK(kebab_tcm_parse_app_info(info, 45, &app) ==
		    KEBAB_TCM_ERR_LAYOUT, "45 bytes is one short and is rejected");
		CHECK(kebab_tcm_parse_app_info(info, 47, &app) == KEBAB_TCM_OK,
		    "47 bytes is enough (the struct is a prefix)");
	}

	printf("C7 the report program is the vendor sequence, plus z\n");
	{
		uint32_t plen;

		plen = kebab_tcm_program_build(program, sizeof(program), 1);
		CHECK(plen == 13, "the pressure program is 13 bytes");
		CHECK(memcmp(program, pressure_program, sizeof(pressure_program)) == 0,
		    "bytes match foreach/index/class/x/y/z/end");
		CHECK(kebab_tcm_program_object_bits(program, plen) == 40,
		    "one contact occupies 40 bits, five bytes");
		dump("pressure program", program, plen);

		plen = kebab_tcm_program_build(program, sizeof(program), 0);
		CHECK(plen == 11, "the vendor fallback program is 11 bytes");
		CHECK(memcmp(program, vendor_program, sizeof(vendor_program)) == 0,
		    "bytes match the vendor default (no z)");
		CHECK(kebab_tcm_program_object_bits(program, plen) == 32,
		    "one contact occupies 32 bits, four bytes");

		CHECK(kebab_tcm_program_build(program, 12, 1) == 0,
		    "a buffer one byte short is refused");
		CHECK(kebab_tcm_program_build(NULL, 16, 1) == 0,
		    "a null buffer is refused");

		/* A program this decoder does not implement must be refused, not
		 * walked with guessed widths. */
		memcpy(wire, pressure_program, sizeof(pressure_program));
		wire[3] = 0x7f;
		CHECK(kebab_tcm_program_object_bits(wire, 13) == 0,
		    "an unknown field code makes the program unusable");
		memcpy(wire, pressure_program, 12);
		CHECK(kebab_tcm_program_object_bits(wire, 6) == 0,
		    "a program cut before FOREACH_END is unusable");
	}

	printf("C8 the report config payload is zero padded to the firmware size\n");
	{
		uint32_t total;
		uint32_t i;
		int all_zero = 1;

		total = kebab_tcm_report_payload_build(payload, sizeof(payload),
		    pressure_program, sizeof(pressure_program), 128);
		CHECK(total == 128, "a 128-byte firmware buffer is filled");
		CHECK(memcmp(payload, pressure_program, 13) == 0,
		    "the program is at the front");
		for (i = 13; i < total; i++) {
			if (payload[i] != 0) {
				all_zero = 0;
			}
		}
		CHECK(all_zero, "the rest is zero, which the program parser reads as END");

		total = kebab_tcm_report_payload_build(payload, sizeof(payload),
		    pressure_program, sizeof(pressure_program), 0);
		CHECK(total == 13, "no firmware size sends the program alone");

		CHECK(kebab_tcm_report_payload_build(payload, sizeof(payload),
		    pressure_program, sizeof(pressure_program), 12) == 0,
		    "a firmware buffer smaller than the program is refused");
		CHECK(kebab_tcm_report_payload_build(payload, 100,
		    pressure_program, sizeof(pressure_program), 128) == 0,
		    "a caller buffer that cannot hold the padded payload is refused");
		CHECK(kebab_tcm_report_payload_build(payload, sizeof(payload),
		    pressure_program, sizeof(pressure_program), 4096) == 13,
		    "a firmware size this driver has no buffer for falls back to the "
		    "unpadded program");
	}

	printf("C9 decoding one contact: every field, with the bit reader\n");
	{
		uint32_t plen = kebab_tcm_program_build(program, sizeof(program), 1);
		kebab_tcm_status st;

		CHECK(kebab_tcm_bits(finger_a, 0, 4) == 3, "bits 0..3 are the index 3");
		CHECK(kebab_tcm_bits(finger_a, 4, 4) == 1,
		    "bits 4..7 are the classification 1");
		CHECK(kebab_tcm_bits(finger_a, 8, 12) == 0x2c4,
		    "bits 8..19 are x 0x2c4 (708)");
		CHECK(kebab_tcm_bits(finger_a, 20, 12) == 0x5a0,
		    "bits 20..31 are y 0x5a0 (1440), across the nibble boundary");
		CHECK(kebab_tcm_bits(finger_a, 32, 8) == 0x7f,
		    "bits 32..39 are z 0x7f (127)");
		CHECK(kebab_tcm_bits(finger_a, 9, 12) != 0x2c4,
		    "one bit late is a different x: 0x162, not 0x2c4");
		CHECK(kebab_tcm_bits(finger_a, 9, 12) == 0x162,
		    "and it is exactly x >> 1 with y's first bit on top");

		st = kebab_tcm_decode_report(program, plen, finger_a,
		    sizeof(finger_a), KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(st == KEBAB_TCM_OK, "the report decodes");
		CHECK(report.objects == 1, "one contact");
		CHECK(report.payload_bits == 40 && report.bits == 40,
		    "40 payload bits, all used");
		CHECK(report.truncated_bits == 0 && !report.truncated_nonzero,
		    "no tail");
		CHECK(report.object[0].index == 3, "index 3");
		CHECK(report.object[0].classification == 1, "classification 1");
		CHECK(report.object[0].x == 708, "x 708");
		CHECK(report.object[0].y == 1440, "y 1440");
		CHECK(report.object[0].z == 127, "z 127");

		st = kebab_tcm_decode_report(program, plen, finger_straddle,
		    sizeof(finger_straddle), KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(st == KEBAB_TCM_OK, "the straddling contact decodes");
		CHECK(report.object[0].index == 9, "index 9 from 0x69's low nibble");
		CHECK(report.object[0].classification == 6,
		    "classification 6 from 0x69's high nibble");
		CHECK(report.object[0].x == 0xabc, "x 0xabc (2748)");
		CHECK(report.object[0].y == 0x123, "y 0x123 (291)");
		CHECK(report.object[0].z == 0x5a, "z 0x5a (90)");
	}

	printf("C10 down, move and up are one state machine over three reports\n");
	{
		uint32_t plen = kebab_tcm_program_build(program, sizeof(program), 1);
		uint32_t nev;

		memset(&tracker, 0, sizeof(tracker));
		kebab_tcm_decode_report(program, plen, finger_a, sizeof(finger_a),
		    KEBAB_TCM_MAX_OBJECTS, &report);
		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 1, "one event for the first report");
		CHECK(events[0].phase == KEBAB_TCM_PHASE_DOWN, "it is a down");
		CHECK(events[0].slot == 3, "on slot 3");
		CHECK(events[0].raw_x == 708 && events[0].raw_y == 1440,
		    "at 708,1440");
		CHECK(events[0].pressure == 127, "pressure 127");
		CHECK(tracker.active == (1u << 3), "slot 3 is now active");

		kebab_tcm_decode_report(program, plen, finger_a_moved,
		    sizeof(finger_a_moved), KEBAB_TCM_MAX_OBJECTS, &report);
		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 1, "one event for the second report");
		CHECK(events[0].phase == KEBAB_TCM_PHASE_MOVE, "it is a move");
		CHECK(events[0].slot == 3, "on the same slot");
		CHECK(events[0].raw_x == 709, "x moved to 709");
		CHECK(events[0].raw_y == 1440, "y did not move");
		CHECK(tracker.active == (1u << 3), "slot 3 is still active");

		/* All fingers up is an empty payload: no bits, no contacts. */
		kebab_tcm_decode_report(program, plen, finger_a, 0,
		    KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(report.objects == 0 && report.payload_bits == 0,
		    "an empty report decodes to no contacts");
		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 1, "one event when the last contact disappears");
		CHECK(events[0].phase == KEBAB_TCM_PHASE_UP, "it is an up");
		CHECK(events[0].slot == 3, "on slot 3");
		CHECK(events[0].raw_x == 709 && events[0].raw_y == 1440,
		    "reported at the last coordinates it had");
		CHECK(events[0].pressure == 0, "with no pressure");
		CHECK(tracker.active == 0, "nothing is active any more");

		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 0, "a second empty report emits nothing");
	}

	printf("C11 two contacts: two downs, then a move and an up\n");
	{
		uint32_t plen = kebab_tcm_program_build(program, sizeof(program), 1);
		uint32_t nev;

		memset(&tracker, 0, sizeof(tracker));
		CHECK(kebab_tcm_decode_report(program, plen, two_contacts,
		    sizeof(two_contacts), KEBAB_TCM_MAX_OBJECTS, &report) ==
		    KEBAB_TCM_OK, "the two-contact report decodes");
		CHECK(report.objects == 2, "two contacts");
		CHECK(report.object[0].index == 0 && report.object[0].x == 100 &&
		    report.object[0].y == 200 && report.object[0].z == 40,
		    "contact 0 is (100,200) z 40");
		CHECK(report.object[1].index == 1 && report.object[1].x == 540 &&
		    report.object[1].y == 1200 && report.object[1].z == 200,
		    "contact 1 is (540,1200) z 200");
		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 2, "two events");
		CHECK(events[0].phase == KEBAB_TCM_PHASE_DOWN &&
		    events[0].slot == 0, "the first is a down on slot 0");
		CHECK(events[1].phase == KEBAB_TCM_PHASE_DOWN &&
		    events[1].slot == 1, "the second is a down on slot 1");
		CHECK(tracker.active == 0x3u, "both slots are active");

		/* Only contact 1 in the next report. */
		kebab_tcm_decode_report(program, plen, contact_b,
		    sizeof(contact_b), KEBAB_TCM_MAX_OBJECTS, &report);
		nev = kebab_tcm_track(&tracker, &report, events,
		    2 * KEBAB_TCM_MAX_OBJECTS);
		CHECK(nev == 2, "a move and an up");
		CHECK(events[0].phase == KEBAB_TCM_PHASE_MOVE &&
		    events[0].slot == 1, "the move for slot 1 comes first");
		CHECK(events[1].phase == KEBAB_TCM_PHASE_UP &&
		    events[1].slot == 0, "the up for slot 0 follows");
		CHECK(events[1].raw_x == 100 && events[1].raw_y == 200,
		    "the up uses slot 0's last coordinates");
		CHECK(tracker.active == 0x2u, "only slot 1 remains active");
	}

	printf("C12 a truncated report is not decoded as a contact\n");
	{
		uint32_t plen = kebab_tcm_program_build(program, sizeof(program), 1);
		kebab_tcm_status st;

		/* 4 bytes is 32 bits: less than the 40 a contact needs. */
		st = kebab_tcm_decode_report(program, plen, finger_a, 4,
		    KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(st == KEBAB_TCM_OK, "the decoder returns instead of reading past the buffer");
		CHECK(report.objects == 0, "no contact is invented from 32 bits");
		CHECK(report.truncated_bits == 32, "the 32 bits are counted as a tail");
		CHECK(report.truncated_nonzero == 1, "and the tail is non-zero");

		/* One full contact and 4 bytes of the next. */
		{
			uint8_t mixed[9];

			memcpy(mixed, two_contacts, 9);
			st = kebab_tcm_decode_report(program, plen, mixed,
			    sizeof(mixed), KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(st == KEBAB_TCM_OK, "the first contact survives");
			CHECK(report.objects == 1 && report.object[0].index == 0,
			    "exactly one contact is decoded");
			CHECK(report.truncated_bits == 32 && report.truncated_nonzero,
			    "the second contact is a non-zero 32-bit tail");
		}

		/* A six-byte report: one contact and one zero byte of padding. */
		{
			uint8_t padded[6];

			memcpy(padded, finger_a, sizeof(finger_a));
			padded[5] = 0x00;
			st = kebab_tcm_decode_report(program, plen, padded,
			    sizeof(padded), KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(st == KEBAB_TCM_OK && report.objects == 1,
			    "zero padding does not hide the contact");
			CHECK(report.truncated_bits == 8 &&
			    !report.truncated_nonzero, "a zero tail is padding");
		}

		/* The message layer has its own truncation check. */
		n = frame(wire, KEBAB_TCM_REPORT_TOUCH, two_contacts, 10);
		CHECK(kebab_tcm_message_parse(wire, n - 3, &header, &consumed) ==
		    KEBAB_TCM_ERR_SHORT, "a frame cut inside its payload is short");
	}

	printf("C13 a response that echoes the command is not an answer\n");
	{
		/* A header whose code is the identify command byte, not 0x10. */
		n = frame(wire, KEBAB_TCM_CMD_IDENTIFY, NULL, 0);
		CHECK(kebab_tcm_message_parse(wire, n, &header, &consumed) ==
		    KEBAB_TCM_OK, "the frame itself parses");
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_IDENTIFY) ==
		    KEBAB_TCM_ERR_CODE, "code 0x02 is not the identify report");
		CHECK(kebab_tcm_expect_status(&header) == KEBAB_TCM_OK,
		    "but 0x02 is in the status range, so identify must ask for 0x10");

		/* A command byte above the status range must not read as OK. */
		n = frame(wire, KEBAB_TCM_CMD_SET_REPORT_CONFIG, NULL, 0);
		kebab_tcm_message_parse(wire, n, &header, &consumed);
		CHECK(header.code == 0x26, "the header keeps the echoed byte");
		CHECK(kebab_tcm_expect_status(&header) == KEBAB_TCM_ERR_CODE,
		    "0x26 is neither a status nor a report and is rejected");

		/* A touch report arriving where a command answer is expected. */
		n = frame(wire, KEBAB_TCM_REPORT_TOUCH, NULL, 0);
		kebab_tcm_message_parse(wire, n, &header, &consumed);
		CHECK(kebab_tcm_expect_status(&header) == KEBAB_TCM_ERR_CODE,
		    "a touch report in the status slot is rejected");
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_TOUCH) ==
		    KEBAB_TCM_OK, "and is the right answer for the poll thread");

		/* A status arriving where a report is expected. */
		n = frame(wire, KEBAB_TCM_STATUS_OK, NULL, 0);
		kebab_tcm_message_parse(wire, n, &header, &consumed);
		CHECK(kebab_tcm_expect_report(&header, KEBAB_TCM_REPORT_TOUCH) ==
		    KEBAB_TCM_ERR_CODE, "a status is not a touch report");
	}

	printf("C14 the report program decides the layout\n");
	{
		uint8_t width_program[13];
		kebab_tcm_status st;
		uint16_t x11;
		uint16_t y13;

		/* The same report with x asked for as 11 bits. */
		memcpy(width_program, pressure_program, sizeof(pressure_program));
		width_program[6] = 11;
		st = kebab_tcm_decode_report(width_program, sizeof(width_program),
		    finger_a, sizeof(finger_a), KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(st == KEBAB_TCM_OK, "the 11-bit-x program decodes");
		x11 = report.object[0].x;
		CHECK(x11 == 708,
		    "an 11-bit x still holds 708: the value fits, the layout moved");
		CHECK(report.object[0].y == 2880,
		    "and y is read one bit early: 1440 << 1, so the width byte is used");

		/* y asked for as 13 bits picks up z's first bit.  A 13-bit y makes
		 * the contact 41 bits, so the payload needs a sixth byte. */
		{
			uint8_t padded[6];

			memcpy(padded, finger_a, sizeof(finger_a));
			padded[5] = 0x00;
			memcpy(width_program, pressure_program,
			    sizeof(pressure_program));
			width_program[8] = 13;
			st = kebab_tcm_decode_report(width_program,
			    sizeof(width_program), padded, sizeof(padded),
			    KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(st == KEBAB_TCM_OK, "the 13-bit-y program decodes");
			y13 = report.object[0].y;
			CHECK(y13 == 1440 + 4096,
			    "y is 1440 plus z's low bit at bit 12: the width byte is read");
			CHECK(y13 != 1440, "so a stale width cannot pass silently");
		}

		/* One bit of shift in the stream. */
		st = kebab_tcm_decode_report(program, 13, finger_a_shifted,
		    sizeof(finger_a_shifted), KEBAB_TCM_MAX_OBJECTS, &report);
		CHECK(st == KEBAB_TCM_OK, "the shifted stream still decodes");
		CHECK(report.object[0].index != 3 || report.object[0].x != 708 ||
		    report.object[0].y != 1440,
		    "but not to the same contact: a one-bit shift cannot hide");

		/* The vendor program has no z, so z stays zero. */
		CHECK(kebab_tcm_decode_report(vendor_program,
		    sizeof(vendor_program), finger_straddle, 4,
		    KEBAB_TCM_MAX_OBJECTS, &report) == KEBAB_TCM_OK,
		    "the 32-bit program decodes its 4-byte contact");
		CHECK(report.object[0].x == 0xabc && report.object[0].y == 0x123,
		    "x and y are where the 32-bit program puts them");
		CHECK(report.object[0].z == 0, "z is zero when the program has no z");
	}

	printf("C15 the tracker ignores what is not a finger\n");
	{
		uint32_t plen = kebab_tcm_program_build(program, sizeof(program), 1);
		uint32_t nev;

		memset(&tracker, 0, sizeof(tracker));
		/* Gloved finger is a contact. */
		{
			uint8_t gloved[5] = { 0x21, 0x64, 0x80, 0x0c, 0x28 };

			kebab_tcm_decode_report(program, plen, gloved,
			    sizeof(gloved), KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(report.object[0].classification ==
			    KEBAB_TCM_CLASS_GLOVED_FINGER, "class 2 is a gloved finger");
			nev = kebab_tcm_track(&tracker, &report, events,
			    2 * KEBAB_TCM_MAX_OBJECTS);
			CHECK(nev == 1 && events[0].phase == KEBAB_TCM_PHASE_DOWN &&
			    events[0].slot == 1, "it presses slot 1");
		}

		/* A lift object is not a contact: the slot disappears and the up
		 * carries the last coordinates. */
		{
			uint8_t lift[5] = { 0x01, 0x00, 0x00, 0x00, 0x00 };

			kebab_tcm_decode_report(program, plen, lift,
			    sizeof(lift), KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(report.object[0].classification ==
			    KEBAB_TCM_CLASS_LIFT, "class 0 is a lift");
			nev = kebab_tcm_track(&tracker, &report, events,
			    2 * KEBAB_TCM_MAX_OBJECTS);
			CHECK(nev == 1 && events[0].phase == KEBAB_TCM_PHASE_UP,
			    "the lift turns into an up, not a down");
			CHECK(events[0].slot == 1, "on the slot that was held");
			CHECK(events[0].raw_x == 100 && events[0].raw_y == 200,
			    "at the last coordinates");
		}

		/* An index outside the ten slots is ignored. */
		{
			uint8_t beyond[5] = { 0x1c, 0x64, 0x80, 0x0c, 0x28 };

			memset(&tracker, 0, sizeof(tracker));
			kebab_tcm_decode_report(program, plen, beyond,
			    sizeof(beyond), KEBAB_TCM_MAX_OBJECTS, &report);
			CHECK(report.object[0].index == 12, "index 12 decodes");
			nev = kebab_tcm_track(&tracker, &report, events,
			    2 * KEBAB_TCM_MAX_OBJECTS);
			CHECK(nev == 0 && tracker.active == 0,
			    "but no event and no slot state");
		}

		/* A caller that offers room for one event still gets its state
		 * updated. */
		{
			memset(&tracker, 0, sizeof(tracker));
			kebab_tcm_decode_report(program, plen, two_contacts,
			    sizeof(two_contacts), KEBAB_TCM_MAX_OBJECTS, &report);
			nev = kebab_tcm_track(&tracker, &report, events, 1);
			CHECK(nev == 1, "only the event that fit is returned");
			CHECK(tracker.active == 0x3u,
			    "but both slots are marked active");
		}

		CHECK(kebab_tcm_track(NULL, &report, events, 4) == 0,
		    "a null tracker is refused");
	}

	printf("C16 the transform keeps the three spaces apart\n");
	{
		kebab_tcm_point p;

		p = kebab_tcm_transform(0, 0, 1079, 2399);
		CHECK(p.panel_x == 0 && p.panel_y == 0, "raw 0,0 is panel 0,0");
		CHECK(p.logical_x == 22 && p.logical_y == 18,
		    "and logical 22,18, the presentation origin");
		CHECK(!p.clamped, "nothing is clamped");

		p = kebab_tcm_transform(1079, 2399, 1079, 2399);
		CHECK(p.panel_x == 1079 && p.panel_y == 2399,
		    "raw 1079,2399 is panel 1079,2399");
		CHECK(p.logical_x == 1101 && p.logical_y == 2417,
		    "and logical 1101,2417");
		CHECK(!p.clamped, "nothing is clamped");

		p = kebab_tcm_transform(540, 1200, 1079, 2399);
		CHECK(p.panel_x == 540 && p.panel_y == 1200, "the middle stays put");
		CHECK(p.logical_x == 562 && p.logical_y == 1218,
		    "and is offset by 22,18 in logical space");

		p = kebab_tcm_transform(0x0fff, 0x0fff, 1079, 2399);
		CHECK(p.panel_x == 1079 && p.panel_y == 2399,
		    "0xfff from a lifting finger clamps to the panel edge");
		CHECK(p.clamped, "and is flagged");

		p = kebab_tcm_transform(1079, 2399, 0, 0);
		CHECK(p.panel_x == 1079 && p.panel_y == 2399,
		    "no app info falls back to the device tree's 1079,2399");

		p = kebab_tcm_transform(1000, 1000, 2000, 2000);
		CHECK(p.panel_x == 540 && p.panel_y == 1200,
		    "another advertised maximum scales instead of clipping");

		p = kebab_tcm_transform(500, 500, 0xffff, 0xffff);
		CHECK(p.panel_x == (int32_t)(500 * 1079 / 1079) &&
		    p.panel_y == (int32_t)(500 * 2399 / 2399),
		    "an implausible maximum (16 bits, not 12) is not used");
	}

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
