/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_SHARED_H
#define KEBAB_A650_GMU_SHARED_H
#include "KebabA650GmuNativeBacking.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Offsets within the already-published 20KiB HFI region, never CPU pointers.
 * These APIs alone grant no clock, firmware-execution or namespace lease. */
int kebab_a650_gmu_hfi_read(const KebabA650GmuBackingToken *,uint32_t offset,uint32_t *);
int kebab_a650_gmu_hfi_write(const KebabA650GmuBackingToken *,uint32_t offset,uint32_t value);
int kebab_a650_gmu_hfi_barrier(const KebabA650GmuBackingToken *);
#ifdef __cplusplus
}
#endif
#endif
