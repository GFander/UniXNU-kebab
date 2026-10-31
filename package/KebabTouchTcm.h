/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The Synaptics TCM protocol the S3908 on SE13 speaks, and the touch report
 * decoder on top of it.
 *
 * The module is free of I2C, of MMIO and of kernel dependencies.  It owns three
 * things the driver used to spell out inline:
 *
 *   - the message framing: marker, code, 16-bit length, payload, padding;
 *   - the two application-mode commands whose payloads are laid out by the
 *     vendor headers (identify, application info) and the report program whose
 *     layout the driver itself chooses (SET_TOUCH_REPORT_CONFIG);
 *   - the bitfield walk that turns a report into contacts, the contact state
 *     machine that turns a stream of reports into down/move/up, and the
 *     sensor -> panel -> logical transform.
 *
 * The bitfield walk is driven by the report program, not by a fixed 5-byte
 * stride: the same bytes the driver sends to the chip are the bytes the decoder
 * reads, so a program the firmware accepted is a layout the decoder knows.
 *
 * Every length that comes off the wire is checked against the buffer before it
 * is used.  A message whose declared length is implausible is rejected with the
 * four header bytes intact so the caller can print them; nothing is decoded
 * from a message that did not pass the framing checks.
 */
#ifndef KEBAB_TOUCH_TCM_H
#define KEBAB_TOUCH_TCM_H

#ifdef KEBAB_TOUCH_KERNEL
#include <sys/types.h>
#include <libkern/libkern.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

/* ------------------------------------------------------------ framing */

#define KEBAB_TCM_MARKER       0xa5u
#define KEBAB_TCM_PADDING      0x5au
#define KEBAB_TCM_HEADER_SIZE  4u

/*
 * The largest payload this decoder will look at.  The touch report is the only
 * message that can be large: ten objects of five bytes is 50 bytes, and the
 * application info is 46.  A header that claims more than this is not a header
 * this driver can trust, which is exactly the check the measured 0x5a00 needed.
 */
#define KEBAB_TCM_MAX_PAYLOAD  128u

/* Command set (enum command in syna_tcm_core.h). */
#define KEBAB_TCM_CMD_IDENTIFY          0x02u
#define KEBAB_TCM_CMD_GET_BOOT_INFO     0x10u
#define KEBAB_TCM_CMD_GET_APP_INFO      0x20u
#define KEBAB_TCM_CMD_SET_REPORT_CONFIG 0x26u

/* Report ids and status codes (enum report_type, enum status_code). */
#define KEBAB_TCM_REPORT_IDENTIFY 0x10u
#define KEBAB_TCM_REPORT_TOUCH    0x11u
#define KEBAB_TCM_REPORT_STATUS   0x1bu
#define KEBAB_TCM_STATUS_OK       0x01u

/* Boot modes (enum boot_mode). */
#define KEBAB_TCM_MODE_APPLICATION      0x01u
#define KEBAB_TCM_MODE_HOST_DOWNLOAD    0x02u
#define KEBAB_TCM_MODE_BOOTLOADER       0x0bu
#define KEBAB_TCM_MODE_TDDI_BOOTLOADER  0x0cu
#define KEBAB_TCM_MODE_PRODUCTION_TEST  0x0eu

/* Touch report program codes (enum touch_report_code). */
#define KEBAB_TCM_TOUCH_END                    0u
#define KEBAB_TCM_TOUCH_FOREACH_ACTIVE_OBJECT  1u
#define KEBAB_TCM_TOUCH_FOREACH_OBJECT         2u
#define KEBAB_TCM_TOUCH_FOREACH_END            3u
#define KEBAB_TCM_TOUCH_PAD_TO_NEXT_BYTE       4u
#define KEBAB_TCM_TOUCH_TIMESTAMP              5u
#define KEBAB_TCM_TOUCH_OBJECT_N_INDEX         6u
#define KEBAB_TCM_TOUCH_OBJECT_N_CLASSIFICATION 7u
#define KEBAB_TCM_TOUCH_OBJECT_N_X_POSITION    8u
#define KEBAB_TCM_TOUCH_OBJECT_N_Y_POSITION    9u
#define KEBAB_TCM_TOUCH_OBJECT_N_Z             10u

