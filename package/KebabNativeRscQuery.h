/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_RSC_QUERY_H
#define KEBAB_NATIVE_RSC_QUERY_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/KebabRscOwnerQuery.h>
/* Existing board-qualified CPU APPS-DRV2 aperture only, not GPU MMIO.
 * Normal worker/board/teardown check must precede mapping and every read.
 * No panic, write-capable mapping, IRQ_CLEAR or resource-address dereference.
 * Link the same-owner .inc in the existing canonical RPMh owner module. */
struct KebabNativeRscAccess {uintptr_t address;void *cookie;int (*check)(void *);};
static int kebab_native_rsc_read(void *cookie,uint32_t offset,uint32_t *out)
{
 KebabNativeRscAccess *a=(KebabNativeRscAccess *)cookie;
 if(!a || !out || !KebabRscQueryAllowed(offset) || offset>0x10000-4)return 22;
 if(!a->address || !a->check || a->check(a->cookie))return 95;
 *out=*(volatile const uint32_t *)(a->address+offset);return 0;
}
static int kebab_native_rsc_barrier(void *cookie)
{
 KebabNativeRscAccess *a=(KebabNativeRscAccess *)cookie;
 if(!a || !a->address || !a->check || a->check(a->cookie))return 95;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static int kebab_native_rsc_owner_snapshot(void *cookie,int (*check)(void *),KebabRscOwnerQueryRecord *out)
{
 IOMemoryDescriptor *memory=0;IOMemoryMap *map=0;int result=22;
 KebabNativeRscAccess access={0,cookie,check};
 if(!out)return 22;
 (void)kebab_rpmh_owner_query(0,out);
 if(!check || check(cookie)){out->result=95;return 95;}
 memory=IOMemoryDescriptor::withPhysicalAddress(0x18220000,0x10000,kIODirectionIn);
 if(!memory)goto no_memory;
 map=memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
 if(!map || map->getLength()<0x10000)goto no_memory;
 access.address=map->getVirtualAddress();if(!access.address)goto no_memory;
 {
  KebabRscQueryIo io={&access,kebab_native_rsc_read,kebab_native_rsc_barrier};
  result=kebab_rpmh_owner_query(&io,out);
 }
 goto done;
no_memory:
 out->result=12;result=12;
done:
 if(map)map->release();if(memory)memory->release();return result;
}
#endif
