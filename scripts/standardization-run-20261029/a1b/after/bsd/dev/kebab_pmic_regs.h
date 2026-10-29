/* PM8150 board register facts retained from kebab_spmi.c: vendor DT,
 * qpnp-power-on.c and pinctrl-spmi-gpio.c. These are PMIC addresses, not VAs. */
#ifndef _KEBAB_PMIC_REGS_H
#define _KEBAB_PMIC_REGS_H
#define KEBAB_SPMI_PON_SID      0u
#define KEBAB_SPMI_PON_PPID     0x008u
#define KEBAB_SPMI_PON_SUBTYPE  0x0805u
#define KEBAB_SPMI_PON_SPARE    0x088fu
#define KEBAB_SPMI_KEYS_PON_RT      0x0810u
#define KEBAB_SPMI_KEYS_GPIO6_RT    0x0c510u
#define KEBAB_SPMI_KEYS_GPIO7_RT    0x0c610u
#define KEBAB_SPMI_KEYS_GPIO6_TYPE  0x0c504u
#define KEBAB_SPMI_KEYS_GPIO6_MODE  0x0c540u
#define KEBAB_SPMI_KEYS_GPIO6_PULL  0x0c542u
#define KEBAB_SPMI_KEYS_GPIO6_EN    0x0c546u
#define KEBAB_SPMI_KEYS_GPIO7_MODE  0x0c640u
#define KEBAB_SPMI_KEYS_GPIO7_PULL  0x0c642u
#define KEBAB_SPMI_KEYS_GPIO7_EN    0x0c646u
#define KEBAB_SPMI_PON_REASON_MASK   0xfeu
#define KEBAB_SPMI_PON_REASON_BOOTLOADER 0x02u
#define KEBAB_SPMI_PON_REASON_FIELD  (KEBAB_SPMI_PON_REASON_BOOTLOADER << 1)
#endif
