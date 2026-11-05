/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_TCM_H
#define KEBAB_A650_GMU_TCM_H
#include "KebabA650GmuNativeBacking.h"
typedef struct {
 uint32_t magic,schema,bytes,result,word_count,payload_words,image_bytes,tcm_bytes,core_version,hfi_version;
 uint64_t generation;
} KebabA650GmuTcmManifest;
#ifdef __cplusplus
static_assert(sizeof(KebabA650GmuTcmManifest)==48,"A650 private TCM source manifest v1");
extern "C" {
#endif
/* Kernel-private producer APIs, never a CAP/ioctl byte accessor. Acquire the
 * real backing token first. Begin verifies the private image; publish that
 * same token before Word. No CPU pointer or arbitrary address is exported. */
int kebab_a650_gmu_tcm_begin(KebabA650GmuBackingToken *,KebabA650GmuTcmManifest *);
int kebab_a650_gmu_tcm_check(const KebabA650GmuBackingToken *);
int kebab_a650_gmu_tcm_word(const KebabA650GmuBackingToken *,uint32_t ordinal,uint32_t *gmu_offset,uint32_t *value);
#ifdef __cplusplus
}
#endif
#endif
