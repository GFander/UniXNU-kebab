/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_BACKING_TYPES_H
#define KEBAB_A650_GMU_BACKING_TYPES_H
#include <stdint.h>
typedef struct {uint32_t result,image_verified,ram_staged,hfi_initialized,extent_count,mapped_pages,ram_bytes,tcm_bytes_pending,firmware_started,mmio_writes;uint8_t sha256[32];} KebabA650GmuBackingRecord;
#endif
