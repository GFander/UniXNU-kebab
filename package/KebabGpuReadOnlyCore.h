/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_READ_ONLY_CORE_H
#define KEBAB_GPU_READ_ONLY_CORE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* SM8250 only. Offsets are BYTE offsets from GCC=0x00100000 and
 * GPUCC=0x03d90000. This observer contains NO GX/GMU/SMMU read path.
 * read32 returning an error cannot recover from a physical bus fault.
 * The caller must hold independently qualified access/power lifetimes.
 * A successful mapping or a sampled clock bit is NOT that qualification.
 * access_check must validate that the held qualification remains in force;
 * it must not enable clocks/rails or probe a possibly unpowered register.
 * Calls, output and callback storage must be exclusive and disjoint. */
enum { KebabGpuRoGcc=0, KebabGpuRoGpucc=1 };
enum {
    KebabGpuRoAccessGcc=1u, KebabGpuRoAccessGpucc=2u,
    KebabGpuRoAccessCxParent=4u, KebabGpuRoAccessGfxParent=8u
};
enum {
    KebabGpuRoGccCfgAhb=0, KebabGpuRoGccMemnoc,
    KebabGpuRoGccSnocDvm, KebabGpuRoGccDdrss, KebabGpuRoGccReset,
    KebabGpuRoGpuccAhb, KebabGpuRoCxGmuClock, KebabGpuRoCxoClock,
    KebabGpuRoGxGmuClock, KebabGpuRoCxGdsc, KebabGpuRoCxHwControl,
    KebabGpuRoGxGdsc, KebabGpuRoGxDomainMisc, KebabGpuRoCxReset,
    KebabGpuRoGmuReset, KebabGpuRoGxReset, KebabGpuRoXoReset,
    KebabGpuRoAcdReset, KebabGpuRoGfxAonReset,
    KebabGpuRoGmuRcgCommand, KebabGpuRoGmuRcgConfig,
    KebabGpuRoSlotCount
};
enum {
    KebabGpuRoControlSnapshot=0, KebabGpuRoGccOnly=1,
    KebabGpuRoPartialControl=2, KebabGpuRoGateClosed=3,
    KebabGpuRoUnstable=4, KebabGpuRoAccessUnavailable=5,
    KebabGpuRoIoError=6, KebabGpuRoBarrierError=7,
    KebabGpuRoGateChanged=8, KebabGpuRoInvalid=22
};
enum {
    KebabGpuRoNoGpuccLease=1u, KebabGpuRoNoCxParentLease=2u,
    KebabGpuRoNoGfxParentLease=4u,
    KebabGpuRoNotRunGmu=1u, KebabGpuRoNotRunGx=2u,
    KebabGpuRoNotRunSmmu=4u
};
typedef struct {
    void *cookie;
    uint32_t access_flags;
    int (*access_check)(void *,uint32_t required_flags);
    int (*read32)(void *,unsigned region,size_t byte_offset,uint32_t *);
    int (*barrier)(void *);
} KebabGpuRoIo;
/* Per-pass valid means that exact sample was successfully read. Stable means
 * both valid samples agree; zero raw data without valid never means OFF.
 * not_run_slots means no data read was attempted on either pass. Gate reads
 * are counted in attempts/reads but do not manufacture data validity.
 * not_run_subsystems is always 7: no downstream accessibility is inferred. */
typedef struct {
    uint32_t magic,schema,bytes,status;
    uint32_t access_flags,not_run_subsystems,not_run_slots,blocked_reason;
    uint32_t valid[2],stable,attempted[2],attempts,reads;
    uint32_t failed_slot,failed_region,failed_offset,io_error;
    uint32_t sample[2][KebabGpuRoSlotCount];
} KebabGpuRoRecord;

