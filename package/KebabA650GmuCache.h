/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_CACHE_H
#define KEBAB_A650_GMU_CACHE_H
#include <stdint.h>
typedef struct {
 uint32_t magic,schema,bytes,result,valid_mask,expected_wimg,cpu_page_bytes,page_count;
 uint32_t wimg[3],pool_index[3],page_offset[3],async_hfi_ready;
} KebabA650GmuCacheRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabA650GmuCacheRecord)==72,"A650 shared cache record v1");
extern "C" {
#endif
int kebab_a650_gmu_backing_cache_snapshot(KebabA650GmuCacheRecord *);
#ifdef __cplusplus
}
#endif
#endif
