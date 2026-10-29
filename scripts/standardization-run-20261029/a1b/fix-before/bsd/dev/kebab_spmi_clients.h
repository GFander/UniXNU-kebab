#ifndef _KEBAB_SPMI_CLIENTS_H
#define _KEBAB_SPMI_CLIENTS_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Keep legacy entry points. _locked rail read is internal to the serialized
 * board power sequence. Panic may call only the original unlocked PON core. */
void kebab_spmi_init(void);
void
kebab_spmi_report(void);
bool
kebab_spmi_pon_writes_enabled(void);
int
kebab_spmi_pon_reason_write(void);
void
kebab_spmi_pon_report(void);
int
kebab_spmi_pon_reason_write_normal(void);
void
kebab_spmi_test_panic_lock(void);
void
kebab_spmi_pon_prepare(void);
int
kebab_spmi_keys_sample(uint8_t raw[4]);
int
kebab_spmi_keys_config(uint8_t gpio6[3], uint8_t gpio7[3]);
int
kebab_spmi_rail_enable_read_locked(const char *rail, const char *peripheral,
    uint8_t *out);
void
kebab_spmi_power_report(void);
bool
kebab_spmi_vrm_writes_enabled(void);
void
kebab_spmi_vrm_init(void);
void
kebab_tcs_report(void);
int
kebab_spmi_vrm_enable(const char *rail);
int
kebab_spmi_vrm_boot_power(void);
void
kebab_rpmh_report(void);
#ifdef __cplusplus
}
#endif
#endif
