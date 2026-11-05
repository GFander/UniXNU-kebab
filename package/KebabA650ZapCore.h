/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_ZAP_CORE_H
#define KEBAB_A650_ZAP_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Bounded A650 input/staging candidate. No allocation, SMC, MMIO, signature
 * verification or ownership transfer occurs here. Hash binding is the caller's
 * separate prerequisite; parsing success never means authenticated firmware. */
enum { KZ_OK, KZ_ARGUMENT, KZ_HEADER, KZ_PROFILE, KZ_BOUNDS,
       KZ_SPLIT, KZ_RELOCATION, KZ_PLACEMENT, KZ_OVERLAP, KZ_UNQUALIFIED };
typedef struct { const uint8_t *bytes; size_t size; } KebabZapBlob;
typedef struct { KebabZapBlob metadata, part[3]; } KebabZapInputs;
typedef struct {
 uint32_t header_bytes, hash_bytes, metadata_bytes;
 uint32_t link_address, entry_offset, load_file_bytes, load_memory_bytes;
 uint32_t logical_region_bytes, region_alignment, allocation_bytes;
} KebabZapPlan;
static inline uint16_t kz_u16(const uint8_t *p)
{ return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static inline uint32_t kz_u32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline int kz_extent(size_t size,uint64_t offset,uint64_t length)
{ return offset<=size && length<=size-offset; }
static inline int kz_cpu_overlap(const void *a,size_t an,const void *b,size_t bn)
{
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 if(x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn)return 1;
 return x<y+bn && y<x+an;
}
static inline int KebabA650ZapPlan(const KebabZapInputs *in,KebabZapPlan *out)
{
 if(out)memset(out,0,sizeof *out);
 if(!in || !out || !in->metadata.bytes)return KZ_ARGUMENT;
 const uint8_t *m=in->metadata.bytes;size_t n=in->metadata.size;
 if(n<148 || n>65536)return KZ_BOUNDS;
 if(memcmp(m,"\177ELF\1\1\1",7) || kz_u16(m+16)!=2 ||
    kz_u16(m+18)!=164 || kz_u32(m+20)!=1 || kz_u32(m+28)!=52 ||
    kz_u32(m+32)!=0 || kz_u32(m+36)!=3 || kz_u16(m+40)!=52 ||
    kz_u16(m+42)!=32 || kz_u16(m+44)!=3 || kz_u16(m+46) ||
    kz_u16(m+48) || kz_u16(m+50))return KZ_HEADER;
 const uint8_t *h=m+52,*hash=m+84,*load=m+116;
 uint32_t hb=kz_u32(h+16),hs=kz_u32(hash+16),lb=kz_u32(load+16);
 uint32_t lm=kz_u32(load+20),link=kz_u32(load+12),align=kz_u32(load+28);
 if(kz_u32(h)!=0 || kz_u32(h+4)!=0 || kz_u32(h+8)!=0 ||
    kz_u32(h+12)!=0 || hb!=148 || kz_u32(h+20)!=0 ||
    kz_u32(h+24)!=0x07000000 || kz_u32(h+28)!=0 ||
    kz_u32(hash)!=0 || kz_u32(hash+24)!=0x02200000 ||
    !hs || hs>65536-148 || hs>kz_u32(hash+20) ||
    n!=(size_t)hb+hs || kz_u32(load)!=1)return KZ_PROFILE;
 if(!lb || lb>lm || lm>0x100000 || !lm || (link&4095) ||
    (uint64_t)link+lm>0x100000000ULL || align!=0x100000 ||
    kz_u32(load+8)!=link)return KZ_BOUNDS;
 if(kz_u32(load+24)!=0x08000007)return KZ_RELOCATION;
 uint32_t entry=kz_u32(m+24);
 if(entry<link || (uint64_t)entry>=(uint64_t)link+lm)return KZ_BOUNDS;
 for(unsigned i=0;i<3;i++) {
  uint32_t expected=i==0?hb:i==1?hs:lb;
  if(!in->part[i].bytes || in->part[i].size!=expected)return KZ_SPLIT;
 }
 if(memcmp(in->part[0].bytes,m,hb) || memcmp(in->part[1].bytes,m+hb,hs))return KZ_SPLIT;
 uint64_t logical=(((uint64_t)link+lm+4095)&~4095ULL)-link;
 uint64_t allocation=(logical>n?logical:n);
 allocation=(allocation+align-1)&~((uint64_t)align-1);
 if(logical>0x100000 || allocation>0x100000)return KZ_BOUNDS;
 out->header_bytes=hb;out->hash_bytes=hs;out->metadata_bytes=(uint32_t)n;
 out->link_address=link;out->entry_offset=entry-link;
 out->load_file_bytes=lb;out->load_memory_bytes=lm;
 out->logical_region_bytes=(uint32_t)logical;out->region_alignment=align;
 out->allocation_bytes=(uint32_t)allocation;return 0;
}
/* Additional monolithic ELF consistency check, never signature validation. */
static inline int KebabA650ZapElf(const KebabZapInputs *in,const KebabZapBlob *elf)
{
 KebabZapPlan p;int r=KebabA650ZapPlan(in,&p);if(r)return r;
 if(!elf || !elf->bytes || elf->size<148 || elf->size>0x200000 ||
    memcmp(elf->bytes,in->metadata.bytes,148))return KZ_HEADER;
 for(unsigned i=0;i<3;i++) {
  const uint8_t *ph=in->metadata.bytes+52+32*i;
  uint32_t offset=kz_u32(ph+4),length=kz_u32(ph+16);
  if(!kz_extent(elf->size,offset,length))return KZ_BOUNDS;
  if(memcmp(elf->bytes+offset,in->part[i].bytes,length))return KZ_SPLIT;
 }
 return 0;
}
static inline int kz_pa_fits(uint64_t pa,uint64_t bytes,unsigned bits)
{
 if(!pa || !bytes || (bits!=32 && bits!=64))return 0;
 uint64_t last=bits==32?0xffffffffULL:UINT64_MAX;
 return pa<=last && bytes-1<=last-pa;
}
/* address_bits must come from a PAS13-specific qualified aperture, never from
 * SMC64 availability, QSEE success or display SMMU OAS. metadata is an isolated
 * NC allocation, NOT an existing QSEE SHM bridge or shared Root/Data page.
 * Range checks do not themselves establish memory ownership/security policy. */
static inline int KebabA650ZapPlacement(const KebabZapPlan *p,
 uint64_t metadata_pa,uint64_t metadata_capacity,uint64_t image_pa,
 uint64_t image_capacity,unsigned qualified_address_bits)
{
 if(!p || !p->metadata_bytes || !p->allocation_bytes)return KZ_ARGUMENT;
 if(qualified_address_bits!=32 && qualified_address_bits!=64)return KZ_UNQUALIFIED;
 if((metadata_pa&0x3fff) || (metadata_capacity&0x3fff) ||
    metadata_capacity<p->metadata_bytes || image_capacity<p->allocation_bytes ||
    (image_pa&(p->region_alignment-1)) ||
    !kz_pa_fits(metadata_pa,metadata_capacity,qualified_address_bits) ||
    !kz_pa_fits(image_pa,image_capacity,qualified_address_bits))return KZ_PLACEMENT;
 /* Subtraction avoids overflow at UINT64_MAX for valid inclusive PA ranges. */
 if(metadata_pa<=image_pa ? image_pa-metadata_pa<metadata_capacity :
                          metadata_pa-image_pa<image_capacity)return KZ_OVERLAP;
 return 0;
}
/* Only before ANY PAS call, on exclusively owned, unpublished CPU memory.
 * All validation precedes writes. Keep buffers pinned after any uncertain SMC;
 * do not call Stage again while firmware may own either range. */
static inline int KebabA650ZapStage(const KebabZapInputs *in,
 void *metadata,size_t metadata_capacity,void *image,size_t image_capacity,
 KebabZapPlan *out)
{
 KebabZapPlan p;int r=KebabA650ZapPlan(in,&p);if(r)return r;
 if(!out || !metadata || !image || metadata_capacity<p.metadata_bytes ||
    metadata_capacity>0x100000 || image_capacity<p.allocation_bytes ||
    image_capacity>0x100000)return KZ_BOUNDS;
 if(kz_cpu_overlap(metadata,metadata_capacity,image,image_capacity))return KZ_OVERLAP;
 for(unsigned i=0;i<4;i++) {
  KebabZapBlob b=i?in->part[i-1]:in->metadata;
  if(kz_cpu_overlap(metadata,metadata_capacity,b.bytes,b.size) ||
     kz_cpu_overlap(image,image_capacity,b.bytes,b.size))return KZ_OVERLAP;
 }
 memset(metadata,0,metadata_capacity);memset(image,0,image_capacity);
 memcpy(metadata,in->metadata.bytes,p.metadata_bytes);
 memcpy(image,in->part[2].bytes,p.load_file_bytes);
 *out=p;return 0;
}

/* ABI builder only: no call is made and no lifecycle permission is granted.
 * Normal PAS calls are standard/yielding SIP, unlike fast INFO availability.
 * PA32 fallback is rejected when any address would be truncated. */
typedef struct { uint64_t x[8]; } KebabPasRegs;
static inline int KebabPas13Request(unsigned command,int smc64,uint64_t address,
 uint64_t bytes,KebabPasRegs *out)
{
 if(!out || (smc64!=0 && smc64!=1))return KZ_ARGUMENT;
 memset(out,0,sizeof *out);
 if(command!=1 && command!=2 && command!=5 && command!=6 && command!=7)return KZ_ARGUMENT;
 if((command==1 || command==2) &&
    (!kz_pa_fits(address,command==1?1:bytes,smc64?64:32) || (address&4095)))return KZ_PLACEMENT;
 if(command!=2 && bytes)return KZ_ARGUMENT;
 if(command!=1 && command!=2 && address)return KZ_ARGUMENT;
 out->x[0]=(smc64?0x42000200ULL:0x02000200ULL)|command;
 out->x[1]=command==1?0x82:command==2?3:1;
 out->x[2]=13;
 if(command==1 || command==2)out->x[3]=address;
 if(command==2)out->x[4]=bytes;
 return 0;
}
typedef struct {
 uint64_t status_raw,value,type,data,cookie;
 unsigned attempts,returned,interrupted_exhausted;
} KebabPasResult;
/* Native adapter must serialize SCM clients, reject interrupt/panic/preemption
 * unsafe contexts and hold all power/memory ownership externally. The callback
 * is a host fake in tests. A nonreturning SMC is NOT timed out by this loop. */
static inline int KebabPasCallBounded(const KebabPasRegs *request,
 void *cookie,int (*invoke)(void *,KebabPasRegs *),unsigned limit,KebabPasResult *out)
{
 if(out)memset(out,0,sizeof *out);
 if(!request || !invoke || !out || !limit || limit>32)return KZ_ARGUMENT;
 unsigned command=(unsigned)(request->x[0]&255);
 KebabPasRegs allowed;
 if(KebabPas13Request(command,!!(request->x[0]&0x40000000),
    command==1 || command==2?request->x[3]:0,command==2?request->x[4]:0,&allowed) ||
    memcmp(request,&allowed,sizeof allowed))return KZ_ARGUMENT;
 uint64_t resume_cookie=0;
 for(unsigned n=0;n<limit;n++) {
  KebabPasRegs call=*request;call.x[0]=n?1:request->x[0];call.x[6]=resume_cookie;
  out->attempts=n+1;
  if(invoke(cookie,&call))return KZ_UNQUALIFIED;
  out->returned=1;out->status_raw=call.x[0];out->value=call.x[1];
  out->type=call.x[2];out->data=call.x[3];out->cookie=call.x[6];
  if(call.x[0]!=1)return 0; /* preserve transport and peripheral result */
  resume_cookie=call.x[6];
 }
 out->interrupted_exhausted=1;return KZ_UNQUALIFIED;
}
/* For INIT/MEM_SETUP/AUTH/SHUTDOWN only. IS_SUPPORTED instead expects value=1.
 * This result still grants no CPU write/release permission over PAS memory. */
static inline int KebabPas13OperationSucceeded(const KebabPasResult *r)
{ return r && r->returned && !r->interrupted_exhausted && !r->status_raw && !r->value; }
#endif
