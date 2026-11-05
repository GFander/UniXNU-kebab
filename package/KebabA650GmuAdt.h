/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_ADT_H
#define KEBAB_A650_GMU_ADT_H
#include <stdint.h>
enum {KAGADT_NOT_RUN=0,KAGADT_WORKER=1,KAGADT_BACKING_STATE=2,KAGADT_LOOKUP=3,
 KAGADT_PROPERTY=4,KAGADT_LENGTH=5,KAGADT_POINTER=6,KAGADT_STAGE=7,KAGADT_DONE=8};
typedef struct {
 uint32_t magic,schema,bytes;int32_t result;
 int32_t lookup_status,property_status,stage_status;uint32_t reason;
 uint32_t property_bytes,current_secure_dt,stage_called,firmware_started;
} KebabA650GmuAdtRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabA650GmuAdtRecord)==48,"A650 current SecureDT source v1");
extern "C" {
#endif
int kebab_a650_gmu_backing_stage_current_adt(KebabA650GmuAdtRecord *);
#ifdef __cplusplus
}
#endif
#endif
