/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_RSC_OWNERSHIP_QUERY_H
#define KEBAB_RSC_OWNERSHIP_QUERY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* APPS RSC DRV2 only: CPU PA 0x18220000, TCS byte offset 0xd00,
 * eight ordinary TCSes (ACTIVE0/1, SLEEP2..4, WAKE5..7), 16 commands.
 * The CONTROL TCS is not a ninth ordinary TCS. No IRQ_CLEAR/read, writes,
 * DRV0/1, display RSC or arbitrary resource-address dereference exists.
 * RSC access is the existing CPU always-on native path, not a GPUCC gate.
 * Caller serializes with the EXISTING global RPMh owner before querying;
 * two equal samples alone never grant channel or resource ownership. */
enum {KebabRscQueryWords=4+8*84,KebabRscQueryOk=0,KebabRscQueryReadError=5,
      KebabRscQueryChanged=11,KebabRscQueryInvalid=22,KebabRscQueryGeometry=95};
typedef struct {
    void *cookie;
    int (*read32)(void *,uint32_t byte_offset,uint32_t *);
    int (*barrier)(void *);
} KebabRscQueryIo;
typedef struct {
    uint32_t magic,schema,bytes,result,reads,attempts,failed_offset,io_error;
    uint32_t valid_words[2],stable_words,clean_active_mask,occupied_tcs_mask;
    uint32_t irq_enable,irq_status,num_tcs,num_commands,ownership_granted;
    uint32_t sample[2][KebabRscQueryWords];
} KebabRscQueryRecord;
static inline int KebabRscQueryOffset(unsigned word,uint32_t *out)
{
    static const uint32_t global[4]={0,0x0c,0x0d00,0x0d04};
    static const uint32_t control[4]={0x10,0x14,0x18,0x1c};
    if(!out || word>=KebabRscQueryWords)return 0;
    if(word<4)*out=global[word];
    else {
        unsigned tcs=(word-4)/84,field=(word-4)%84;
        *out=0xd00+tcs*0x2a0;
        if(field<4)*out+=control[field];
        else *out+=0x30+((field-4)/5)*20+((field-4)%5)*4;
    }
    return 1;
}
static inline int KebabRscQueryAllowed(uint32_t offset)
{
    unsigned i;uint32_t at=0;
    for(i=0;i<KebabRscQueryWords;++i) {
        (void)KebabRscQueryOffset(i,&at);if(offset==at)return 1;
    }
    return 0;
}
static inline int KebabRscQuery(const KebabRscQueryIo *io,KebabRscQueryRecord *out)
{
    unsigned pass,word,tcs,command;uint32_t offset=0,value=0;int error;
    if(!out)return KebabRscQueryInvalid;
    memset(out,0,sizeof *out);out->magic=0x51534352;out->schema=1;out->bytes=sizeof *out;
    out->result=KebabRscQueryInvalid;out->failed_offset=UINT32_MAX;
    if(!io || !io->read32 || !io->barrier)return KebabRscQueryInvalid;
    for(pass=0;pass<2;++pass) {
        for(word=0;word<KebabRscQueryWords;++word) {
            (void)KebabRscQueryOffset(word,&offset);++out->attempts;
            error=io->read32(io->cookie,offset,&value);
            if(error) {out->failed_offset=offset;out->io_error=(uint32_t)error;out->result=KebabRscQueryReadError;return KebabRscQueryReadError;}
            ++out->reads;out->sample[pass][word]=value;++out->valid_words[pass];
            /* ID identifies the register layout. Do not apply v2 offsets
             * to a new RSC major version or walk beyond hardware geometry. */
            if(word==0 && (((value>>16)&255u)!=2 || ((value>>8)&255u)>7)) {
                out->result=KebabRscQueryGeometry;return KebabRscQueryGeometry;
            }
            if(word==1 && (((value>>12)&63u)!=8 || ((value>>27)&31u)!=16)) {
                out->result=KebabRscQueryGeometry;return KebabRscQueryGeometry;
            }
        }
        error=io->barrier(io->cookie);
        if(error){out->io_error=(uint32_t)error;out->result=KebabRscQueryReadError;return KebabRscQueryReadError;}
    }
    for(word=0;word<KebabRscQueryWords;++word)
        if(out->sample[0][word]==out->sample[1][word])++out->stable_words;
    if(out->stable_words!=KebabRscQueryWords) {out->result=KebabRscQueryChanged;return KebabRscQueryChanged;}
    out->irq_enable=out->sample[0][2];out->irq_status=out->sample[0][3];
    out->num_tcs=8;out->num_commands=16;
    for(tcs=0;tcs<8;++tcs) {
        const uint32_t *p=&out->sample[0][4+tcs*84];
        int occupied=p[0]!=0 || (p[1]&(1u<<24))!=0 || p[2]!=1 || p[3]!=0 || (out->irq_status&(1u<<tcs));
        for(command=0;command<16;++command)
            if(p[4+command*5+3] & ((1u<<8)|(1u<<16)))occupied=1;
        if(occupied)out->occupied_tcs_mask|=1u<<tcs;
        else if(tcs<2)out->clean_active_mask|=1u<<tcs;
    }
    /* A clean TCS is merely a candidate for the existing owner's claim.
     * No register sample can establish absence of firmware consumers. */
    out->ownership_granted=0;out->result=0;return 0;
}
#endif
