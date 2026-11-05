/* SPDX-License-Identifier: MIT */
#include "KebabDisplayObservation.h"
#include <string.h>
/* Facts independently bound to clean local LineageOS 7c2b1aed, kona-sde.dtsi,
 * dispcc-kona.c, gdsc-regulator.c and sde_hw_{intf,sspp}.c. No vendor bodies. */
static int read_reg(const KebabDisplayReadIo *io,unsigned region,size_t offset,uint32_t *v,KebabDisplayReadResult *r)
{
    ++r->reads;
    if(region>1 || (offset&3) || offset>io->bytes[region] || io->bytes[region]-offset<4 ||
       io->read32(io->cookie,region,offset,v)) {
        r->failed_region=region;r->failed_offset=offset;return 1;
    }
    return 0;
}
static int gate(const KebabDisplayReadIo *io,KebabDisplayObservation *s,KebabDisplayReadResult *r)
{
    if(read_reg(io,0,0x3000,&s->gdsc,r) || read_reg(io,0,0x2080,&s->ahb,r) ||
       read_reg(io,0,0x200c,&s->core,r))return KebabDisplayReadIoError;
    if(!(s->gdsc&0x80000000u) || (s->gdsc&1) || (s->ahb&0x80000000u) ||
       (s->core&0x80000000u))return KebabDisplayReadPowerUnavailable;
    return 0;
}
static int sample(const KebabDisplayReadIo *io,KebabDisplayObservation *s,KebabDisplayReadResult *r)
{
    static const size_t intf_base[]={0x6b800,0x6c000};
    static const size_t intf_offset[]={0x26c,0x000,0x0a8,0x084,0x0ac,0x0b0,0x298,0x2b0};
    static const size_t source_base[]={0x5000,0x7000,0x9000,0xb000,0x25000,0x27000,0x29000,0x2b000};
    static const size_t source_offset[]={0,0x14,0x24,0x30};
    int error=gate(io,s,r);if(error)return error;
    if(read_reg(io,1,0,&s->mdss_version,r) || read_reg(io,1,0x1000,&s->mdp_version,r) ||
       read_reg(io,1,0x1010,&s->irq_enabled,r) || read_reg(io,1,0x1014,&s->irq_pending,r))return KebabDisplayReadIoError;
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<8;j++)
        if(read_reg(io,1,intf_base[i]+intf_offset[j],&s->intf[i][j],r))return KebabDisplayReadIoError;
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)
        if(read_reg(io,1,source_base[i]+source_offset[j],&s->source[i][j],r))return KebabDisplayReadIoError;
    KebabDisplayObservation after={0};error=gate(io,&after,r);if(error)return error;
    if(s->gdsc!=after.gdsc || s->ahb!=after.ahb || s->core!=after.core)return KebabDisplayReadUnstable;
    return 0;
}
static int same(const KebabDisplayObservation *a,const KebabDisplayObservation *b)
{
    if(a->gdsc!=b->gdsc || a->ahb!=b->ahb || a->core!=b->core || a->mdss_version!=b->mdss_version ||
       a->mdp_version!=b->mdp_version || a->irq_enabled!=b->irq_enabled)return 0;
    for(unsigned i=0;i<2;i++) {
        if((a->intf[i][0]^b->intf[i][0])&1)return 0;
        for(unsigned j=1;j<4;j++)if(a->intf[i][j]!=b->intf[i][j])return 0;
    }
    return !memcmp(a->source,b->source,sizeof a->source);
}
int KebabDisplayObserve(const KebabDisplayReadIo *io,KebabDisplayObservation *out,KebabDisplayReadResult *r)
{
    if(out)memset(out,0,sizeof *out);if(r)memset(r,0,sizeof *r);
    if(!io || !out || !r || !io->read32 || io->bytes[0]<0x3004 || io->bytes[1]<0x6c2b4)return KebabDisplayReadArgument;
    for(unsigned attempt=1;attempt<=3;attempt++) {
        r->attempts=attempt;KebabDisplayObservation a={0},b={0};
        int error=sample(io,&a,r);if(error==KebabDisplayReadUnstable)continue;if(error)return error;
        error=sample(io,&b,r);if(error==KebabDisplayReadUnstable)continue;if(error)return error;
        if(!same(&a,&b))continue;
        *out=b;return KebabDisplayReadComplete;
    }
    return KebabDisplayReadUnstable;
}
int KebabDisplayNonsecureSid(uint32_t sid)
{
    return sid<=0xffffu && (sid&~0x402u)==0x820u;
}
int KebabDisplayIovaSpan(uint64_t address,uint64_t bytes,uint64_t granule)
{
    if(granule!=4096 && granule!=16384 && granule!=65536)return 0;
    return bytes && !(address&(granule-1)) && !(bytes&(granule-1)) &&
           address>=0x20000 && address<0x100000000ULL && bytes<=0x100000000ULL-address;
}
