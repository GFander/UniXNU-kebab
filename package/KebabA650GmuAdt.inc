/* SPDX-License-Identifier: MIT */
#include <pexpert/device_tree.h>
#include "KebabA650GmuAdt.h"
/* Include once after KebabA650GmuNativeBacking.cpp's private owner body.
 * This resolves the current SecureDT on each invocation; no early DT pointer,
 * transport/user pointer, public byte dump or alternative property fallback.
 * The payload producer must include this property in the original full ADT
 * owner measurement. Lookup itself is not a cryptographic owner attestation. */
extern "C" int kebab_a650_gmu_backing_stage_current_adt(KebabA650GmuAdtRecord *out)
{
 if(!out)return KG_INVALID;memset(out,0,sizeof(*out));
 out->magic=0x3141474b;out->schema=1;out->bytes=sizeof(*out);
 out->lookup_status=out->property_status=out->stage_status=INT32_MIN;
 int e=KGNativeWorker(NULL);if(e){out->reason=KAGADT_WORKER;out->result=e;return e;}
 if(__atomic_load_n(&kagb_prepare_state,__ATOMIC_ACQUIRE)!=2 || kagb_prepare_result){out->reason=KAGADT_BACKING_STATE;out->result=KG_STATE;return KG_STATE;}
 if(!KGTry(&kagb_domain)){out->reason=KAGADT_BACKING_STATE;out->result=KG_BUSY;return KG_BUSY;}
 const int ready=!kagb_closing && kagb_record.phase==KAGB_PREPARED && kagb_domain.receipt.state==KG_PREPARED && !kagb_domain.touched && !kagb_domain.lease.held;
 KGUnlock(&kagb_domain);
 if(!ready){out->reason=KAGADT_BACKING_STATE;out->result=KG_STATE;return KG_STATE;}
 DTEntry entry=NULL;const void *property=NULL;unsigned int bytes=0;
 out->lookup_status=SecureDTLookupEntry(NULL,"/chosen",&entry);
 if(out->lookup_status!=kSuccess || !entry){out->reason=KAGADT_LOOKUP;out->result=KG_STATE;return KG_STATE;}
 out->property_status=SecureDTGetProperty(entry,"kebab-gmu-firmware",&property,&bytes);
 out->property_bytes=bytes;
 if(out->property_status!=kSuccess){out->reason=KAGADT_PROPERTY;out->result=KG_STATE;return KG_STATE;}
 out->current_secure_dt=1;
 if(bytes!=KebabA650GmuImageBytes){out->reason=KAGADT_LENGTH;out->result=KG_INVALID;return KG_INVALID;}
 if(!property){out->reason=KAGADT_POINTER;out->result=KG_INVALID;return KG_INVALID;}
 /* Stage takes exactly one copy of these bytes into its private 64KiB pool,
  * then hashes and consumes only that snapshot. Caller/source-buffer
  * stability after the copy is neither assumed nor asserted. */
 out->stage_called=1;e=kebab_a650_gmu_backing_stage(property,bytes);
 out->stage_status=e;out->result=e;out->reason=e?KAGADT_STAGE:KAGADT_DONE;return e;
}
