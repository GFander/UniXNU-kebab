/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_READ_ONLY_H
#define KEBAB_NATIVE_GPU_READ_ONLY_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "KebabGpuReadOnlyCore.h"

/* Candidate only; include once from a normal worker after the existing
 * KEBAB/SM8250 board qualification. The caller's check must establish that
 * the ALWAYS-ON GCC path remains accessible for the whole snapshot, and
 * reject panic/teardown or concurrent resource reconfiguration. It must
 * never read GPUCC/GDSCR/GMU/GX/SMMU to prove that assertion.
 * No GPUCC qualification can be supplied to this initial native adapter.
 * It maps only a 16KiB-aligned slice of GCC containing the five registers.
 * No try/catch or callback error can recover from a physical MMIO fault. */
struct KebabNativeGpuRoAccess {
    uintptr_t address;
    void *owner_cookie;
    int (*gcc_access_check)(void *);
};
static int kebab_gpu_ro_native_access(void *cookie,uint32_t flags)
{
    KebabNativeGpuRoAccess *access=(KebabNativeGpuRoAccess *)cookie;
    if(!access || !access->address || flags!=KebabGpuRoAccessGcc ||
        !access->gcc_access_check)return 95;
    return access->gcc_access_check(access->owner_cookie);
}
static int kebab_gpu_ro_native_read(void *cookie,unsigned region,size_t offset,uint32_t *out)
{
    KebabNativeGpuRoAccess *access=(KebabNativeGpuRoAccess *)cookie;
    if(region!=KebabGpuRoGcc || !out ||
        !KebabGpuRoOffsetAllowed(region,offset) ||
        offset<0x70000 || offset-0x70000>0x4000-4)return 22;
    if(kebab_gpu_ro_native_access(cookie,KebabGpuRoAccessGcc))return 5;
    *out=*(volatile const uint32_t *)(access->address+(offset-0x70000));
    return 0;
}
static int kebab_gpu_ro_native_barrier(void *cookie)
{
    if(kebab_gpu_ro_native_access(cookie,KebabGpuRoAccessGcc))return 5;
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    OSMemoryBarrier();
#endif
    return 0;
}
static int kebab_native_gpu_ro_snapshot(void *owner_cookie,
    int (*gcc_access_check)(void *),KebabGpuRoRecord *out)
{
    IOMemoryDescriptor *memory=0;IOMemoryMap *map=0;
    KebabNativeGpuRoAccess access={0,owner_cookie,gcc_access_check};
    int status=KebabGpuRoInvalid;
    if(!out)return status;
    (void)KebabGpuRoObserve(0,out);
    if(!gcc_access_check || gcc_access_check(owner_cookie)) {
        out->status=KebabGpuRoAccessUnavailable;return KebabGpuRoAccessUnavailable;
    }
    memory=IOMemoryDescriptor::withPhysicalAddress(0x00170000,0x4000,kIODirectionIn);
    if(!memory)goto no_memory;
    map=memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
    if(!map || map->getLength()<0x4000)goto no_memory;
    access.address=map->getVirtualAddress();
    if(!access.address)goto no_memory;
    {
        KebabGpuRoIo io={&access,KebabGpuRoAccessGcc,kebab_gpu_ro_native_access,
            kebab_gpu_ro_native_read,kebab_gpu_ro_native_barrier};
        status=KebabGpuRoObserve(&io,out);
    }
    goto done;
no_memory:
    out->status=KebabGpuRoIoError;out->io_error=12;status=KebabGpuRoIoError;
done:
    if(map)map->release();
    if(memory)memory->release();
    return status;
}
#endif
