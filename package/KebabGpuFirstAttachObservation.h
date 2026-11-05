/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_FIRST_ATTACH_OBSERVATION_H
#define KEBAB_GPU_FIRST_ATTACH_OBSERVATION_H
#include <stdint.h>
#include <string.h>
typedef struct {
 uint32_t magic,schema,bytes,result,state,attempts,source,receipt_bytes,discovery_bytes,snapshot_result;
 uint32_t receipt_words[36],discovery_words[408];
} KebabGpuFirstAttachObservationRecord;
static inline void KebabGpuFirstAttachObservationInit(KebabGpuFirstAttachObservationRecord *r,uint32_t result)
{
 memset(r,0,sizeof *r);r->magic=0x314f464b;r->schema=1;r->bytes=1816;r->result=result;r->state=2;r->attempts=1;
 r->source=25;r->receipt_bytes=144;r->discovery_bytes=1632;
 r->receipt_words[0]=0x3141464b;r->receipt_words[1]=1;r->receipt_words[2]=144;r->receipt_words[3]=result;
}
#ifdef __cplusplus
static_assert(sizeof(KebabGpuFirstAttachObservationRecord)==1816,"CAP25 fixed observation ABI");
extern "C" {
#endif
int kebab_gpu_first_attach_cold(void);
int kebab_gpu_first_observation_copy(KebabGpuFirstAttachObservationRecord *);
#ifdef __cplusplus
}
#endif
#endif
