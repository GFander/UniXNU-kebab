/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_SMMU_4K_H
#define KEBAB_SMMU_4K_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Authored AArch64 stage1 32-bit IOVA/36-bit PA, 4KiB tables, read-only non-executable
 * normal-NC, non-global data (MAIR index0 must separately be configured to0x44). This is a
 * builder, not a hardware domain: no MMIO, DMA, TLB or cache maintenance. The
 * caller must pin, clean and retain the pool before any hardware publication. */
#define KEBAB_IO4K_ATTRS (0x0060000000000000ULL|0xec3ULL)
typedef struct {uint64_t root_pa,map_bytes;uint32_t pages_used,mapped_pages;} KebabSmmu4KInfo;
static uint64_t *kebab_io4k_child(uint64_t *pool,size_t bytes,uint64_t pa,uint64_t entry)
{
 uint64_t address=entry&0xffffff000ULL;
 if((entry&~0xffffff003ULL) || (entry&3)!=3 || address<pa || address-pa>bytes-4096)return NULL;
 return pool+(address-pa)/8;
}
static int KebabSmmu4KBuild(uint64_t *pool,size_t bytes,uint64_t pool_pa,uint64_t iova,uint64_t physical,uint64_t length,KebabSmmu4KInfo *out)
{
 if(out)memset(out,0,sizeof *out);
 if(!pool || !out || ((uintptr_t)pool&7) || bytes<4096 || bytes>65536 || (bytes&4095) ||
    (pool_pa&4095) || pool_pa<0x20000 || pool_pa>0x1000000000ULL-bytes ||
    (iova&4095) || (physical&4095) || (length&4095) || !length || length>0x1000000 ||
    iova<0x20000 || physical<0x20000 || iova>0x100000000ULL-length || physical>0x1000000000ULL-length ||
    (physical<pool_pa+bytes && pool_pa<physical+length))return 1;
 memset(pool,0,bytes);unsigned used=1;
 for(uint64_t offset=0;offset<length;offset+=4096) {
  uint64_t va=iova+offset;unsigned l1=(unsigned)(va>>30),l2=(unsigned)((va>>21)&511),l3=(unsigned)((va>>12)&511);
  if(!pool[l1]) {if(used==bytes/4096)goto exhausted;pool[l1]=(pool_pa+4096*used++)|3;}
  uint64_t *middle=kebab_io4k_child(pool,bytes,pool_pa,pool[l1]);
  if(!middle)goto exhausted;
  if(!middle[l2]) {if(used==bytes/4096)goto exhausted;middle[l2]=(pool_pa+4096*used++)|3;}
  uint64_t *leaf=kebab_io4k_child(pool,bytes,pool_pa,middle[l2]);
  if(!leaf)goto exhausted;
  leaf[l3]=(physical+offset)|KEBAB_IO4K_ATTRS;
 }
 out->root_pa=pool_pa;out->map_bytes=length;out->pages_used=used;out->mapped_pages=(uint32_t)(length/4096);return 0;
exhausted:
 memset(pool,0,bytes);return 2;
}
static int KebabSmmu4KTranslate(const uint64_t *pool,size_t bytes,uint64_t pool_pa,uint64_t iova,uint64_t *physical)
{
 if(!pool || !physical || ((uintptr_t)pool&7) || bytes<4096 || bytes>65536 || (bytes&4095) || (pool_pa&4095) ||
    pool_pa>0x1000000000ULL-bytes || iova>=0x100000000ULL)return 1;
 const uint64_t *middle=kebab_io4k_child((uint64_t *)pool,bytes,pool_pa,pool[iova>>30]);if(!middle)return 2;
 const uint64_t *leaf=kebab_io4k_child((uint64_t *)pool,bytes,pool_pa,middle[(iova>>21)&511]);if(!leaf)return 2;
 uint64_t entry=leaf[(iova>>12)&511];if((entry&~0xffffff000ULL)!=KEBAB_IO4K_ATTRS)return 2;
 *physical=(entry&0xffffff000ULL)|(iova&4095);return 0;
}
#endif
