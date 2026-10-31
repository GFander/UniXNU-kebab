/* Arbiter implementation only; not a PMIC client interface. */
#ifndef _KEBAB_SPMI_BUS_PRIVATE_H
#define _KEBAB_SPMI_BUS_PRIVATE_H
/* ------------------------------------------------------------ board facts */

#define KEBAB_SPMI_CORE_PA      0x0c440000u
#define KEBAB_SPMI_CORE_SIZE    0x00001100u
#define KEBAB_SPMI_OBSRV_PA     0x0e600000u
#define KEBAB_SPMI_OBSRV_SIZE   0x00100000u
#define KEBAB_SPMI_CNFG_PA      0x0c40a000u
#define KEBAB_SPMI_CNFG_SIZE    0x00026000u
/* The whole read/write window is 32 MiB; only one APID's slice is mapped. */
#define KEBAB_SPMI_CHNLS_PA     0x0c600000u
#define KEBAB_SPMI_CHNLS_SIZE   0x02000000u
#define KEBAB_SPMI_CHNL_SLICE   0x00001000u

#define KEBAB_SPMI_EE           0u
#define KEBAB_SPMI_CHANNEL      0u      /* qcom,channel; not used for reads */

/* ---------------------------------------------------------- v5 register map */

#define KEBAB_SPMI_VERSION      0x0000u
#define KEBAB_SPMI_APID_MAP     0x0900u
#define KEBAB_SPMI_APID_MAX     512u    /* PMIC_ARB_MAX_PERIPHS */
#define KEBAB_SPMI_OWNERSHIP    0x0700u

#define KEBAB_SPMI_CMD          0x0000u
#define KEBAB_SPMI_STATUS       0x0008u
#define KEBAB_SPMI_WDATA0       0x0010u
#define KEBAB_SPMI_RDATA0       0x0018u

#define KEBAB_SPMI_STATUS_DONE      (1u << 0)
#define KEBAB_SPMI_STATUS_FAILURE   (1u << 1)
#define KEBAB_SPMI_STATUS_DENIED    (1u << 2)
#define KEBAB_SPMI_STATUS_DROPPED   (1u << 3)

#define KEBAB_SPMI_OP_READ      15u     /* PMIC_ARB_OP_READ */
#define KEBAB_SPMI_OP_WRITEL    0u      /* PMIC_ARB_OP_EXT_WRITEL */

/* One poll per microsecond for a millisecond, like PMIC_ARB_TIMEOUT_US. */
#define KEBAB_SPMI_TIMEOUT_US   1000u

/* PM8150 PON, SID 0: PPID is (sid << 8) | (addr >> 8). */
#define KEBAB_SPMI_PON_SID      0u
#define KEBAB_SPMI_PON_PPID     0x008u
#define KEBAB_SPMI_PON_SUBTYPE  0x0805u
#define KEBAB_SPMI_PON_SPARE    0x088fu

/*
 * The PM8150 key sources read by bsd/dev/kebab_keys.c.  Power is the PON's
 * real-time status at 0x0810 (base 0x0800 + 0x10).  The volume keys are
 * PM8150 GPIO6 and GPIO7, whose pages start at 0x0c500 and 0x0c600
 * (0x0c000 + (n - 1) * 0x100); each page's real-time status is +0x10 and its
 * type register is +0x04.  The type byte is part of every sample so an
 * all-zero bus read cannot be mistaken for both active-low volume keys.
 *
 * Vendor evidence is in package/KebabKeys.h and docs/BRING-UP-STATE.md
 * section 71: pm8150.dtsi, kona-pmic-overlay.dtsi, kebab-oem.dtsi,
 * qpnp-power-on.c and pinctrl-spmi-gpio.c.
 */
#define KEBAB_SPMI_KEYS_PON_RT      0x0810u
#define KEBAB_SPMI_KEYS_GPIO6_RT    0x0c510u
#define KEBAB_SPMI_KEYS_GPIO7_RT    0x0c610u
#define KEBAB_SPMI_KEYS_GPIO6_TYPE  0x0c504u

/*
 * The pin setup of the same two pages, read once at start-up: mode 0x40,
 * pull 0x42, enable 0x46 (pinctrl-spmi-gpio.c).  The vendor state is an
 * enabled input with a pull-up; this kernel never writes the PMIC, so the
 * keys driver reports the registers and withholds a key whose pin is not
 * configured that way.
 */
#define KEBAB_SPMI_KEYS_GPIO6_MODE  0x0c540u
#define KEBAB_SPMI_KEYS_GPIO6_PULL  0x0c542u
#define KEBAB_SPMI_KEYS_GPIO6_EN    0x0c546u
#define KEBAB_SPMI_KEYS_GPIO7_MODE  0x0c640u
#define KEBAB_SPMI_KEYS_GPIO7_PULL  0x0c642u
#define KEBAB_SPMI_KEYS_GPIO7_EN    0x0c646u

/* qpnp_pon_set_restart_reason(), Gen2 layout. */
#define KEBAB_SPMI_PON_REASON_MASK   0xfeu
#define KEBAB_SPMI_PON_REASON_BOOTLOADER 0x02u
#define KEBAB_SPMI_PON_REASON_FIELD  (KEBAB_SPMI_PON_REASON_BOOTLOADER << 1)

/*
 * A PPID that belongs to the AP block rather than to a PMIC, used as a
 * negative control for the mapping walk.  No PMIC on this board has SID 0x0F,
 * so PPID 0xF00 must not resolve; if it does, the walk is reading something
 * other than the mapping table and none of its answers are trustworthy.
 */
#define KEBAB_SPMI_NEGATIVE_PPID 0xf00u

/* A PPID-to-observer-APID answer kept across polls (see kebab_spmi_reader). */
struct kebab_spmi_reader {
	uint32_t ppid;
	uint32_t apid;
	bool     valid;
};

#define KEBAB_SPMI_READERS      4u

struct kebab_spmi_bus_state {
	vm_offset_t core;
	vm_offset_t obsrvr;
	vm_offset_t cnfg;
	vm_offset_t chnl;              /* one 4 KiB read/write channel slice */
	uint32_t    version;           /* arbiter version read once in init */
	uint32_t    chnl_apid;
	uint32_t    read_attempt;
	bool        mapped;
	bool        attempted;
	bool        verbose;            /* kebab_pon_verbose: raw register traffic */
	bool        chnl_mapped;
	struct kebab_spmi_reader readers[KEBAB_SPMI_READERS];
};

#define spmi kebab_spmi_bus





#endif
