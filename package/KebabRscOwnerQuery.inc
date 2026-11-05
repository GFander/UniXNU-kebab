/* SPDX-License-Identifier: MIT */
/* Include ONCE at the end of the canonical KebabRpmhVrm.c, then sync its
 * generated kernel copy. It intentionally refers to the existing module's
 * static kebab_rpmh_global_lock. A separate translation unit/lock is wrong.
 * Read-only: never changes dead, used or used_mask; only claims busy while
 * sampling. Existing sender atomics serialize against exactly this busy. */
#if defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabRscOwnerQuery.h>
#else
#include "KebabRscOwnerQuery.h"
#endif
static uint32_t kebab_rpmh_query_sequence;
int kebab_rpmh_owner_query(const KebabRscQueryIo *io,KebabRscOwnerQueryRecord *out)
{
 int result;
 if(!out)return 22;
 memset(out,0,sizeof *out);out->magic=0x4f515352;out->schema=1;out->bytes=sizeof *out;out->result=22;
 if(!io || !io->read32 || !io->barrier)return 22;
 if(__atomic_exchange_n(&kebab_rpmh_global_lock.busy,1,__ATOMIC_ACQ_REL)!=0) {
  out->channel_busy=1;out->result=16;return 16;
 }
 out->same_native_owner=1;
 out->owner_dead=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.dead,__ATOMIC_ACQUIRE);
 out->owner_used=(uint32_t)__atomic_load_n(&kebab_rpmh_global_lock.used,__ATOMIC_ACQUIRE);
 out->owner_used_mask=__atomic_load_n(&kebab_rpmh_global_lock.used_mask,__ATOMIC_ACQUIRE);
 /* Zero remains unknown/unheld even if current hardware votes are high. */
 out->rail_lease_mask=0;
 if(kebab_rpmh_query_sequence==UINT32_MAX)result=75;
 else {out->sequence=++kebab_rpmh_query_sequence;result=KebabRscQuery(io,&out->rsc);}
 out->result=(uint32_t)result;
 __atomic_store_n(&kebab_rpmh_global_lock.busy,0,__ATOMIC_RELEASE);
 return result;
}
