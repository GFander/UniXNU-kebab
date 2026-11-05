/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_BRIGHTNESS_RECORD_H
#define KEBAB_BRIGHTNESS_RECORD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabBacklightNits.h"
typedef struct {uint32_t brightness_present,limit_present,brightness,limit;} KebabBrightnessRecord;
/* Fixed20H392 _kern_SwapSetBrightness writes flag/context+0x2ff and double
 * context+0x304; SwapEnd passes context+0x18,1132bytes. Limit is flag+0x300,
 * double+0x30c. Kernel uses integer IEEE754 decoding; no FP state is touched. */
static int kebab_brightness_level(uint64_t bits,uint32_t *out)
{
 return kebab_backlight_double_nits_level(bits,out);
}
static int KebabBrightnessRecordDecode(const void *record,size_t bytes,KebabBrightnessRecord *out)
{
 if(out)memset(out,0,sizeof *out);
 if(!record||bytes!=0x46c||!out)return 22;
 const uint8_t *p=(const uint8_t *)record;
 if(p[0x2e7]>1||p[0x2e8]>1)return 22;
 out->brightness_present=p[0x2e7];out->limit_present=p[0x2e8];uint64_t value;
 if(out->brightness_present){memcpy(&value,p+0x2ec,8);if(kebab_brightness_level(value,&out->brightness))return 22;}
 if(out->limit_present){memcpy(&value,p+0x2f4,8);if(kebab_brightness_level(value,&out->limit))return 22;}
 return 0;
}
#endif
