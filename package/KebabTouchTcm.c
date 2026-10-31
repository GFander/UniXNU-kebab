/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The TCM framing, payload layouts, report program and report decoder.
 *
 * The protocol facts are from the vendor headers this part's driver compiles
 * against (synaptics_tcm_core.h, synaptics_tcm_touch.c): MESSAGE_HEADER_SIZE 4,
 * MESSAGE_MARKER 0xa5, MESSAGE_PADDING 0x5a, enum command, enum status_code,
 * enum report_type, enum touch_report_code, struct syna_tcm_identification,
 * struct syna_tcm_app_info, and touch_get_report_data()'s LSB-first bit order.
 * Where this firmware differs from the vendor struct (the two bytes before the
 * identify part number) the decoder anchors on the part string instead of
 * assuming the vendor offset, and says so in the prefix it keeps.
 */
#include "KebabTouchTcm.h"

const char *
kebab_tcm_status_name(kebab_tcm_status status)
{
	switch (status) {
	case KEBAB_TCM_OK:
		return "ok";
	case KEBAB_TCM_ERR_SHORT:
		return "short";
	case KEBAB_TCM_ERR_MARKER:
		return "no marker";
	case KEBAB_TCM_ERR_LENGTH:
		return "bad length";
	case KEBAB_TCM_ERR_PADDING:
		return "bad padding";
	case KEBAB_TCM_ERR_CODE:
		return "unexpected code";
	case KEBAB_TCM_ERR_LAYOUT:
		return "bad layout";
	case KEBAB_TCM_ERR_PROGRAM:
		return "program does not fit";
	case KEBAB_TCM_ERR_ARGUMENT:
		return "bad argument";
	default:
		return "unknown";
	}
}

/* --------------------------------------------------------------- framing */

