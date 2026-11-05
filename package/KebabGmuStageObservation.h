/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_STAGE_OBSERVATION_H
#define KEBAB_GMU_STAGE_OBSERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabA650GmuAdt.h"
#include "KebabA650GmuNativeBacking.h"
#include "KebabA650GmuCache.h"
/* CAP24 contains metadata only. No firmware, physical address or kernel token. */
typedef struct {
 uint32_t magic,schema,bytes,result,state,attempts,source,prepare_result;
 uint32_t adt_result,backing_snapshot_result,cache_snapshot_result,reserved;
 KebabA650GmuAdtRecord adt;
 KebabA650GmuNativeBackingRecord backing;
 KebabA650GmuCacheRecord cache;
} KebabGmuStageObservationRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabGmuStageObservationRecord)==296,"CAP24 fixed record");
static_assert(offsetof(KebabGmuStageObservationRecord,adt)==48,"CAP24 ADT");
static_assert(offsetof(KebabGmuStageObservationRecord,backing)==96,"CAP24 backing");
static_assert(offsetof(KebabGmuStageObservationRecord,cache)==224,"CAP24 cache");
#endif
static inline void KebabGmuStageObservationInit(KebabGmuStageObservationRecord *r,uint32_t result)
{
 memset(r,0,sizeof *r);r->magic=0x3154474b;r->schema=1;r->bytes=sizeof *r;
 r->result=result;r->state=2;r->attempts=1;r->source=24;
 r->prepare_result=r->adt_result=r->backing_snapshot_result=r->cache_snapshot_result=UINT32_MAX;
}
#endif
