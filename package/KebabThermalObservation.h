/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_THERMAL_OBSERVATION_H
#define KEBAB_THERMAL_OBSERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
    uint32_t control,trdy,sensor_mask,valid_mask;
    uint32_t status[16];int32_t milli_celsius[16];
} KebabThermalObservation;
typedef struct {
    void *cookie;size_t bytes[2];int (*read32)(void *,unsigned,size_t,uint32_t *);
} KebabThermalReadIo;
enum { KebabThermalArgument=1,KebabThermalIoError,KebabThermalDisabled,
       KebabThermalNotReady,KebabThermalUnstable,KebabThermalNoValidReadings };
/* Source facts: local clean LineageOS tsens2xxx.c/tsens.h (qcom,tsens24xx).
 * SROT+0 is VERSION; CTRL is +4, explicitly used by vendor hw_init and
 * sensor_en. The supplied SROT span includes both words (8 bytes), despite
 * the old vendor DT describing only the base word. No writes or probing.
 * Read-only inherited configuration. Callback owns mapped SROT/TM resources.
 * No enabling/reset, SCM call, IRQ acknowledgement, threshold or watchdog
 * change. Strict valid-bit sampling; stable invalid codes remain unknown.
 * Disabled/not-ready results retain diagnostic control values. IO failure
 * or a changed enable state leaves out zero; success is not thermal control.
 */
static int kebab_thermal_read(const KebabThermalReadIo *io,unsigned region,size_t off,uint32_t *v)
{
    return region>1 || (off&3) || off>io->bytes[region] || io->bytes[region]-off<4 || io->read32(io->cookie,region,off,v);
}
static int32_t KebabThermalDecode(uint32_t status)
{
    int32_t code=(int32_t)(status&0xfff);if(code&0x800)code-=4096;
    return code*100;
}
static int KebabThermalObserve(const KebabThermalReadIo *io,KebabThermalObservation *out)
{
    if(out)memset(out,0,sizeof *out);
    if(!io || !out || !io->read32 || io->bytes[0]<8 || io->bytes[1]<0xe8)return KebabThermalArgument;
    KebabThermalObservation s={0};uint32_t after;
    if(kebab_thermal_read(io,0,4,&s.control))return KebabThermalIoError;
    s.sensor_mask=(s.control>>3)&0xffff;
    if(!(s.control&1)){*out=s;return KebabThermalDisabled;}
    if(kebab_thermal_read(io,1,0xe4,&s.trdy))return KebabThermalIoError;
    if(!(s.trdy&8)){*out=s;return KebabThermalNotReady;}
    for(unsigned i=0;i<16;i++)if(s.sensor_mask&(1u<<i)) {
        if(kebab_thermal_read(io,1,0xa0+4*i,&s.status[i]))return KebabThermalIoError;
        if(s.status[i]&(1u<<21)) {
            s.valid_mask|=1u<<i;s.milli_celsius[i]=KebabThermalDecode(s.status[i]);
        }
    }
    if(kebab_thermal_read(io,0,4,&after))return KebabThermalIoError;
    if(((s.control^after)&0x7fff9u)!=0)return KebabThermalUnstable;
    if(kebab_thermal_read(io,1,0xe4,&after))return KebabThermalIoError;
    if(!(after&8))return KebabThermalUnstable;
    *out=s;return s.valid_mask?0:KebabThermalNoValidReadings;
}
#endif