/* Object classification (enum touch_status). */
#define KEBAB_TCM_CLASS_LIFT         0u
#define KEBAB_TCM_CLASS_FINGER       1u
#define KEBAB_TCM_CLASS_GLOVED_FINGER 2u

/* ------------------------------------------------------------- geometry */

#define KEBAB_TCM_PANEL_W          1080
#define KEBAB_TCM_PANEL_H          2400
#define KEBAB_TCM_LOGICAL_W        1125
#define KEBAB_TCM_LOGICAL_H        2436
#define KEBAB_TCM_LOGICAL_ORIGIN_X 22   /* (1125 - 1080) / 2, truncated */
#define KEBAB_TCM_LOGICAL_ORIGIN_Y 18   /* (2436 - 2400) / 2 */

/* The two fallbacks the device tree fixes when the app info has no answer. */
#define KEBAB_TCM_SENSOR_MAX_X     1079
#define KEBAB_TCM_SENSOR_MAX_Y     2399
#define KEBAB_TCM_SENSOR_LIMIT     0x0fffu  /* the report field is 12 bits */

/* The device tree's touchpanel,max-num-support. */
#define KEBAB_TCM_MAX_OBJECTS      10u

/* --------------------------------------------------------------- types */

typedef enum {
	KEBAB_TCM_OK = 0,
	KEBAB_TCM_ERR_SHORT,    /* fewer bytes than the header claims */
	KEBAB_TCM_ERR_MARKER,   /* no 0xa5 where a message should start */
	KEBAB_TCM_ERR_LENGTH,   /* declared payload length is not plausible */
	KEBAB_TCM_ERR_PADDING,  /* trailing byte is not 0x5a */
	KEBAB_TCM_ERR_CODE,     /* message code is not the one expected */
	KEBAB_TCM_ERR_LAYOUT,   /* payload too short, or a program code not known */
	KEBAB_TCM_ERR_PROGRAM,  /* report program does not fit the buffer */
	KEBAB_TCM_ERR_ARGUMENT, /* caller handed in a null or a bad size */
} kebab_tcm_status;

const char *kebab_tcm_status_name(kebab_tcm_status status);

typedef struct {
	uint8_t  code;
	uint16_t payload_len;
} kebab_tcm_header;

/* The vendor struct syna_tcm_identification, plus what the firmware prepends.
 *
 * The measured S3908 payload is 24 bytes with the part number at offset 4, so
 * version and mode sit at 2 and 3 and two bytes are left over in front of them.
 * The two bytes are kept and printed rather than interpreted: on a payload that
 * matches the vendor struct they are simply the version and the mode, and on
 * this firmware they are not. */
typedef struct {
	uint8_t  version;
	uint8_t  mode;
	char     part[17];      /* NUL terminated, always */
	uint32_t build;         /* little endian packrat number, when present */
	int      build_known;
	uint32_t prefix_len;    /* bytes in front of the version/mode pair */
	uint8_t  prefix[8];     /* first eight of them, for the log */
} kebab_tcm_identify;

/* The vendor struct syna_tcm_app_info: 46 bytes, every field a 16-bit
 * little-endian value.  The two report sizes are what the report program has to
 * fit; max_x/max_y are the sensor geometry the transform uses. */
#define KEBAB_TCM_APP_INFO_SIZE 46u
typedef struct {
	uint16_t version;
	uint16_t status;
	uint16_t static_config_size;
	uint16_t dynamic_config_size;
	uint16_t app_config_start_write_block;
	uint16_t app_config_size;
	uint16_t max_touch_report_config_size;
	uint16_t max_touch_report_payload_size;
	uint8_t  customer_config_id[16];
	uint16_t max_x;
	uint16_t max_y;
	uint16_t max_objects;
	uint16_t num_of_buttons;
	uint16_t num_of_image_rows;
	uint16_t num_of_image_cols;
	uint16_t has_hybrid_data;
} kebab_tcm_app_info;

