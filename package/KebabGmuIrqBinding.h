/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_IRQ_BINDING_H
#define KEBAB_GMU_IRQ_BINDING_H
/* Not an execution qualification. Root must review actual new-boot GIC NS
 * snapshot, exact source/writer pins and live RESET_HELD owner before binding.
 * Source FDT SPI304/305 and OTA bits are fixed; no security/group writes. */
static const uint32_t KebabGmuIrqNativeQualified=1;
#endif
