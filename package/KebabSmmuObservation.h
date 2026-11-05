/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_SMMU_OBSERVATION_H
#define KEBAB_SMMU_OBSERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
    uint32_t sid,matches,index,smr,s2cr,context_read,shared_or_broad;
    uint32_t sctlr,ttbcr,ttbr_low,ttbr_high,fsr;
} KebabSmmuStreamObservation;
typedef struct {
    uint32_t cr0,id0,id1,id2,groups,context_banks,page_shift;
    KebabSmmuStreamObservation stream[4];
} KebabSmmuObservation;
typedef struct { void *cookie;size_t bytes;int (*read32)(void *,size_t,uint32_t *); } KebabSmmuReadIo;
/* Read-only NS APPS SMMUv2 reference geometry. Caller owns accessible mapping
 * and live bus resources. No writes, ATS requests, TLB maintenance or readiness
 * claims. Context reads require a uniquely matched NS display stream and no
 * active unknown/secure stream sharing its CB. Split TTBR reads are not atomic.
 * These are observations, never permission to reuse a firmware context. */
static int kebab_smmu_read(const KebabSmmuReadIo *io,size_t off,uint32_t *v)
{
    return (off&3) || off>io->bytes || io->bytes-off<4 || io->read32(io->cookie,off,v);
}
static int kebab_smmu_valid(uint32_t id0,uint32_t smr,uint32_t s2cr)
{
    return !!((id0&0x100)?(s2cr&0x400):(smr&0x80000000u));
}
static int kebab_smmu_ns_only(uint32_t smr)
{
    uint32_t mask=(smr>>16)&0x7fff,id=smr&0xffff;
    return !(mask&~0x402u) && (id&~mask)==0x820;
}
static int KebabSmmuObserve(const KebabSmmuReadIo *io,KebabSmmuObservation *out)
{
    if(out)memset(out,0,sizeof *out);
    if(!io || !out || !io->read32 || io->bytes!=0x100000)return 1;
    KebabSmmuObservation s={0};uint32_t smr[255],s2cr[255];
    if(kebab_smmu_read(io,0,&s.cr0) || kebab_smmu_read(io,0x20,&s.id0) ||
       kebab_smmu_read(io,0x24,&s.id1) || kebab_smmu_read(io,0x28,&s.id2))return 2;
    s.groups=s.id0&0xff;s.context_banks=s.id1&0xff;s.page_shift=(s.id1&0x80000000u)?16:12;
    if(!(s.id0&(1u<<27)) || !s.groups || !s.context_banks ||
       (1ULL<<(s.page_shift+((s.id1>>28)&7)+1))!=io->bytes/2 ||
       ((uint64_t)s.context_banks<<s.page_shift)>io->bytes/2)return 3;
    for(unsigned i=0;i<s.groups;i++)
        if(kebab_smmu_read(io,0x800+4*i,&smr[i]) || kebab_smmu_read(io,0xc00+4*i,&s2cr[i]))return 2;
    const uint32_t sid[]={0x820,0x822,0xc20,0xc22};
    for(unsigned n=0;n<4;n++) {
        KebabSmmuStreamObservation *v=&s.stream[n];v->sid=sid[n];
        for(unsigned i=0;i<s.groups;i++) {
            uint32_t mask=(smr[i]>>16)&0x7fff;
            if(kebab_smmu_valid(s.id0,smr[i],s2cr[i]) && ((sid[n]^(smr[i]&0xffff))&~mask)==0) {
                v->matches++;v->index=i;v->smr=smr[i];v->s2cr=s2cr[i];
            }
        }
        if(v->matches!=1)continue;
        if(!kebab_smmu_ns_only(v->smr)) {v->shared_or_broad=1;continue;}
        uint32_t cb=v->s2cr&0xff;
        if(((v->s2cr>>16)&3)!=0 || cb>=s.context_banks)continue;
        for(unsigned i=0;i<s.groups;i++)
            if(kebab_smmu_valid(s.id0,smr[i],s2cr[i]) && ((s2cr[i]>>16)&3)==0 &&
               (s2cr[i]&0xff)==cb && !kebab_smmu_ns_only(smr[i]))v->shared_or_broad=1;
        if(v->shared_or_broad)continue;
        size_t base=io->bytes/2+((size_t)cb<<s.page_shift);
        if(kebab_smmu_read(io,base,&v->sctlr) || kebab_smmu_read(io,base+0x30,&v->ttbcr) ||
           kebab_smmu_read(io,base+0x20,&v->ttbr_low) || kebab_smmu_read(io,base+0x24,&v->ttbr_high) ||
           kebab_smmu_read(io,base+0x58,&v->fsr))return 2;
        v->context_read=1;
    }
    /* Detect changed global and stream configuration. Fault status may change
     * naturally; no clear-on-read or clear-fault operation is used. */
    uint32_t check;
    if(kebab_smmu_read(io,0,&check))return 2;if(check!=s.cr0)return 4;
    for(unsigned i=0;i<s.groups;i++) {
        if(kebab_smmu_read(io,0x800+4*i,&check))return 2;if(check!=smr[i])return 4;
        if(kebab_smmu_read(io,0xc00+4*i,&check))return 2;if(check!=s2cr[i])return 4;
    }
    *out=s;return 0;
}
#endif
