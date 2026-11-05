/* SPDX-License-Identifier: MIT */
/* Append AFTER the sealed TCM implementation in the actual backing TU. */
#include "KebabA650GmuShared.h"
static int kagb_hfi_gate_locked(const KebabA650GmuBackingToken *token)
{
 int e=kagb_token_locked(token);if(e)return e;
 if(token!=kagb_tcm_identity || token->generation!=kagb_tcm_generation ||
    !kagb_holder[token->slot].published || kagb_closing || kagb_record.phase!=KAGB_STAGED ||
    !kagb_domain.pool[5].handle || !kagb_domain.pool[5].prepared || !kagb_domain.pool[5].address)return KG_STATE;
 return kagb_pins_locked(); /* Includes actual WIMG6 for all shared CPU pages. */
}
extern "C" int kebab_a650_gmu_hfi_read(const KebabA650GmuBackingToken *token,uint32_t offset,uint32_t *out)
{
 if(!out)return KG_INVALID;*out=0;if((offset&3) || offset>=0x5000)return KG_INVALID;
 int e=KGNativeWorker(NULL);if(e)return e;if(!KGTry(&kagb_domain))return KG_BUSY;
 e=kagb_hfi_gate_locked(token);
 if(!e)*out=*(volatile const uint32_t *)((uintptr_t)kagb_domain.pool[5].address+offset);
 KGUnlock(&kagb_domain);return e;
}
extern "C" int kebab_a650_gmu_hfi_write(const KebabA650GmuBackingToken *token,uint32_t offset,uint32_t value)
{
 /* CPU producer owns command data/write_index; CPU consumer owns response
  * and debug read_index. Firmware-owned indices are NEVER written here. */
 if((offset&3) || !((offset>=224 && offset<224+4096) || offset==68 || offset==112 || offset==160))return KG_INVALID;
 if((offset==68 || offset==112 || offset==160) && (value>=1024 || (value&3)))return KG_INVALID;
 int e=KGNativeWorker(NULL);if(e)return e;if(!KGTry(&kagb_domain))return KG_BUSY;
 e=kagb_hfi_gate_locked(token);
 if(!e)*(volatile uint32_t *)((uintptr_t)kagb_domain.pool[5].address+offset)=value;
 KGUnlock(&kagb_domain);return e;
}
extern "C" int kebab_a650_gmu_hfi_barrier(const KebabA650GmuBackingToken *token)
{
 int e=KGNativeWorker(NULL);if(e)return e;if(!KGTry(&kagb_domain))return KG_BUSY;e=kagb_hfi_gate_locked(token);
 if(!e){
#if defined(__aarch64__)
  __asm__ volatile("dsb sy" ::: "memory");
#else
  OSMemoryBarrier();
#endif
 }
 KGUnlock(&kagb_domain);return e;
}
