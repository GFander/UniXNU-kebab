#ifndef _KEBAB_REBOOT_H
#define _KEBAB_REBOOT_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resident C reset policy. Init maps IMEM in normal context; later panic
 * calls use only that mapping and the pre-mapped unlocked PON core.
 * Callers release transport gates before reset/flush. Normal callers perform
 * filesystem sync first; panic callers must skip it. No service ownership. */
void kebab_reboot_init(void);
int kebab_reboot_plain(void);
void kebab_reboot_fastboot(bool use_reset2);
bool kebab_reboot_armed(void);
void kebab_reboot_psci_report(void);
void kebab_reboot_psci_negative_report(void);
void kebab_reboot_imem_report(void);
#ifdef __cplusplus
}
#endif
#endif
