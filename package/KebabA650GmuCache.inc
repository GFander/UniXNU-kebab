/* SPDX-License-Identifier: MIT */
#include "KebabA650GmuCache.h"
extern "C" int kebab_a650_gmu_backing_cache_snapshot(KebabA650GmuCacheRecord *out)
{
 if(!out)return KG_INVALID;memset(out,0,sizeof(*out));out->magic=0x3143474b;out->schema=1;out->bytes=sizeof(*out);
 out->expected_wimg=VM_WIMG_WCOMB;out->cpu_page_bytes=PAGE_SIZE;
 int e=KGNativeWorker(NULL);if(e){out->result=e;return e;}
 if(__atomic_load_n(&kagb_prepare_state,__ATOMIC_ACQUIRE)!=2 || kagb_prepare_result){out->result=KG_STATE;return KG_STATE;}
 if(!KGTry(&kagb_domain)){out->result=KG_BUSY;return KG_BUSY;}
 if(kagb_closing || (kagb_record.phase!=KAGB_PREPARED && kagb_record.phase!=KAGB_STAGED) ||
    kagb_domain.receipt.state!=KG_PREPARED || !kagb_domain.pool[5].handle || !kagb_domain.pool[6].handle){KGUnlock(&kagb_domain);out->result=KG_STATE;return KG_STATE;}
 if(PAGE_SIZE!=16384 || kagb_domain.pool[5].bytes!=32768 || kagb_domain.pool[6].bytes!=16384){KGUnlock(&kagb_domain);out->result=KG_UNSUPPORTED;return KG_UNSUPPORTED;}
 const uint32_t index[3]={5,5,6},offset[3]={0,16384,0};e=0;
 for(uint32_t i=0;i<3;i++){
  out->pool_index[i]=index[i];out->page_offset[i]=offset[i];
  out->wimg[i]=pmap_cache_attributes((ppnum_t)((kagb_domain.pool[index[i]].pa+offset[i])>>PAGE_SHIFT))&VM_WIMG_MASK;
  out->valid_mask|=1u<<i;out->page_count++;
  if(out->wimg[i]!=VM_WIMG_WCOMB)e=KG_POOL;
 }
 /* Cache-mode qualification alone is not a running HFI message protocol. */
 out->result=e;KGUnlock(&kagb_domain);return e;
}
