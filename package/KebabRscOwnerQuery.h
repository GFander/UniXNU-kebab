/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_RSC_OWNER_QUERY_H
#define KEBAB_RSC_OWNER_QUERY_H
#include "KebabRscOwnershipQuery.h"
/* Same native global software owner as the existing touch VRM sender.
 * This evidence is not an ARC lease or proof that firmware never writes. */
typedef struct {
 uint32_t magic,schema,bytes,result,same_native_owner,sequence;
 uint32_t owner_dead,owner_used,owner_used_mask,channel_busy,rail_lease_mask;
 KebabRscQueryRecord rsc;
} KebabRscOwnerQueryRecord;
#ifdef __cplusplus
extern "C" {
#endif
int kebab_rpmh_owner_query(const KebabRscQueryIo *,KebabRscOwnerQueryRecord *);
#ifdef __cplusplus
}
#endif
#endif