kebab_tcm_status
kebab_tcm_header_parse(const uint8_t raw[KEBAB_TCM_HEADER_SIZE],
    kebab_tcm_header *out)
{
	if (raw == NULL || out == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	out->code = 0;
	out->payload_len = 0;
	if (raw[0] != KEBAB_TCM_MARKER) {
		return KEBAB_TCM_ERR_MARKER;
	}
	out->payload_len = (uint16_t)kebab_tcm_le16(&raw[2]);
	out->code = raw[1];
	if (out->payload_len > KEBAB_TCM_MAX_PAYLOAD) {
		return KEBAB_TCM_ERR_LENGTH;
	}
	return KEBAB_TCM_OK;
}

uint32_t
kebab_tcm_message_bytes(const kebab_tcm_header *header)
{
	if (header == NULL) {
		return 0;
	}
	return KEBAB_TCM_HEADER_SIZE + (uint32_t)header->payload_len + 1u;
}

kebab_tcm_status
kebab_tcm_message_parse(const uint8_t *buf, uint32_t len,
    kebab_tcm_header *header, uint32_t *consumed)
{
	kebab_tcm_header h;
	kebab_tcm_status status;
	uint32_t total;

	if (buf == NULL || header == NULL || consumed == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	*consumed = 0;
	if (len < KEBAB_TCM_HEADER_SIZE) {
		return KEBAB_TCM_ERR_SHORT;
	}
	status = kebab_tcm_header_parse(buf, &h);
	if (status != KEBAB_TCM_OK) {
		return status;
	}
	total = kebab_tcm_message_bytes(&h);
	if (len < total) {
		return KEBAB_TCM_ERR_SHORT;
	}
	if (buf[total - 1] != KEBAB_TCM_PADDING) {
		return KEBAB_TCM_ERR_PADDING;
	}
	*header = h;
	*consumed = total;
	return KEBAB_TCM_OK;
}

uint32_t
kebab_tcm_find_marker(const uint8_t *buf, uint32_t len, uint32_t from)
{
	uint32_t i;

	if (buf == NULL) {
		return len;
	}
	for (i = from; i < len; i++) {
		if (buf[i] == KEBAB_TCM_MARKER) {
			return i;
		}
	}
	return len;
}

uint32_t
kebab_tcm_build_command(uint8_t *out, uint32_t cap, uint8_t command,
    const uint8_t *payload, uint32_t len)
{
	if (out == NULL || cap < len + 3u || len > 0xffffu) {
		return 0;
	}
	if (len != 0 && payload == NULL) {
		return 0;
	}
	out[0] = command;
	out[1] = (uint8_t)(len & 0xffu);
	out[2] = (uint8_t)((len >> 8) & 0xffu);
	if (len != 0) {
		memcpy(&out[3], payload, len);
	}
	return len + 3u;
}

kebab_tcm_status
kebab_tcm_expect_report(const kebab_tcm_header *header, uint8_t id)
{
	if (header == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	if (header->code != id) {
		return KEBAB_TCM_ERR_CODE;
	}
	return KEBAB_TCM_OK;
}

kebab_tcm_status
kebab_tcm_expect_status(const kebab_tcm_header *header)
{
	if (header == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	/*
	 * A command answer carries a status below 0x10.  A code from 0x10 up is a
	 * report, and a code that merely echoes the command byte (0x20, 0x26) is
	 * neither: it must not be read as "OK".
	 */
	if (header->code >= KEBAB_TCM_REPORT_IDENTIFY) {
		return KEBAB_TCM_ERR_CODE;
	}
	return KEBAB_TCM_OK;
}

/* -------------------------------------------------------- payload layouts */

kebab_tcm_status
kebab_tcm_parse_identify(const uint8_t *payload, uint32_t len,
    kebab_tcm_identify *out)
{
	uint32_t anchor;
	uint32_t i;

	if (payload == NULL || out == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));

	/*
	 * Anchor on four printable bytes: the part number is the only field in
	 * this payload whose position is self-evident.  On the vendor struct that
	 * lands at 2; on the measured S3908 payload it lands at 4.
	 */
	anchor = len;
	for (i = 0; i + 4u <= len; i++) {
		if (payload[i] >= 0x20u && payload[i] < 0x7fu &&
		    payload[i + 1] >= 0x20u && payload[i + 1] < 0x7fu &&
		    payload[i + 2] >= 0x20u && payload[i + 2] < 0x7fu &&
		    payload[i + 3] >= 0x20u && payload[i + 3] < 0x7fu) {
			anchor = i;
			break;
		}
	}
	if (anchor == len) {
		return KEBAB_TCM_ERR_LAYOUT;
	}
	if (anchor < 2u) {
		/* No room for version and mode in front of the part number. */
		return KEBAB_TCM_ERR_LAYOUT;
	}
	out->version = payload[anchor - 2u];
	out->mode = payload[anchor - 1u];
	out->prefix_len = anchor - 2u;
	for (i = 0; i < out->prefix_len && i < sizeof(out->prefix); i++) {
		out->prefix[i] = payload[i];
	}
	for (i = 0; i < 16u && anchor + i < len; i++) {
		uint8_t c = payload[anchor + i];

		if (c == 0) {
			/* The vendor part_number is NUL padded; the string ends here. */
			break;
		}
		out->part[i] = (c >= 0x20u && c < 0x7fu) ? (char)c : '.';
	}
	out->part[i] = '\0';
	if (len >= anchor + 20u) {
		out->build = kebab_tcm_le32(&payload[anchor + 16u]);
		out->build_known = 1;
	}
	return KEBAB_TCM_OK;
}

kebab_tcm_status
kebab_tcm_parse_app_info(const uint8_t *payload, uint32_t len,
    kebab_tcm_app_info *out)
{
	if (payload == NULL || out == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	if (len < KEBAB_TCM_APP_INFO_SIZE) {
		return KEBAB_TCM_ERR_LAYOUT;
	}
	memset(out, 0, sizeof(*out));
	out->version = (uint16_t)kebab_tcm_le16(&payload[0]);
	out->status = (uint16_t)kebab_tcm_le16(&payload[2]);
	out->static_config_size = (uint16_t)kebab_tcm_le16(&payload[4]);
	out->dynamic_config_size = (uint16_t)kebab_tcm_le16(&payload[6]);
	out->app_config_start_write_block = (uint16_t)kebab_tcm_le16(&payload[8]);
	out->app_config_size = (uint16_t)kebab_tcm_le16(&payload[10]);
	out->max_touch_report_config_size = (uint16_t)kebab_tcm_le16(&payload[12]);
	out->max_touch_report_payload_size = (uint16_t)kebab_tcm_le16(&payload[14]);
	memcpy(out->customer_config_id, &payload[16], 16);
	out->max_x = (uint16_t)kebab_tcm_le16(&payload[32]);
	out->max_y = (uint16_t)kebab_tcm_le16(&payload[34]);
	out->max_objects = (uint16_t)kebab_tcm_le16(&payload[36]);
	out->num_of_buttons = (uint16_t)kebab_tcm_le16(&payload[38]);
	out->num_of_image_rows = (uint16_t)kebab_tcm_le16(&payload[40]);
	out->num_of_image_cols = (uint16_t)kebab_tcm_le16(&payload[42]);
	out->has_hybrid_data = (uint16_t)kebab_tcm_le16(&payload[44]);
	return KEBAB_TCM_OK;
}

/* --------------------------------------------------------- report program */

uint32_t
kebab_tcm_program_build(uint8_t *out, uint32_t cap, int pressure)
{
	uint32_t n = 0;

	if (out == NULL) {
		return 0;
	}
	/* FOREACH_END + END, plus the fields below; 13 with pressure, 11 without. */
	if (cap < (pressure ? 13u : 11u)) {
		return 0;
	}
	out[n++] = KEBAB_TCM_TOUCH_FOREACH_ACTIVE_OBJECT;
	out[n++] = KEBAB_TCM_TOUCH_OBJECT_N_INDEX;
	out[n++] = 4;
	out[n++] = KEBAB_TCM_TOUCH_OBJECT_N_CLASSIFICATION;
	out[n++] = 4;
	out[n++] = KEBAB_TCM_TOUCH_OBJECT_N_X_POSITION;
	out[n++] = 12;
	out[n++] = KEBAB_TCM_TOUCH_OBJECT_N_Y_POSITION;
	out[n++] = 12;
	if (pressure) {
		out[n++] = KEBAB_TCM_TOUCH_OBJECT_N_Z;
		out[n++] = 8;
	}
	out[n++] = KEBAB_TCM_TOUCH_FOREACH_END;
	out[n++] = KEBAB_TCM_TOUCH_END;
	return n;
}

/*
 * Find the foreach body and add up the field widths inside it.  The decoder
 * only implements the program this driver writes, and the body has to be a flat
 * sequence of object fields for the "bits per contact" to be a constant; a
 * program that is anything else is rejected here rather than misread later.
 */
static kebab_tcm_status
kebab_tcm_program_scan(const uint8_t *program, uint32_t len, uint32_t *body_out,
    uint32_t *bits_out)
{
	uint32_t idx = 0;
	uint32_t body;
	uint32_t bits = 0;

	if (program == NULL || body_out == NULL || bits_out == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	while (idx < len) {
		uint8_t code = program[idx++];

		if (code == KEBAB_TCM_TOUCH_END) {
			return KEBAB_TCM_ERR_LAYOUT;   /* no foreach at all */
		}
		if (code != KEBAB_TCM_TOUCH_FOREACH_ACTIVE_OBJECT &&
		    code != KEBAB_TCM_TOUCH_FOREACH_OBJECT) {
			return KEBAB_TCM_ERR_LAYOUT;   /* top-level field not supported */
		}
		body = idx;
		while (idx < len) {
			uint8_t field = program[idx++];
			uint32_t width;

			if (field == KEBAB_TCM_TOUCH_FOREACH_END) {
				*body_out = body;
				*bits_out = bits;
				return bits == 0 ? KEBAB_TCM_ERR_LAYOUT :
				    KEBAB_TCM_OK;
			}
			if (field == KEBAB_TCM_TOUCH_END || idx >= len) {
				return KEBAB_TCM_ERR_LAYOUT;
			}
			width = program[idx++];
			switch (field) {
			case KEBAB_TCM_TOUCH_OBJECT_N_INDEX:
			case KEBAB_TCM_TOUCH_OBJECT_N_CLASSIFICATION:
				if (width > 8u) {
					return KEBAB_TCM_ERR_LAYOUT;
				}
				break;
			case KEBAB_TCM_TOUCH_OBJECT_N_X_POSITION:
			case KEBAB_TCM_TOUCH_OBJECT_N_Y_POSITION:
				if (width > 16u) {
					return KEBAB_TCM_ERR_LAYOUT;
				}
				break;
			case KEBAB_TCM_TOUCH_OBJECT_N_Z:
				if (width > 8u) {
					return KEBAB_TCM_ERR_LAYOUT;
				}
				break;
			default:
				return KEBAB_TCM_ERR_LAYOUT;
			}
			bits += width;
			if (bits > 64u) {
				return KEBAB_TCM_ERR_LAYOUT;
			}
		}
		return KEBAB_TCM_ERR_LAYOUT;           /* missing FOREACH_END */
	}
	return KEBAB_TCM_ERR_LAYOUT;
}

uint32_t
kebab_tcm_program_object_bits(const uint8_t *program, uint32_t len)
{
	uint32_t body = 0;
	uint32_t bits = 0;

	if (kebab_tcm_program_scan(program, len, &body, &bits) != KEBAB_TCM_OK) {
		return 0;
	}
	return bits;
}

uint32_t
kebab_tcm_report_payload_build(uint8_t *out, uint32_t cap,
    const uint8_t *program, uint32_t program_len, uint32_t firmware_max)
{
	uint32_t total;

	if (out == NULL || program == NULL || program_len == 0) {
		return 0;
	}
	if (firmware_max > KEBAB_TCM_REPORT_CONFIG_MAX) {
		/* Not a size this driver carries a buffer for; send the program. */
		firmware_max = 0;
	}
	if (firmware_max != 0 && firmware_max < program_len) {
		/*
		 * The firmware says its report program buffer is smaller than the
		 * program: sending the program anyway would overrun the very
		 * buffer the app info just described.  The caller has to fall
		 * back to a shorter program or report the mismatch.
		 */
		return 0;
	}
	total = (firmware_max != 0) ? firmware_max : program_len;
	if (total > cap) {
		return 0;
	}
	/*
	 * Zero padding is not filler: the vendor driver sends the whole
	 * max_touch_report_config_size buffer and the firmware's program parser
	 * stops at TOUCH_END, which is a zero byte.
	 */
	memset(out, 0, total);
	memcpy(out, program, program_len);
	return total;
}

/* --------------------------------------------------------------- decoding */

uint32_t
kebab_tcm_bits(const uint8_t *data, uint32_t offset, uint32_t bits)
{
	uint32_t out = 0;
	uint32_t remaining = bits;
	uint32_t bit_offset = offset & 7u;
	uint32_t byte = offset >> 3;

	while (remaining != 0) {
		uint32_t available = 8u - bit_offset;
		uint32_t take = (available < remaining) ? available : remaining;
		uint32_t mask = (1u << take) - 1u;
		uint32_t v = ((uint32_t)data[byte] >> bit_offset) & mask;

		out |= v << (bits - remaining);
		bit_offset = 0;
		byte++;
		remaining -= take;
	}
	return out;
}

static kebab_tcm_status
kebab_tcm_walk_object(const uint8_t *program, uint32_t len, uint32_t body,
    const uint8_t *payload, uint32_t payload_bits, uint32_t *offset,
    kebab_tcm_object *object)
{
	uint32_t idx = body;

	memset(object, 0, sizeof(*object));
	while (idx < len) {
		uint8_t field = program[idx++];
		uint32_t width;

		if (field == KEBAB_TCM_TOUCH_FOREACH_END) {
			return KEBAB_TCM_OK;
		}
		if (idx >= len) {
			return KEBAB_TCM_ERR_LAYOUT;
		}
		width = program[idx++];
		if (*offset + width > payload_bits) {
			return KEBAB_TCM_ERR_SHORT;
		}
		switch (field) {
		case KEBAB_TCM_TOUCH_OBJECT_N_INDEX:
			object->index = (uint8_t)kebab_tcm_bits(payload, *offset,
			    width);
			break;
		case KEBAB_TCM_TOUCH_OBJECT_N_CLASSIFICATION:
			object->classification = (uint8_t)kebab_tcm_bits(payload,
			    *offset, width);
			break;
		case KEBAB_TCM_TOUCH_OBJECT_N_X_POSITION:
			object->x = (uint16_t)kebab_tcm_bits(payload, *offset,
			    width);
			break;
		case KEBAB_TCM_TOUCH_OBJECT_N_Y_POSITION:
			object->y = (uint16_t)kebab_tcm_bits(payload, *offset,
			    width);
			break;
		case KEBAB_TCM_TOUCH_OBJECT_N_Z:
			object->z = (uint8_t)kebab_tcm_bits(payload, *offset,
			    width);
			break;
		default:
			return KEBAB_TCM_ERR_LAYOUT;
		}
		*offset += width;
	}
	return KEBAB_TCM_ERR_LAYOUT;
}

kebab_tcm_status
kebab_tcm_decode_report(const uint8_t *program, uint32_t program_len,
    const uint8_t *payload, uint32_t payload_len, uint32_t max_objects,
    kebab_tcm_report *out)
{
	uint32_t body = 0;
	uint32_t object_bits = 0;
	uint32_t offset = 0;
	kebab_tcm_status status;

	if (program == NULL || payload == NULL || out == NULL) {
		return KEBAB_TCM_ERR_ARGUMENT;
	}
	status = kebab_tcm_program_scan(program, program_len, &body,
	    &object_bits);
	if (status != KEBAB_TCM_OK) {
		return status;
	}
	memset(out, 0, sizeof(*out));
	out->payload_bits = payload_len * 8u;
	if (max_objects == 0 || max_objects > KEBAB_TCM_MAX_OBJECTS) {
		max_objects = KEBAB_TCM_MAX_OBJECTS;
	}

	/*
	 * A contact is decoded only when all of its bits are there.  A tail that
	 * is too short for another contact is counted, not decoded: decoding it
	 * would read a contact out of the next message's bytes.
	 */
	while (out->objects < max_objects &&
	    offset + object_bits <= out->payload_bits) {
		kebab_tcm_object object;

		status = kebab_tcm_walk_object(program, program_len, body, payload,
		    out->payload_bits, &offset, &object);
		if (status != KEBAB_TCM_OK) {
			return status;
		}
		out->object[out->objects++] = object;
	}
	out->bits = offset;
	out->truncated_bits = out->payload_bits - offset;
	if (out->truncated_bits != 0) {
		/*
		 * All-zero tails are byte padding, not a cut contact: a report
		 * that ends on a zero byte boundary must not lift every finger.
		 * A non-zero tail is a truncated contact and the caller drops the
		 * whole report.
		 */
		uint32_t bit;

		for (bit = offset; bit < out->payload_bits; bit++) {
			if (((payload[bit >> 3] >> (bit & 7u)) & 1u) != 0) {
				out->truncated_nonzero = 1;
				break;
			}
		}
	}
	return KEBAB_TCM_OK;
}

/* --------------------------------------------------------------- tracking */

uint32_t
kebab_tcm_track(kebab_tcm_tracker *tracker, const kebab_tcm_report *report,
    kebab_tcm_event *events, uint32_t max_events)
{
	uint16_t seen = 0;
	uint32_t stored = 0;
	uint32_t i;

	if (tracker == NULL || report == NULL) {
		return 0;
	}
	for (i = 0; i < report->objects; i++) {
		const kebab_tcm_object *object = &report->object[i];
		kebab_tcm_event event;
		uint8_t phase;

		if (object->classification != KEBAB_TCM_CLASS_FINGER &&
		    object->classification != KEBAB_TCM_CLASS_GLOVED_FINGER) {
			continue;
		}
		if (object->index >= KEBAB_TCM_MAX_OBJECTS) {
			continue;
		}
		phase = (tracker->active & (1u << object->index)) ?
		    KEBAB_TCM_PHASE_MOVE : KEBAB_TCM_PHASE_DOWN;
		seen |= (uint16_t)(1u << object->index);
		tracker->last_x[object->index] = object->x;
		tracker->last_y[object->index] = object->y;
		event.phase = phase;
		event.slot = object->index;
		event.raw_x = object->x;
		event.raw_y = object->y;
		event.pressure = object->z;
		if (events != NULL && stored < max_events) {
			events[stored++] = event;
		}
	}
	/* A slot that was active and is missing from this report has lifted. */
	for (i = 0; i < KEBAB_TCM_MAX_OBJECTS; i++) {
		kebab_tcm_event event;

		if (!(tracker->active & (1u << i)) || (seen & (1u << i))) {
			continue;
		}
		event.phase = KEBAB_TCM_PHASE_UP;
		event.slot = (uint8_t)i;
		event.raw_x = tracker->last_x[i];
		event.raw_y = tracker->last_y[i];
		event.pressure = 0;
		if (events != NULL && stored < max_events) {
			events[stored++] = event;
		}
	}
	tracker->active = seen;
	return stored;
}

/* -------------------------------------------------------------- transform */

kebab_tcm_point
kebab_tcm_transform(uint32_t raw_x, uint32_t raw_y, uint32_t max_x,
    uint32_t max_y)
{
	kebab_tcm_point p;

	if (max_x == 0 || max_x > KEBAB_TCM_SENSOR_LIMIT) {
		max_x = KEBAB_TCM_SENSOR_MAX_X;
	}
	if (max_y == 0 || max_y > KEBAB_TCM_SENSOR_LIMIT) {
		max_y = KEBAB_TCM_SENSOR_MAX_Y;
	}
	memset(&p, 0, sizeof(p));
	if (raw_x > max_x) {
		raw_x = max_x;
		p.clamped = 1;
	}
	if (raw_y > max_y) {
		raw_y = max_y;
		p.clamped = 1;
	}
	p.panel_x = (int32_t)(((uint64_t)raw_x * (KEBAB_TCM_PANEL_W - 1) +
	    max_x / 2u) / max_x);
	p.panel_y = (int32_t)(((uint64_t)raw_y * (KEBAB_TCM_PANEL_H - 1) +
	    max_y / 2u) / max_y);
	p.logical_x = p.panel_x + KEBAB_TCM_LOGICAL_ORIGIN_X;
	p.logical_y = p.panel_y + KEBAB_TCM_LOGICAL_ORIGIN_Y;
	/* Defensive only: with the origin above these cannot fire. */
	if (p.logical_x >= KEBAB_TCM_LOGICAL_W) {
		p.logical_x = KEBAB_TCM_LOGICAL_W - 1;
		p.clamped = 1;
	}
	if (p.logical_y >= KEBAB_TCM_LOGICAL_H) {
		p.logical_y = KEBAB_TCM_LOGICAL_H - 1;
		p.clamped = 1;
	}
	return p;
}
