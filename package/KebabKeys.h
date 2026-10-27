/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM8150 key sources of the OnePlus 8T (kebab) and the press/release
 * state machine on top of them.
 *
 * WHERE THE KEYS ARE (vendor device tree and vendor drivers, not guessed)
 *
 * Power is the PM8150 power-on block ("PON"), SID 0, base 0x0800:
 *
 *   qcom,power-on@800 in arch/arm64/boot/dts/vendor/qcom/pm8150.dtsi,
 *   qcom,pon_1 { qcom,pon-type = <PON_POWER_ON_TYPE_KPDPWR>;
 *                linux,code = <KEY_POWER>; }
 *
 * Volume up and volume down are NOT TLMM pins and are not PON RESIN.  They
 * are two inputs of the PM8150 GPIO block, active low, pulled up:
 *
 *   &pm8150_gpios key_vol_up  { pins = "gpio6"; input-enable; bias-pull-up; }
 *   &pm8150_gpios key_vol_down{ pins = "gpio7"; input-enable; bias-pull-up; }
 *   gpio_keys vol_up  { gpios = <&pm8150_gpios 6 GPIO_ACTIVE_LOW>; code KEY_VOLUMEUP; }
 *   gpio_keys vol_down{ gpios = <&pm8150_gpios 7 GPIO_ACTIVE_LOW>; code KEY_VOLUMEDOWN; }
 *
 * The first two stanzas are in kona-pmic-overlay.dtsi and kebab-oem.dtsi;
 * the gpio_keys node is in kona-mtp.dtsi and kebab-oem.dtsi.  The final
 * decompiled device tree (kebab-vendor.dts) has the same two entries with
 * phandle 0x625, which resolves to qcom,pm8150@0/pinctrl@c000.
 *
 * The vendor PON driver also configures RESIN as KEY_VOLUMEDOWN:
 *
 *   qcom,pon_2 { qcom,pon-type = <PON_POWER_ON_TYPE_RESIN>;
 *                linux,code = <KEY_VOLUMEDOWN>; }
 *
 * so the vendor tree contains two possible report paths for volume down.
 * This decoder ORs them into one logical key and keeps the PON RESIN bit in
 * the event flags and the raw bytes, so a hardware run shows which path the
 * board actually wires instead of hiding the question.
 *
 * REGISTER LAYOUTS
 *
 *   PON RT_STS        base 0x0800 + 0x10 = 0x0810
 *     bit 0  KPDPWR_N_SET   qpnp-power-on.c: QPNP_PON_KPDPWR_N_SET = BIT(0)
 *     bit 1  RESIN_N_SET    qpnp-power-on.c: QPNP_PON_RESIN_N_SET  = BIT(1)
 *     A set bit means the key is held (qpnp_pon_input_dispatch() reports the
 *     masked value as the key state).
 *
 *   PM8150 GPIO block  SID 0, base 0x0c00, one 0x100-byte page per GPIO
 *     (pinctrl-spmi-gpio.c: PMIC_GPIO_ADDRESS_RANGE = 0x100, pad->base =
 *      start + i * 0x100, PMIC_GPIO_PHYSICAL_OFFSET = 1)
 *     GPIO6 page 0x0c00 + 5 * 0x100 = 0x0c500
 *     GPIO7 page 0x0c00 + 6 * 0x100 = 0x0c600
 *     page + 0x10 = RT_STS, bit 0 = pin level (1 high, 0 low)
 *     page + 0x04 = type, expected 0x10 (PMIC_GPIO_TYPE)
 *     page + 0x05 = subtype (0x10 low voltage, 0x11 medium voltage)
 *     page + 0x40 = mode, bits [1:0] (0 = input)
 *     page + 0x42 = pull, bits [2:0] (0..3 = up, 4 = down, 5 = none)
 *     page + 0x46 = enable, bit 7
 *
 *   So the two GPIO status bytes this module decodes are 0x0c510 and
 *   0x0c610, and the identity byte read with them is 0x0c504 (GPIO6 type).
 *   gpio_keys treats the line as GPIO_ACTIVE_LOW, so a low level (bit 0
 *   clear) is a press; this module applies that inversion once, here.
 *
 * The module owns no MMIO, no SPMI transfer and no kernel object.  The host
 * tests drive it directly; bsd/dev/kebab_keys.c feeds it the four bytes from
 * the observer reads and owns the thread, the log and /dev/kebab_keys.
 */
#ifndef KEBAB_KEYS_H
#define KEBAB_KEYS_H