/* One contact as the report program describes it. */
typedef struct {
	uint8_t  index;
	uint8_t  classification;
	uint16_t x;
	uint16_t y;
	uint8_t  z;             /* pressure proxy: 0..255, 0 when not reported */
} kebab_tcm_object;

typedef struct {
	uint32_t objects;
	uint32_t bits;             /* payload bits consumed by whole contacts */
	uint32_t payload_bits;
	uint32_t truncated_bits;   /* tail that could not form another contact */
	int      truncated_nonzero;
	kebab_tcm_object object[KEBAB_TCM_MAX_OBJECTS];
} kebab_tcm_report;

typedef enum {
	KEBAB_TCM_PHASE_DOWN = 1,
	KEBAB_TCM_PHASE_MOVE = 2,
	KEBAB_TCM_PHASE_UP   = 3,
} kebab_tcm_phase;

typedef struct {
	uint8_t  phase;         /* kebab_tcm_phase */
	uint8_t  slot;
	uint16_t raw_x;
	uint16_t raw_y;
	uint8_t  pressure;
} kebab_tcm_event;

typedef struct {
	uint16_t active;        /* one bit per contact index, previous report */
	uint16_t last_x[KEBAB_TCM_MAX_OBJECTS];
	uint16_t last_y[KEBAB_TCM_MAX_OBJECTS];
} kebab_tcm_tracker;

typedef struct {
	int32_t panel_x;
	int32_t panel_y;
	int32_t logical_x;
	int32_t logical_y;
	int     clamped;
} kebab_tcm_point;

/* ------------------------------------------------------------- helpers */

