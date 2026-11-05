/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_GMU_PMAP_CACHE_H
#define KEBAB_A650_GMU_PMAP_CACHE_H
/* These are actual exported XNU kernel types/constants, independently of
 * MACH_KERNEL_PRIVATE. Do not change that macro for the entire IOKit TU.
 * vm/pmap.h:335-336 hides this existing C prototype behind the Mach-private
 * scope; arm/pmap/pmap.c:10164 defines the real read-only implementation. */
#include <mach/vm_types.h>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#ifdef __cplusplus
static_assert(sizeof(ppnum_t)==4,"actual XNU ppnum_t ABI");
static_assert(VM_WIMG_MASK==0xff && VM_WIMG_WCOMB==6,"actual ARM Normal-NC cache constants");
extern "C" {
#endif
extern unsigned int pmap_cache_attributes(ppnum_t);
#ifdef __cplusplus
}
#endif
#endif
