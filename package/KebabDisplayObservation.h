/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_DISPLAY_OBSERVATION_H
#define KEBAB_DISPLAY_OBSERVATION_H
#include <stddef.h>
#include <stdint.h>
enum { KebabDisplayCc=0, KebabDisplayMdp=1 };
typedef struct {
    void *cookie;
    size_t bytes[2];
    int (*read32)(void *,unsigned,size_t,uint32_t *);
} KebabDisplayReadIo;
typedef struct {
    uint32_t gdsc,ahb,core;
    uint32_t mdss_version,mdp_version,irq_enabled,irq_pending;
    uint32_t intf[2][8];
    uint32_t source[8][4]; /* size, address0, stride0, format */
} KebabDisplayObservation;
typedef struct { unsigned reads,attempts,failed_region;size_t failed_offset; } KebabDisplayReadResult;
enum { KebabDisplayReadComplete=0,KebabDisplayReadArgument,KebabDisplayReadIoError,
       KebabDisplayReadPowerUnavailable,KebabDisplayReadUnstable };
/* Source-bound SM8250 observation, relative to DISP_CC and vendor mdp_phys.
 * Caller owns mapped, accessible configuration resources and serializes real
 * power/mode changes. Callback must enforce its native mapping lease. Status
 * bits do not establish ownership. No MMIO mapping, writes, IRQ enable, waits,
 * power/clock votes, VBL callbacks or completed-frame claims occur here.
 * Complete only means two bounded snapshots agree on selected config fields.
 * Counters are raw and may change naturally. Failure leaves out zeroed.
 */
int KebabDisplayObserve(const KebabDisplayReadIo *,KebabDisplayObservation *,KebabDisplayReadResult *);
/* Source-derived SID/mask membership only; does not allocate or program a CB. */
int KebabDisplayNonsecureSid(uint32_t sid);
/* Validate a proposed IOVA in the vendor nonsecure 32-bit pool. This is not
 * a DMA map. Granule is supplied by an independently qualified SMMU backend,
 * never inferred from XNU's CPU page size. No mapping or TLB action occurs. */
int KebabDisplayIovaSpan(uint64_t address,uint64_t bytes,uint64_t granule);
#endif