#ifdef KEBAB_KEYS_KERNEL
#include <sys/types.h>
#include <libkern/libkern.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

/* ------------------------------------------------------------- sources */

#define KEBAB_KEYS_PON_RT_STS          0x0810u
#define KEBAB_KEYS_GPIO_UP_RT_STS      0xc510u  /* PM8150 GPIO6 page + 0x10 */
#define KEBAB_KEYS_GPIO_DOWN_RT_STS    0xc610u  /* PM8150 GPIO7 page + 0x10 */
#define KEBAB_KEYS_GPIO_IDENT          0xc504u  /* PM8150 GPIO6 page + 0x04 */

#define KEBAB_KEYS_GPIO_TYPE           0x10u
#define KEBAB_KEYS_GPIO_LEVEL          0x01u
#define KEBAB_KEYS_PON_KPDPWR          0x01u
#define KEBAB_KEYS_PON_RESIN           0x02u

/* --------------------------------------------------------------- keys */

enum {
	KEBAB_KEYS_KEY_POWER = 0,
	KEBAB_KEYS_KEY_VOLUME_UP = 1,
	KEBAB_KEYS_KEY_VOLUME_DOWN = 2,
	KEBAB_KEYS_KEY_COUNT = 3
};

#define KEBAB_KEYS_MAX_EVENTS           3u
#define KEBAB_KEYS_DEBOUNCE_SAMPLES     2u
/* Ten seconds of one key held, independent of the poll interval. */
#define KEBAB_KEYS_STUCK_NS             10000000000ull

#define KEBAB_KEYS_EVENT_RESIN          0x01u

/* -------------------------------------------------------------- sample */

/*
 * One look at all four registers.  read_ok is the transport's answer: the
 * decoder never turns a failed transfer into a key state.
 */
typedef struct {
	uint8_t pon_rt;         /* 0x0810 */
	uint8_t gpio_up_rt;     /* 0xc510 */
	uint8_t gpio_down_rt;   /* 0xc610 */
	uint8_t gpio_ident;     /* 0xc504, expected 0x10 */
	int     read_ok;
} kebab_keys_sample;

typedef enum {
	KEBAB_KEYS_SAMPLE_OK = 0,
	KEBAB_KEYS_SAMPLE_ZERO,         /* every byte zero: bus silent, no key */
	KEBAB_KEYS_SAMPLE_BAD_IDENTITY, /* not a PM8150 GPIO page */
	KEBAB_KEYS_SAMPLE_READ_ERROR    /* transport failed */
} kebab_keys_sample_status;

/* --------------------------------------------------------------- event */

typedef struct {
	uint8_t  key;           /* KEBAB_KEYS_KEY_* */
	uint8_t  pressed;       /* 1 = press, 0 = release */
	uint8_t  flags;         /* KEBAB_KEYS_EVENT_RESIN */
	uint8_t  raw_pon;
	uint8_t  raw_gpio_up;
	uint8_t  raw_gpio_down;
	uint8_t  raw_ident;
	uint32_t seq;           /* per-state event sequence, starts at 0 */
	uint64_t timestamp_ns;
} kebab_keys_event;

/* --------------------------------------------------------------- state */

typedef struct {
	int      usable;        /* the caller has a reason to believe the source */
	int      down;          /* the debounced, reported state */
	int      candidate;     /* the state the last sample agreed on */
	uint8_t  candidate_count;
	uint64_t held_samples;  /* valid samples this press has been held */
	uint64_t pressed_since_ns;
	int      stuck;         /* held for KEBAB_KEYS_STUCK_NS */
	uint32_t presses;
	uint32_t releases;
} kebab_keys_key;

typedef struct {
	kebab_keys_key key[KEBAB_KEYS_KEY_COUNT];
	uint64_t samples;       /* valid samples fed */
	uint64_t invalid;       /* rejected samples (zero/identity/transport) */
	uint32_t seq;
} kebab_keys_state;

/* ------------------------------------------------------------- calls */

void kebab_keys_reset(kebab_keys_state *state);
void kebab_keys_enable(kebab_keys_state *state, unsigned key, int usable);
const char *kebab_keys_name(unsigned key);
kebab_keys_sample_status kebab_keys_classify(const kebab_keys_sample *sample);
size_t kebab_keys_feed(kebab_keys_state *state, const kebab_keys_sample *sample,
    uint64_t now_ns, kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS]);
int kebab_keys_pressed(const kebab_keys_state *state, unsigned key);

#endif /* KEBAB_KEYS_H */
