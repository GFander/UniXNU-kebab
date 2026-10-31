/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host tests for the PM8150 key decode and debounce state machine.
 *
 * No MMIO, no SPMI, no kernel: every case below is a sequence of four-byte
 * samples and the events the decoder returns for them.  The raw bytes are
 * the ones the driver reads from 0x0810, 0xc510, 0xc610 and 0xc504.
 *
 * Build with:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -o kebab-keys-test \
 *       package/KebabKeys.c package/KebabKeysHostTests.c
 */
#include "KebabKeys.h"

#include <stdio.h>

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

static uint64_t clock_ns;

static kebab_keys_sample
sample(uint8_t pon, uint8_t up, uint8_t down, uint8_t ident)
{
	kebab_keys_sample s;

	s.pon_rt = pon;
	s.gpio_up_rt = up;
	s.gpio_down_rt = down;
	s.gpio_ident = ident;
	s.read_ok = 1;
	return s;
}

/* PON 0, GPIO6 high, GPIO7 high, identity 0x10: nothing held. */
static kebab_keys_sample
released(void)
{
	return sample(0x00, 0x01, 0x01, KEBAB_KEYS_GPIO_TYPE);
}

static size_t
feed(kebab_keys_state *state, kebab_keys_sample s,
    kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS])
{
	clock_ns += 16ull * 1000ull * 1000ull; /* 16 ms, the driver's default */
	return kebab_keys_feed(state, &s, clock_ns, events);
}

static void
begin(kebab_keys_state *state)
{
	kebab_keys_reset(state);
	clock_ns = 0;
}

static size_t
feed_n(kebab_keys_state *state, kebab_keys_sample s, unsigned n)
{
	kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS];
	size_t total = 0;
	unsigned i;

	for (i = 0; i < n; i++) {
		total += feed(state, s, events);
	}
	return total;
}

