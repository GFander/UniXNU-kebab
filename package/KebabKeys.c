/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM8150 key decode and debounce state machine.
 *
 * The register facts, the GPIO active-low convention and the two possible
 * volume-down paths (PM8150 GPIO7 and PON RESIN) are documented once in
 * KebabKeys.h.  This file is deliberately free of MMIO, SPMI and kernel
 * objects: bsd/dev/kebab_keys.c feeds it four bytes and publishes what it
 * returns, and KebabKeysHostTests.c drives the same functions.
 *
 * Two rules decide most of the behaviour:
 *
 *   - a sample that is all zero is a silent bus, not "every key held".  The
 *     GPIO level bit is inverted (active low), so a naive decoder would read
 *     0x00 as both volume keys pressed.  The identity byte (0x0c504, always
 *     0x10 on a live PM8150 GPIO page) is part of the sample for exactly this
 *     reason: if it is zero as well, nothing was read.
 *   - a transfer that failed, or a page that is not the expected peripheral,
 *     changes no state.  A read error is "no observation", not a release.
 */
#include "KebabKeys.h"

void
kebab_keys_reset(kebab_keys_state *state)
{
	unsigned k;

	if (state == NULL) {
		return;
	}
	memset(state, 0, sizeof(*state));
	for (k = 0; k < KEBAB_KEYS_KEY_COUNT; k++) {
		state->key[k].usable = 1;
	}
}

void
kebab_keys_enable(kebab_keys_state *state, unsigned key, int usable)
{
	kebab_keys_key *k;

	if (state == NULL || key >= KEBAB_KEYS_KEY_COUNT) {
		return;
	}
	k = &state->key[key];
	k->usable = usable != 0;
	if (!k->usable) {
		/* Callers enable before feeding; never leave a reported hold. */
		k->down = 0;
		k->candidate = 0;
		k->candidate_count = 0;
		k->held_samples = 0;
		k->pressed_since_ns = 0;
		k->stuck = 0;
	}
}

const char *
kebab_keys_name(unsigned key)
{
	switch (key) {
	case KEBAB_KEYS_KEY_POWER:
		return "power";
	case KEBAB_KEYS_KEY_VOLUME_UP:
		return "volume-up";
	case KEBAB_KEYS_KEY_VOLUME_DOWN:
		return "volume-down";
	default:
		return "unknown";
	}
}

/*
 * Decide what one look at the four registers means.  The all-zero test comes
 * before the identity test so a dead bus is reported as a dead bus, not as a
 * peripheral that identified itself as zero.
 */
kebab_keys_sample_status
kebab_keys_classify(const kebab_keys_sample *sample)
{
	if (sample == NULL || !sample->read_ok) {
		return KEBAB_KEYS_SAMPLE_READ_ERROR;
	}
	if (sample->pon_rt == 0 && sample->gpio_up_rt == 0 &&
	    sample->gpio_down_rt == 0 && sample->gpio_ident == 0) {
		return KEBAB_KEYS_SAMPLE_ZERO;
	}
	if (sample->gpio_ident != KEBAB_KEYS_GPIO_TYPE) {
		return KEBAB_KEYS_SAMPLE_BAD_IDENTITY;
	}
	return KEBAB_KEYS_SAMPLE_OK;
}

/*
 * Feed one sample.  Returns the number of accepted transitions (0 to 3) and
 * fills events in key order.  The state does not change on an invalid sample.
 *
 * Debounce is two consecutive agreeing samples, which is 32 ms at the default
 * 16 ms poll: longer than the vendor gpio-keys debounce-interval of 15 ms and
 * than the PON block's 15.6 ms hardware debounce.  A bouncy line therefore
 * produces no event at all until it settles.
 *
 * The reported state starts released.  A key already held on the first
 * believed sample is reported pressed after the same two samples, and then
 * once more only when it is released; the stuck flag fires after ten seconds
 * of a continuous hold so the log can say that the key is not coming back.
 */
size_t
kebab_keys_feed(kebab_keys_state *state, const kebab_keys_sample *sample,
    uint64_t now_ns, kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS])
{
	uint8_t raw[KEBAB_KEYS_KEY_COUNT];
	int resin;
	size_t count = 0;
	unsigned k;

	if (state == NULL || sample == NULL || events == NULL) {
		return 0;
	}
	if (kebab_keys_classify(sample) != KEBAB_KEYS_SAMPLE_OK) {
		state->invalid++;
		return 0;
	}
	state->samples++;

	/*
	 * PON RT_STS bits are already asserted-high.  PM8150 GPIO RT_STS bit 0
	 * is the pin level and the vendor gpio_keys node marks both GPIOs
	 * GPIO_ACTIVE_LOW, so a clear level bit is a press.
	 */
	resin = (sample->pon_rt & KEBAB_KEYS_PON_RESIN) != 0;
	raw[KEBAB_KEYS_KEY_POWER] =
	    (sample->pon_rt & KEBAB_KEYS_PON_KPDPWR) != 0;
	raw[KEBAB_KEYS_KEY_VOLUME_UP] =
	    (sample->gpio_up_rt & KEBAB_KEYS_GPIO_LEVEL) == 0;
	raw[KEBAB_KEYS_KEY_VOLUME_DOWN] =
	    ((sample->gpio_down_rt & KEBAB_KEYS_GPIO_LEVEL) == 0) || resin;

	for (k = 0; k < KEBAB_KEYS_KEY_COUNT; k++) {
		kebab_keys_key *key = &state->key[k];
		kebab_keys_event *event;

		if (!key->usable) {
			continue;
		}
		if (raw[k]) {
			key->held_samples++;
			if (key->held_samples == 1) {
				key->pressed_since_ns = now_ns;
			}
			if (!key->stuck && now_ns >= key->pressed_since_ns &&
			    now_ns - key->pressed_since_ns >=
			    KEBAB_KEYS_STUCK_NS) {
				key->stuck = 1;
			}
		} else {
			key->held_samples = 0;
			key->pressed_since_ns = 0;
			key->stuck = 0;
		}

		if (raw[k] == key->down) {
			key->candidate = key->down;
			key->candidate_count = 0;
			continue;
		}
		if (key->candidate_count != 0 && key->candidate == raw[k]) {
			key->candidate_count++;
		} else {
			key->candidate = raw[k];
			key->candidate_count = 1;
		}
		if (key->candidate_count < KEBAB_KEYS_DEBOUNCE_SAMPLES) {
			continue;
		}

		key->down = raw[k];
		key->candidate = key->down;
		key->candidate_count = 0;
		if (key->down) {
			key->presses++;
		} else {
			key->releases++;
		}

		event = &events[count++];
		event->key = (uint8_t)k;
		event->pressed = key->down ? 1 : 0;
		event->flags = (k == KEBAB_KEYS_KEY_VOLUME_DOWN && resin) ?
		    KEBAB_KEYS_EVENT_RESIN : 0;
		event->raw_pon = sample->pon_rt;
		event->raw_gpio_up = sample->gpio_up_rt;
		event->raw_gpio_down = sample->gpio_down_rt;
		event->raw_ident = sample->gpio_ident;
		event->seq = state->seq++;
		event->timestamp_ns = now_ns;
	}
	return count;
}

int
kebab_keys_pressed(const kebab_keys_state *state, unsigned key)
{
	if (state == NULL || key >= KEBAB_KEYS_KEY_COUNT) {
		return 0;
	}
	return state->key[key].usable && state->key[key].down;
}