static inline int KebabGpuRoDescribe(unsigned slot,unsigned *region,
    uint32_t *offset,uint32_t *required)
{
    static const uint32_t offsets[KebabGpuRoSlotCount]={
        0x71004,0x7100c,0x71018,0x71154,0x71000,
        0x1078,0x1098,0x109c,0x1064,0x106c,0x1540,
        0x100c,0x1508,0x1068,0x111c,0x1008,0x1000,
        0x1160,0x10a0,0x1120,0x1124
    };
    if(slot>=KebabGpuRoSlotCount || !region || !offset || !required)return 0;
    *region=slot<KebabGpuRoGpuccAhb?KebabGpuRoGcc:KebabGpuRoGpucc;
    *offset=offsets[slot];
    *required=KebabGpuRoAccessGcc;
    if(*region==KebabGpuRoGpucc)*required|=KebabGpuRoAccessGpucc;
    if(slot==KebabGpuRoCxGdsc || slot==KebabGpuRoCxHwControl)
        *required|=KebabGpuRoAccessCxParent;
    if(slot==KebabGpuRoGxGdsc || slot==KebabGpuRoGxDomainMisc)
        *required|=KebabGpuRoAccessGfxParent;
    return 1;
}
static inline int KebabGpuRoOffsetAllowed(unsigned region,size_t offset)
{
    unsigned slot;
    for(slot=0;slot<KebabGpuRoSlotCount;++slot) {
        unsigned actual=0;uint32_t at=0,required=0;
        (void)KebabGpuRoDescribe(slot,&actual,&at,&required);
        if(region==actual && offset==at)return 1;
    }
    return 0;
}
static inline int KebabGpuRoClockReportedOn(uint32_t value)
{ return (value&1u)!=0 && (value&0x80000000u)==0; }
static inline int KebabGpuRoFail(KebabGpuRoRecord *r,unsigned status,
    unsigned slot,unsigned region,uint32_t offset,int error)
{
    r->status=status;r->failed_slot=slot;r->failed_region=region;
    r->failed_offset=offset;r->io_error=(uint32_t)error;return (int)status;
}
static inline int KebabGpuRoRaw(const KebabGpuRoIo *io,KebabGpuRoRecord *r,
    unsigned slot,uint32_t *value)
{
    unsigned region=0;uint32_t offset=0,required=0,result=0;int error;
    if(!KebabGpuRoDescribe(slot,&region,&offset,&required))return KebabGpuRoInvalid;
    if((io->access_flags&required)!=required ||
        io->access_check(io->cookie,required)!=0)
        return KebabGpuRoFail(r,KebabGpuRoAccessUnavailable,slot,region,offset,0);
    ++r->attempts;
    error=io->read32(io->cookie,region,offset,&result);
    if(error)return KebabGpuRoFail(r,KebabGpuRoIoError,slot,region,offset,error);
    ++r->reads;*value=result;return 0;
}
static inline int KebabGpuRoControlGate(const KebabGpuRoIo *io,
    KebabGpuRoRecord *r,int require_internal_ahb)
{
    uint32_t cfg=0,reset=0,ahb=0;int result;
    result=KebabGpuRoRaw(io,r,KebabGpuRoGccCfgAhb,&cfg);if(result)return result;
    if(cfg!=r->sample[0][KebabGpuRoGccCfgAhb] || !KebabGpuRoClockReportedOn(cfg))
        return KebabGpuRoFail(r,KebabGpuRoGateChanged,KebabGpuRoGccCfgAhb,
            KebabGpuRoGcc,0x71004,0);
    result=KebabGpuRoRaw(io,r,KebabGpuRoGccReset,&reset);if(result)return result;
    if(reset!=r->sample[0][KebabGpuRoGccReset] || (reset&1u))
        return KebabGpuRoFail(r,KebabGpuRoGateChanged,KebabGpuRoGccReset,
            KebabGpuRoGcc,0x71000,0);
    if(require_internal_ahb) {
        result=KebabGpuRoRaw(io,r,KebabGpuRoGpuccAhb,&ahb);if(result)return result;
        if(ahb!=r->sample[0][KebabGpuRoGpuccAhb] || !KebabGpuRoClockReportedOn(ahb))
            return KebabGpuRoFail(r,KebabGpuRoGateChanged,KebabGpuRoGpuccAhb,
                KebabGpuRoGpucc,0x1078,0);
    }
    return 0;
}
static inline int KebabGpuRoSample(const KebabGpuRoIo *io,KebabGpuRoRecord *r,
    unsigned pass,unsigned slot)
{
    uint32_t value=0,before=0;int result;
    if(slot>=KebabGpuRoGpuccAhb) {
        result=KebabGpuRoControlGate(io,r,slot!=KebabGpuRoGpuccAhb);
        if(result)return result;
    }
    before=r->attempts;
    result=KebabGpuRoRaw(io,r,slot,&value);
    if(r->attempts!=before)r->attempted[pass]|=1u<<slot;
    if(result)return result;
    r->sample[pass][slot]=value;r->valid[pass]|=1u<<slot;
    return 0;
}
static inline int KebabGpuRoBarrier(const KebabGpuRoIo *io,KebabGpuRoRecord *r)
{
    int error=io->barrier(io->cookie);
    if(error)return KebabGpuRoFail(r,KebabGpuRoBarrierError,UINT32_MAX,
        UINT32_MAX,0,error);
    return 0;
}
static inline int KebabGpuRoSame(KebabGpuRoRecord *r,unsigned first,unsigned end)
{
    unsigned slot;int same=1;
    for(slot=first;slot<end;++slot) {
        uint32_t bit=1u<<slot;
        if((r->valid[0]&r->valid[1]&bit)!=0) {
            if(r->sample[0][slot]==r->sample[1][slot])r->stable|=bit;
            else same=0;
        }
    }
    return same;
}
static inline int KebabGpuRoObserve(const KebabGpuRoIo *io,KebabGpuRoRecord *r)
{
    unsigned pass,slot;int result=0;
    const uint32_t all=(1u<<KebabGpuRoSlotCount)-1u;
    if(!r)return KebabGpuRoInvalid;
    memset(r,0,sizeof(*r));
    r->magic=0x4f525047u;r->schema=1;r->bytes=(uint32_t)sizeof(*r);
    r->failed_slot=UINT32_MAX;r->failed_region=UINT32_MAX;
    r->not_run_subsystems=KebabGpuRoNotRunGmu|KebabGpuRoNotRunGx|KebabGpuRoNotRunSmmu;
    r->not_run_slots=all;r->status=KebabGpuRoInvalid;
    if(!io || !io->read32 || !io->access_check || !io->barrier ||
        (io->access_flags&~15u)!=0)return KebabGpuRoInvalid;
    r->access_flags=io->access_flags;
    for(pass=0;pass<2;++pass) {
        for(slot=0;slot<KebabGpuRoGpuccAhb;++slot) {
            result=KebabGpuRoSample(io,r,pass,slot);if(result)goto done;
        }
        result=KebabGpuRoBarrier(io,r);if(result)goto done;
    }
    if(!KebabGpuRoSame(r,0,KebabGpuRoGpuccAhb)) {result=KebabGpuRoUnstable;goto done;}
    if(!KebabGpuRoClockReportedOn(r->sample[0][KebabGpuRoGccCfgAhb]) ||
        (r->sample[0][KebabGpuRoGccReset]&1u)) {result=KebabGpuRoGateClosed;goto done;}
    if((io->access_flags&KebabGpuRoAccessGpucc)==0) {
        r->blocked_reason|=KebabGpuRoNoGpuccLease;result=KebabGpuRoGccOnly;goto done;
    }
    for(pass=0;pass<2;++pass) {
        result=KebabGpuRoSample(io,r,pass,KebabGpuRoGpuccAhb);if(result)goto done;
        result=KebabGpuRoBarrier(io,r);if(result)goto done;
    }
    if(!KebabGpuRoSame(r,KebabGpuRoGpuccAhb,KebabGpuRoGpuccAhb+1)) {
        result=KebabGpuRoUnstable;goto done;
    }
    if(!KebabGpuRoClockReportedOn(r->sample[0][KebabGpuRoGpuccAhb])) {
        result=KebabGpuRoGateClosed;goto done;
    }
    for(pass=0;pass<2;++pass) {
        for(slot=KebabGpuRoGpuccAhb+1;slot<KebabGpuRoSlotCount;++slot) {
            unsigned region=0;uint32_t offset=0,required=0;
            (void)KebabGpuRoDescribe(slot,&region,&offset,&required);
            if((io->access_flags&required)!=required) {
                if(required&KebabGpuRoAccessCxParent)r->blocked_reason|=KebabGpuRoNoCxParentLease;
                if(required&KebabGpuRoAccessGfxParent)r->blocked_reason|=KebabGpuRoNoGfxParentLease;
                continue;
            }
            result=KebabGpuRoSample(io,r,pass,slot);if(result)goto done;
        }
        result=KebabGpuRoBarrier(io,r);if(result)goto done;
    }
    if(!KebabGpuRoSame(r,KebabGpuRoGpuccAhb+1,KebabGpuRoSlotCount)) {
        result=KebabGpuRoUnstable;goto done;
    }
    result=r->blocked_reason?KebabGpuRoPartialControl:KebabGpuRoControlSnapshot;
done:
    r->not_run_slots=all&~(r->attempted[0]|r->attempted[1]);
    r->status=(uint32_t)result;return result;
}
#endif
