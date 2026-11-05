/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_SMMU_OBSERVATION_H
#define KEBAB_GPU_SMMU_OBSERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* CAP23 schema1: little-endian words, no pointer or executable control.
 * Payloads are exact byte copies of the sealed120B/1632B native structures.
 * No full static engine header is included in the framebuffer TU. */
typedef struct {
 uint32_t magic,schema,bytes,result,state,attempts,source,receipt_bytes,discovery_bytes,snapshot_result;
 uint32_t receipt_words[30],discovery_words[408];
} KebabGpuSmmuObservationRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabGpuSmmuObservationRecord)==1792,"CAP23 fixed record");
static_assert(offsetof(KebabGpuSmmuObservationRecord,receipt_words)==40,"CAP23 receipt offset");
static_assert(offsetof(KebabGpuSmmuObservationRecord,discovery_words)==160,"CAP23 discovery offset");
extern "C" {
#endif
int kebab_gpu_smmu_candidate_observe(void);
int kebab_gpu_smmu_candidate_observation_copy(KebabGpuSmmuObservationRecord *);
#ifdef __cplusplus
}
#endif
static inline void KebabGpuSmmuObservationInit(KebabGpuSmmuObservationRecord *r,uint32_t result)
{
 memset(r,0,sizeof *r);r->magic=0x314f5347;r->schema=1;r->bytes=sizeof *r;r->result=result;
 r->state=2;r->attempts=1;r->source=23;r->receipt_bytes=120;r->discovery_bytes=1632;
 r->receipt_words[0]=0x3153474b;r->receipt_words[1]=1;r->receipt_words[2]=120;r->receipt_words[3]=result;
 r->receipt_words[6]=6;r->receipt_words[7]=2; /* observer, GMU_KERNEL role; no lease */
}
#endif
