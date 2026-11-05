/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_SMMU_ASID_AUDIT_H
#define KEBAB_SMMU_ASID_AUDIT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Arm IHI0062,2.6: IDR1.NUMCB as read through the Non-secure configuration
 * interface reports only the Non-secure context-bank namespace. Secure banks
 * hidden by SCR1.NSNUMCBO are not walked. This reader never changes allocation,
 * tables, fault status, ASIDs or translation enables in any bank. */
typedef struct {uint32_t id0,id1,cr0,context_banks,active_stage1,active_stage2,chosen_asid,reads,failed_offset,stable,display_cb,display_sctlr,display_cbar,display_cba2r,namespace_contract,reserved,used_asid[8],stage1_banks[4];} KebabSmmuAsidObservation;
typedef struct {uint32_t bank[128][8];} KebabSmmuAsidWorkspace;
typedef struct {void *cookie;size_t bytes;int (*read32)(void *,size_t,uint32_t *);} KebabSmmuAsidIo;
static int kebab_asid_read(const KebabSmmuAsidIo *io,size_t offset,uint32_t *v,KebabSmmuAsidObservation *o)
{
 o->reads++;if((offset&3) || offset>io->bytes-4 || io->read32(io->cookie,offset,v)){o->failed_offset=(uint32_t)offset;return 1;}return 0;
}
static void kebab_asid_mark(KebabSmmuAsidObservation *o,uint32_t asid)
{ asid&=255;o->used_asid[asid/32]|=1u<<(asid%32); }
static int KebabSmmuAsidAudit(const KebabSmmuAsidIo *io,uint32_t display_cb,KebabSmmuAsidWorkspace *workspace,KebabSmmuAsidObservation *o)
{
 if(o)memset(o,0,sizeof *o);
 if(!io || !o || !workspace || !io->read32 || io->bytes!=0x100000)return 1;
 memset(workspace,0,sizeof *workspace);o->display_cb=display_cb;
 if(kebab_asid_read(io,0,&o->cr0,o) || kebab_asid_read(io,0x20,&o->id0,o) || kebab_asid_read(io,0x24,&o->id1,o))return 2;
 o->context_banks=o->id1&255;
 if(!(o->id0&(1u<<27)) || (o->id1&0x80000000u) || !o->context_banks || o->context_banks>128 || display_cb>=o->context_banks ||
    (1ULL<<(12+((o->id1>>28)&7)+1))!=io->bytes/2)return 3;
 kebab_asid_mark(o,0);o->namespace_contract=1;
 for(unsigned pass=0;pass<2;pass++)for(unsigned cb=0;cb<o->context_banks;cb++) {
  uint32_t v[8]={0};size_t base=0x80000+((size_t)cb<<12);
  if(kebab_asid_read(io,base,&v[0],o) || kebab_asid_read(io,0x1000+4*cb,&v[1],o) || kebab_asid_read(io,0x1800+4*cb,&v[2],o))return 2;
  uint32_t type=(v[1]>>16)&3;int s1=(v[0]&1) && type!=0;
  if(s1 && (kebab_asid_read(io,base+0x10,&v[3],o) || kebab_asid_read(io,base+0x24,&v[4],o) ||
            kebab_asid_read(io,base+0x2c,&v[5],o) || kebab_asid_read(io,base+0x34,&v[6],o) || kebab_asid_read(io,base+0x30,&v[7],o)))return 2;
  if(pass) {if(memcmp(v,workspace->bank[cb],sizeof v))return 4;continue;}
  memcpy(workspace->bank[cb],v,sizeof v);
  if(cb==display_cb){o->display_sctlr=v[0];o->display_cbar=v[1];o->display_cba2r=v[2];}
  if((v[0]&1) && (type==0 || type==3))o->active_stage2++;
  if(s1) {
   o->active_stage1++;o->stage1_banks[cb/32]|=1u<<(cb%32);
   /* Conservatively reserve the low8 bits of both TTBR ASID fields and
    * CONTEXTIDR. This covers AArch64, AArch32 long/short and ASID16 contexts
    * without assuming firmware used the Linux CB+1 allocation convention. */
   kebab_asid_mark(o,v[4]>>16);kebab_asid_mark(o,v[5]>>16);kebab_asid_mark(o,v[6]);
  }
 }
 uint32_t check;
 if(kebab_asid_read(io,0,&check,o))return 2;if(check!=o->cr0)return 4;
 if(kebab_asid_read(io,0x24,&check,o))return 2;if(check!=o->id1)return 4;
 uint32_t preferred=display_cb+1;
 if(!(o->used_asid[preferred/32]&(1u<<(preferred%32))))o->chosen_asid=preferred;
 else for(unsigned i=1;i<256;i++)if(!(o->used_asid[i/32]&(1u<<(i%32)))){o->chosen_asid=i;break;}
 if(!o->chosen_asid)return 5;
 o->stable=1;return 0;
}
#endif
