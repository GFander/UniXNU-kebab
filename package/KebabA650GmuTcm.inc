/* SPDX-License-Identifier: MIT */
#include "KebabA650GmuTcm.h"
static KebabA650GmuPlan kagb_tcm_plan;
static const KebabA650GmuBackingToken *kagb_tcm_identity;
static uint64_t kagb_tcm_generation;
extern "C" int kebab_a650_gmu_tcm_check(const KebabA650GmuBackingToken *token)
{
 int e=KGNativeWorker(NULL);if(e)return e;if(!KGTry(&kagb_domain))return KG_BUSY;
 e=kagb_token_locked(token);
 if(!e && (token!=kagb_tcm_identity || token->generation!=kagb_tcm_generation || !kagb_holder[token->slot].published))e=KG_STATE;
 if(!e)e=kagb_pins_locked();KGUnlock(&kagb_domain);return e;
}
extern "C" int kebab_a650_gmu_tcm_begin(KebabA650GmuBackingToken *token,KebabA650GmuTcmManifest *out)
{
 if(!out)return KG_INVALID;memset(out,0,sizeof(*out));out->magic=0x314d544b;out->schema=1;out->bytes=sizeof(*out);
 int e=KGNativeWorker(NULL);if(e){out->result=e;return e;}
 if(!KGTry(&kagb_domain)){out->result=KG_BUSY;return KG_BUSY;}
 e=kagb_token_locked(token);if(!e)e=kagb_pins_locked();
 if(!e && kagb_tcm_identity)e=KG_STATE;
 if(!e)e=KebabA650GmuBoundPlan(kagb_domain.pool[7].address,41548,&kagb_tcm_plan,NULL);
 if(!e && kagb_tcm_plan.tcm_bytes!=18404)e=KG_UNSUPPORTED;
 if(!e){
  kagb_tcm_identity=token;kagb_tcm_generation=token->generation;
  out->word_count=8192;out->payload_words=4601;out->image_bytes=41548;out->tcm_bytes=32768;
  out->core_version=kagb_tcm_plan.core_version;out->hfi_version=kagb_tcm_plan.hfi_version;out->generation=token->generation;
 }
 out->result=e;KGUnlock(&kagb_domain);return e;
}
extern "C" int kebab_a650_gmu_tcm_word(const KebabA650GmuBackingToken *token,uint32_t ordinal,uint32_t *offset,uint32_t *value)
{
 if(!offset || !value)return KG_INVALID;*offset=*value=0;
 int e=KGNativeWorker(NULL);if(e)return e;if(ordinal>=8192)return KG_INVALID;
 if(!KGTry(&kagb_domain))return KG_BUSY;
 e=kagb_token_locked(token);
 if(!e && (token!=kagb_tcm_identity || token->generation!=kagb_tcm_generation ||
    !kagb_holder[token->slot].published || kagb_closing || kagb_record.phase!=KAGB_STAGED ||
    !kagb_domain.pool[7].handle || !kagb_domain.pool[7].prepared || !kagb_domain.pool[7].address))e=KG_STATE;
 if(!e){
  const uint32_t address=ordinal<4096?ordinal*4:0x10004000+(ordinal-4096)*4;
  uint32_t word=0;
  for(uint32_t i=0;i<kagb_tcm_plan.blocks;i++){
   const KebabA650GmuBlock *p=&kagb_tcm_plan.block[i];
   if(p->region<6 || !p->bytes || address<p->address || (uint64_t)address+4>(uint64_t)p->address+p->bytes)continue;
   word=KebabA650GmuU32((const uint8_t *)kagb_domain.pool[7].address+p->image_offset+address-p->address);break;
  }
  /* All 32KiB are owned TCM. Unspecified bytes are deterministically zero;
   * no inherited TCM holes are left behind after this cold load. */
  *offset=ordinal<4096?0x3000+ordinal*4:0x7000+(ordinal-4096)*4;*value=word;
 }
 KGUnlock(&kagb_domain);return e;
}
