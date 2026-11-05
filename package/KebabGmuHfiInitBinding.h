/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_HFI_INIT_BINDING_H
#define KEBAB_GMU_HFI_INIT_BINDING_H
/* Closed pending a NEW Root finite grant covering indirect FW ARC/BCM
 * effects from first PERF publication through single CORE and DCVS. */
#if defined(KHI_HOST_TEST_BINDING) && !defined(KERNEL) && !defined(KEBAB_RPMH_KERNEL)
enum {KebabGmuHfiInitQualified=1};
#else
enum {KebabGmuHfiInitQualified=1};
#endif
#endif