int
main(void)
{
	kebab_keys_state state;
	kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS];
	kebab_keys_sample s;
	size_t n;

	printf("K1 classify: live, all-zero, wrong identity and failed reads\n");
	s = released();
	CHECK(kebab_keys_classify(&s) == KEBAB_KEYS_SAMPLE_OK,
	    "a released PM8150 page is a valid sample");
	s = sample(0x00, 0x00, 0x00, 0x00);
	CHECK(kebab_keys_classify(&s) == KEBAB_KEYS_SAMPLE_ZERO,
	    "all four bytes zero is ZERO, not both volume keys held");
	s = sample(0x00, 0x00, 0x00, 0x10);
	CHECK(kebab_keys_classify(&s) == KEBAB_KEYS_SAMPLE_OK,
	    "a zero PON/GPIO byte with the live identity is a real sample");
	s = sample(0x00, 0x01, 0x01, 0x11);
	CHECK(kebab_keys_classify(&s) == KEBAB_KEYS_SAMPLE_BAD_IDENTITY,
	    "a page that is not a PM8150 GPIO is rejected");
	s = released();
	s.read_ok = 0;
	CHECK(kebab_keys_classify(&s) == KEBAB_KEYS_SAMPLE_READ_ERROR,
	    "a failed transfer is a read error");

	printf("K2 power: a clean press and release, one event each\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "two released looks: no event");
	n = feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 0, "one held look is still debounce, no event");
	n = feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 1, "the second held look is a press");
	CHECK(n == 1 && events[0].key == KEBAB_KEYS_KEY_POWER,
	    "the press is the power key");
	CHECK(n == 1 && events[0].pressed == 1, "the press says pressed");
	CHECK(n == 1 && events[0].raw_pon == KEBAB_KEYS_PON_KPDPWR &&
	    events[0].raw_gpio_up == 0x01 && events[0].raw_gpio_down == 0x01 &&
	    events[0].raw_ident == KEBAB_KEYS_GPIO_TYPE,
	    "the press carries every raw byte");
	CHECK(n == 1 && events[0].seq == 0, "the first event has sequence 0");
	CHECK(n == 1 && events[0].flags == 0, "power has no RESIN flag");
	CHECK(feed_n(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), 3) == 0, "holding does not repeat the press");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].presses == 1,
	    "exactly one press was counted");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 1,
	    "the reported state is held");
	n = feed(&state, released(), events);
	CHECK(n == 0, "one released look is still debounce");
	n = feed(&state, released(), events);
	CHECK(n == 1 && events[0].pressed == 0,
	    "the second released look is a release");
	CHECK(n == 1 && events[0].seq == 1, "the release is sequence 1");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 0,
	    "the reported state is released again");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].releases == 1,
	    "exactly one release was counted");

	printf("K3 volume-up: PM8150 GPIO6 low is a press\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	CHECK(feed(&state, sample(0x00, 0x00, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    events) == 0, "GPIO6 low once: no event");
	n = feed(&state, sample(0x00, 0x00, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    events);
	CHECK(n == 1 && events[0].key == KEBAB_KEYS_KEY_VOLUME_UP &&
	    events[0].pressed == 1, "GPIO6 low twice: volume-up press");
	CHECK(n == 1 && events[0].raw_gpio_up == 0x00 &&
	    events[0].raw_gpio_down == 0x01,
	    "the raw bytes show GPIO6 low and GPIO7 high");
	CHECK(feed_n(&state, sample(0x00, 0x01, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    2) == 1, "GPIO6 high twice: one release");
	CHECK(state.key[KEBAB_KEYS_KEY_VOLUME_UP].presses == 1 &&
	    state.key[KEBAB_KEYS_KEY_VOLUME_UP].releases == 1,
	    "volume-up counted one press and one release");

	printf("K4 volume-down: PM8150 GPIO7 low is a press\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	n = feed(&state, sample(0x00, 0x01, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    events);
	CHECK(n == 0, "GPIO7 low once: no event");
	n = feed(&state, sample(0x00, 0x01, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    events);
	CHECK(n == 1 && events[0].key == KEBAB_KEYS_KEY_VOLUME_DOWN &&
	    events[0].pressed == 1, "GPIO7 low twice: volume-down press");
	CHECK(n == 1 && events[0].flags == 0,
	    "GPIO7 alone does not claim the PON RESIN path");
	CHECK(feed_n(&state, released(), 2) == 1,
	    "GPIO7 high twice: one release");
	CHECK(state.key[KEBAB_KEYS_KEY_VOLUME_DOWN].presses == 1 &&
	    state.key[KEBAB_KEYS_KEY_VOLUME_DOWN].releases == 1,
	    "volume-down counted one press and one release");

	printf("K5 bounce: a line that will not settle produces nothing\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	{
		unsigned i;
		size_t total = 0;

		for (i = 0; i < 20; i++) {
			if ((i & 1u) == 0) {
				total += feed(&state, sample(
				    KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
				    KEBAB_KEYS_GPIO_TYPE), events);
			} else {
				total += feed(&state, released(), events);
			}
		}
		CHECK(total == 0, "20 alternating samples: no press and no release");
	}
	n = feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 0, "the first settled sample is still debounce");
	n = feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 1 && events[0].pressed == 1, "two settled samples: one press");
	{
		unsigned i;
		size_t total = 0;

		for (i = 0; i < 20; i++) {
			if ((i & 1u) == 0) {
				total += feed(&state, released(), events);
			} else {
				total += feed(&state, sample(
				    KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
				    KEBAB_KEYS_GPIO_TYPE), events);
			}
		}
		CHECK(total == 0, "20 alternating samples while held: no release");
	}
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 1,
	    "the key is still reported held through the bounce");
	CHECK(feed_n(&state, released(), 2) == 1,
	    "two settled released samples: one release");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].presses == 1 &&
	    state.key[KEBAB_KEYS_KEY_POWER].releases == 1,
	    "the bounce added no extra transitions");

	printf("K6 both volume keys at once (all-zero key bytes, live identity)\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	CHECK(feed(&state, sample(0x00, 0x00, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    events) == 0, "both low once: no event yet");
	n = feed(&state, sample(0x00, 0x00, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    events);
	CHECK(n == 2, "both low twice: two presses in one sample");
	CHECK(n == 2 && events[0].key == KEBAB_KEYS_KEY_VOLUME_UP &&
	    events[0].pressed == 1, "the first event is volume-up");
	CHECK(n == 2 && events[1].key == KEBAB_KEYS_KEY_VOLUME_DOWN &&
	    events[1].pressed == 1, "the second event is volume-down");
	CHECK(n == 2 && events[0].seq == 0 && events[1].seq == 1,
	    "the two events have consecutive sequence numbers");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_UP) == 1 &&
	    kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_DOWN) == 1,
	    "both keys are reported held");
	CHECK(feed(&state, released(), events) == 0,
	    "one released look: still debounce");
	n = feed(&state, released(), events);
	CHECK(n == 2 && events[0].pressed == 0 && events[1].pressed == 0,
	    "both released twice: two releases in one sample");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_UP) == 0 &&
	    kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_DOWN) == 0,
	    "both keys are reported released");

	printf("K7 all-zero: no key, and no state change\n");
	begin(&state);
	CHECK(feed_n(&state, sample(0x00, 0x00, 0x00, 0x00), 5) == 0,
	    "five all-zero samples: no event");
	CHECK(state.samples == 0 && state.invalid == 5,
	    "all-zero samples are invalid, not five held keys");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 0 &&
	    kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_UP) == 0 &&
	    kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_DOWN) == 0,
	    "all-zero is no key rather than every key");
	CHECK(feed_n(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), 2) == 1, "a real press still works");
	CHECK(feed_n(&state, sample(0x00, 0x00, 0x00, 0x00), 5) == 0,
	    "all-zero while a key is held: no release and no second press");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 1,
	    "the held key is still held through the silent bus");
	CHECK(state.invalid == 10, "every silent sample was counted");
	CHECK(feed_n(&state, released(), 2) == 1,
	    "the real release is still seen after the silence");

	printf("K8 stuck: one press, no repeats, flagged after ten seconds\n");
	begin(&state);
	{
		unsigned i;
		size_t total = 0;

		for (i = 0; i < 700; i++) {
			total += feed(&state, sample(KEBAB_KEYS_PON_KPDPWR,
			    0x01, 0x01, KEBAB_KEYS_GPIO_TYPE), events);
		}
		CHECK(total == 1, "700 held samples produce exactly one press");
	}
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].presses == 1,
	    "the press count is one");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].stuck == 1,
	    "the stuck flag is set after ten seconds");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].held_samples == 700,
	    "the hold length is tracked");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_POWER) == 1,
	    "a stuck key is still reported held, not re-pressed");
	CHECK(feed_n(&state, released(), 2) == 1,
	    "the release after the stuck hold is still one release");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].stuck == 0,
	    "the stuck flag clears on release");
	CHECK(state.key[KEBAB_KEYS_KEY_POWER].presses == 1 &&
	    state.key[KEBAB_KEYS_KEY_POWER].releases == 1,
	    "the stuck hold added no transitions");

	printf("K9 read errors and wrong identity change no state\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	{
		kebab_keys_sample bad = released();
		unsigned i;
		size_t total = 0;

		bad.read_ok = 0;
		for (i = 0; i < 10; i++) {
			total += feed(&state, bad, events);
		}
		CHECK(total == 0, "ten failed transfers produce no event");
		CHECK(state.invalid == 10, "the failed transfers are counted");
	}
	s = released();
	s.gpio_ident = 0x11;
	CHECK(feed_n(&state, s, 10) == 0,
	    "ten samples from a wrong page produce no event");
	CHECK(state.invalid == 20, "the wrong-page samples are counted");
	CHECK(state.samples == 2, "only the two good samples were used");
	CHECK(feed_n(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), 2) == 1,
	    "a good press after the errors is still decoded");

	printf("K10 PON RESIN is volume-down, and the flag says so\n");
	begin(&state);
	CHECK(feed_n(&state, released(), 2) == 0, "released baseline");
	n = feed(&state, sample(KEBAB_KEYS_PON_RESIN, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 0, "RESIN high once: debounce");
	n = feed(&state, sample(KEBAB_KEYS_PON_RESIN, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 1 && events[0].key == KEBAB_KEYS_KEY_VOLUME_DOWN &&
	    events[0].pressed == 1, "RESIN high twice: volume-down press");
	CHECK(n == 1 && (events[0].flags & KEBAB_KEYS_EVENT_RESIN) != 0,
	    "the RESIN flag marks the PON path");
	CHECK(n == 1 && events[0].raw_pon == KEBAB_KEYS_PON_RESIN,
	    "the raw PON byte shows bit 1");
	CHECK(feed_n(&state, sample(0x00, 0x01, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    2) == 0,
	    "GPIO7 low while still held is not a new press");
	CHECK(feed_n(&state, sample(0x00, 0x01, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    2) == 1, "both sources high: one release");
	CHECK(feed_n(&state, sample(0x00, 0x01, 0x00, KEBAB_KEYS_GPIO_TYPE),
	    2) == 1, "GPIO7-only press still works");
	CHECK(state.key[KEBAB_KEYS_KEY_VOLUME_DOWN].presses == 2,
	    "volume-down counted the RESIN press and the GPIO7 press");

	printf("K11 a key held before the first sample is still reported\n");
	begin(&state);
	CHECK(feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events) == 0, "first held look: debounce");
	n = feed(&state, sample(KEBAB_KEYS_PON_KPDPWR, 0x01, 0x01,
	    KEBAB_KEYS_GPIO_TYPE), events);
	CHECK(n == 1 && events[0].pressed == 1,
	    "the key held at start produces one press after debounce");
	CHECK(feed_n(&state, released(), 2) == 1, "and one release when it goes");

	printf("K12 an unusable source never reports and can be turned back on\n");
	begin(&state);
	kebab_keys_enable(&state, KEBAB_KEYS_KEY_VOLUME_UP, 0);
	CHECK(feed_n(&state, sample(0x00, 0x00, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    5) == 0, "a disabled volume-up produces no event");
	CHECK(kebab_keys_pressed(&state, KEBAB_KEYS_KEY_VOLUME_UP) == 0,
	    "a disabled volume-up is never reported pressed");
	kebab_keys_enable(&state, KEBAB_KEYS_KEY_VOLUME_UP, 1);
	CHECK(feed_n(&state, sample(0x00, 0x00, 0x01, KEBAB_KEYS_GPIO_TYPE),
	    2) == 1, "re-enabled volume-up decodes again");

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
