/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_BACKLIGHT_NITS_H
#define KEBAB_BACKLIGHT_NITS_H
#include <stdint.h>
/* Pinned kebab DTS peak5400000 / Qualcomm binding10000 = nominal540nits.
 * Linear device-level control profile; not measured photometry, calibration
 * of the panel's nonlinear response, HBM or HDR enablement. */
#define KEBAB_BACKLIGHT_LEVEL_MAX 2047u
#define KEBAB_BACKLIGHT_NITS_MAX 540u
#define KEBAB_BACKLIGHT_NITS_Q16_MAX (540u*65536u)
static int kebab_backlight_nits_q16_level(uint64_t nits,uint32_t *level)
{
 if(!level||nits>KEBAB_BACKLIGHT_NITS_Q16_MAX)return 22;
 *level=(uint32_t)((nits*2047+KEBAB_BACKLIGHT_NITS_Q16_MAX/2)/KEBAB_BACKLIGHT_NITS_Q16_MAX);return 0;
}
static uint32_t kebab_backlight_level_millinits(uint32_t level)
{return level<=2047?(uint32_t)(((uint64_t)level*540000+1023)/2047):0;}
static int kebab_backlight_double_nits_level(uint64_t bits,uint32_t *level)
{
 if(!level||(bits>>63)||bits>0x4080e00000000000ULL)return 22; /* positive finite <=540 */
 unsigned exponent=(unsigned)(bits>>52);uint64_t mantissa=bits&0xfffffffffffffULL;
 if(exponent)mantissa|=1ULL<<52;
 unsigned shift=exponent?1059-exponent:1058;
 uint64_t q16=shift>=64?0:mantissa>>shift; /* Matches original fcvtzs nits,#16. */
 return kebab_backlight_nits_q16_level(q16,level);
}
#endif