static inline uint32_t
kebab_tcm_le16(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t
kebab_tcm_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------- framing */

/*
 * Validate four bytes as a message header.  A wrong marker and a length that no
 * message this driver can read would have are both rejected here, before
 * anything is read for them.
 */
kebab_tcm_status kebab_tcm_header_parse(
    const uint8_t raw[KEBAB_TCM_HEADER_SIZE], kebab_tcm_header *out);

/* Header, payload and padding: 4 + payload_len + 1. */
uint32_t kebab_tcm_message_bytes(const kebab_tcm_header *header);

/*
 * Parse one complete message out of a buffer.  buf[0] must be the marker.  On
 * success *consumed is the whole message including the padding byte, so the
 * caller can advance.  A trailing byte that is not 0x5a is an error: a stream
 * without it is one byte out of phase and nothing after it can be trusted.
 */
kebab_tcm_status kebab_tcm_message_parse(const uint8_t *buf, uint32_t len,
    kebab_tcm_header *header, uint32_t *consumed);

/* Offset of the next 0xa5 at or after from, or len when there is none. */
uint32_t kebab_tcm_find_marker(const uint8_t *buf, uint32_t len, uint32_t from);

/*
 * Build the bytes of one command: {command, length_lo, length_hi, payload}.
 * Returns the byte count, or 0 when it does not fit.  This is the write side of
 * the framing, and the only place the little-endian length is assembled.
 */
uint32_t kebab_tcm_build_command(uint8_t *out, uint32_t cap, uint8_t command,
    const uint8_t *payload, uint32_t len);

/* Is this the answer the caller is waiting for?  Reports arrive with a code
 * from 0x10 up; command answers carry a status below 0x10. */
kebab_tcm_status kebab_tcm_expect_report(const kebab_tcm_header *header,
    uint8_t id);
kebab_tcm_status kebab_tcm_expect_status(const kebab_tcm_header *header);

/* ------------------------------------------------------- payload layouts */

/*
 * Decode the identify payload.  The part number is the only field whose offset
 * is self-evident (printable ASCII), so the decoder anchors on it and takes the
 * version and mode as the two bytes in front, exactly as the vendor struct
 * orders them.  Anything before those two is kept as an uninterpreted prefix.
 */
kebab_tcm_status kebab_tcm_parse_identify(const uint8_t *payload,
    uint32_t len, kebab_tcm_identify *out);

/* Decode the 46-byte application info at the vendor struct's offsets. */
kebab_tcm_status kebab_tcm_parse_app_info(const uint8_t *payload, uint32_t len,
    kebab_tcm_app_info *out);

/* ------------------------------------------------------- report program */

/*
 * The program this driver asks the firmware for.  With pressure set the layout
 * is, per active object:
 *
 *   index 4, classification 4, x 12, y 12, z 8   -> 40 bits, five bytes
 *
 * and without it the vendor's own default, four bits less z:
 *
 *   index 4, classification 4, x 12, y 12        -> 32 bits, four bytes
 *
 * Returns the program length, or 0 when cap is too small.
 */
#define KEBAB_TCM_PROGRAM_MAX 32u
uint32_t kebab_tcm_program_build(uint8_t *out, uint32_t cap, int pressure);

/* Bits one object occupies in this program, or 0 when it is not a program this
 * decoder can walk. */
uint32_t kebab_tcm_program_object_bits(const uint8_t *program, uint32_t len);

/*
 * The SET_TOUCH_REPORT_CONFIG payload.  The vendor driver sends
 * max_touch_report_config_size bytes, not the program length: the buffer is
 * zero padded and the firmware's program parser stops at TOUCH_END.  When the
 * application info did not provide a usable size, firmware_max is 0 and the
 * program alone is returned.  Returns the byte count, or 0 when it does not fit.
 */
#define KEBAB_TCM_REPORT_CONFIG_MAX 128u
uint32_t kebab_tcm_report_payload_build(uint8_t *out, uint32_t cap,
    const uint8_t *program, uint32_t program_len, uint32_t firmware_max);

/* ------------------------------------------------------------- decoding */

/*
 * LSB-first bit reader, matching touch_get_report_data(): inside a byte the
 * first field bit is bit 0, and the first chunk of a multi-byte field is its
 * least significant byte.  The caller must keep offset + bits inside the
 * buffer; the decoder below does.
 */
uint32_t kebab_tcm_bits(const uint8_t *data, uint32_t offset, uint32_t bits);

/*
 * Walk the report program over one payload.  Contacts are emitted until the
 * payload runs out, at most max_objects of them.  A tail too short to hold
 * another contact is reported in truncated_bits instead of being decoded: a
 * report cut mid-contact must not turn into a contact at the wrong offset, and
 * a report that is all fingers up (an empty payload) must stay distinguishable
 * from a truncated one.
 */
kebab_tcm_status kebab_tcm_decode_report(const uint8_t *program,
    uint32_t program_len, const uint8_t *payload, uint32_t payload_len,
    uint32_t max_objects, kebab_tcm_report *out);

/* ------------------------------------------------------------- tracking */

/*
 * Compare one decoded report with the previous one.  A contact that was not
 * active is a down, one that was is a move, and a slot that was active and is
 * missing from this report is an up, reported at the last coordinates it had.
 * The tracker is updated whether or not the caller's event array had room.
 */
uint32_t kebab_tcm_track(kebab_tcm_tracker *tracker,
    const kebab_tcm_report *report, kebab_tcm_event *events,
    uint32_t max_events);

/* Sensor -> panel -> logical, with the panel-edge clamp.  max_x/max_y are the
 * largest sensor values the chip advertises; an implausible one falls back to
 * the device tree's 1079/2399 rather than dividing by it. */
kebab_tcm_point kebab_tcm_transform(uint32_t raw_x, uint32_t raw_y,
    uint32_t max_x, uint32_t max_y);

#endif /* KEBAB_TOUCH_TCM_H */
