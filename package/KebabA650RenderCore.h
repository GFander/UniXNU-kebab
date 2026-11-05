/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_RENDER_CORE_H
#define KEBAB_A650_RENDER_CORE_H

#include "KebabA650CpCore.h"

enum {
    KAR_WIDTH = 32,
    KAR_HEIGHT = 32,
    KAR_PITCH = 256,
    KAR_TARGET_BYTES = KAR_PITCH * KAR_HEIGHT,
    KAR_BO_BYTES = 0x10000,
    KAR_TARGET_IOVA = 0x90010000u,
    KAR_TARGET_OFFSET = 0x100,
    KAR_TARGET_SPACING = 0x4000,
    KAR_SENTINEL = 0xa5,
    KAR_PASSES = 2,
    KAR_WORDS = 212,
    KAR_SCHEMA = 6,
    KAR_RING_START = 36,
    KAR_CONTROL_OFFSET = 0x80,
    KAR_CONTROL_VALUE = 0xa650c001u,
    KAR_EVENT_OFFSET = 128,
    KAR_EVENT_SPACING = 64,
    KAR_EVENT_PASS_SPACING = 256,
    KAR_DIAGNOSTIC_COUNT = 18,
    KAR_DIAGNOSTIC_OFFSET = 0x300,
    KAR_DIAGNOSTIC_SPACING = 0x100,
    KAR_DIAGNOSTIC_SENTINEL = 0xa65e0000u,
};

/* Fixed nonsecure A6XX 2D command state, Mesa63350a a6xx.xml.
 * Never accept a caller-supplied register or destination address. */
static const uint32_t KARDiagnosticRegisters[KAR_DIAGNOSTIC_COUNT] = {
    0x8400, 0x8405, 0x8406, 0x8c00, 0x8c01, 0x8c17,
    0x8c18, 0x8c19, 0x8c1a, 0x8c2c, 0x8c2d, 0x8c2e,
    0x8c2f, 0xacc0, 0x8e04, 0x8e07, 0x8811, 0x8110,
};

static inline uint32_t
KARDiagnosticOffset(unsigned pass, unsigned index)
{
    return KAR_DIAGNOSTIC_OFFSET + pass * KAR_DIAGNOSTIC_SPACING + index * 4;
}

static inline uint32_t
KARDiagnosticSentinel(unsigned pass, unsigned index)
{
    return KAR_DIAGNOSTIC_SENTINEL + pass * KAR_DIAGNOSTIC_COUNT + index;
}

static const uint8_t KARColors[4][4] = {
    {255, 0, 0, 255}, {0, 255, 0, 0},
    {0, 0, 255, 255}, {255, 255, 0, 0},
};

static inline uint32_t
KARTargetOffset(unsigned pass)
{
    return KAR_TARGET_OFFSET + pass * KAR_TARGET_SPACING;
}

static inline uint32_t
KAREventOffset(unsigned pass, unsigned event)
{
    return KAR_EVENT_OFFSET + pass * KAR_EVENT_PASS_SPACING + event * KAR_EVENT_SPACING;
}

static inline uint32_t *
KARFlushEvent(uint32_t *p, unsigned event, uint32_t offset)
{
    *p++ = KCPType7(0x46, 4);
    *p++ = 0x40000000u | event;
    *p++ = KCP_FENCE_IOVA + offset;
    *p++ = 0;
    *p++ = 0;
    return p;
}

/* Mesa 63350a034b2b A6XX solid-color setup, clear_surface and emit_blit_fini.
 * Each pass owns one fixed linear RGBA8 target and three CCU timestamp slots.
 * A650 normal/blit ECO values are equal, so mode switches are skipped;
 * initial restore still programs RB_DBG_ECO_CNTL=0x04100000. */
static inline int
KARBuildWords(unsigned pass, uint32_t (*commands)[KAR_WORDS])
{
    if (pass >= KAR_PASSES || !commands) {
        return 22;
    }
    uint32_t *p = *commands;
    /* CP control write into the SAME 4KiB GPU target page, outside pixels.
     * This is a visibility witness, never pixel generation or a draw result.
     * Original0591 2D stream below is otherwise byte-for-byte unchanged. */
    *p++ = KCPType7(0x3d, 3);
    *p++ = KAR_TARGET_IOVA + KAR_CONTROL_OFFSET + pass * KAR_TARGET_SPACING;
    *p++ = 0;
    *p++ = KAR_CONTROL_VALUE + pass;
    /* Fixed Mesa63350a A650 initial non-context restore. Equality of normal
     * and blit ECO values only suppresses mode switching, not this init.
     * All original0591 2D and602 control words otherwise remain unchanged. */
    *p++ = KCPType7(0x26, 0);
    *p++ = KCPType4(0x8e04, 1);
    *p++ = 0x04100000u;
    /* Fixed Mesa A6XX static context restore: explicit RB/GRAS modes,
     * both command-level state, not inherited reset values. No power vote. */
    *p++ = KCPType4(0x8811, 1);
    *p++ = 0x10;
    *p++ = KCPType4(0x8110, 1);
    *p++ = 2;
    p = KARFlushEvent(p, 0x1d, KAREventOffset(pass, 0)); /* Color clean */
    p = KARFlushEvent(p, 0x1c, KAREventOffset(pass, 1)); /* Depth clean */
    *p++ = KCPType7(0x46, 1);
    *p++ = 0x19; /* Color invalidate */
    *p++ = KCPType7(0x46, 1);
    *p++ = 0x18; /* Depth invalidate */
    *p++ = KCPType7(0x65, 1);
    *p++ = 12; /* RM6_BLIT2DSCALE */
    *p++ = KCPType7(0x26, 0);
    *p++ = KCPType4(0x8e07, 1);
    /* Sysmem CCU color offset = 3 CCUs * 64 KiB depth, shifted by 12
     * and encoded in bits 23..31. Both cache fractions are FULL. */
    *p++ = 0x18000000u;

    for (unsigned quadrant = 0; quadrant < 4; ++quadrant) {
        const unsigned x = (quadrant % 2) * 16;
        const unsigned y = (quadrant / 2) * 16;
        *p++ = KCPType4(0x8405, 2);
        *p++ = (y << 16) | x;
        *p++ = ((y + 15) << 16) | (x + 15);
        *p++ = KCPType4(0x8c2c, 4);
        for (unsigned channel = 0; channel < 4; ++channel) {
            *p++ = KARColors[quadrant][channel];
        }
        *p++ = KCPType4(0x8c00, 2);
        *p++ = 0x00f03080u; /* Mask RGBA, FMT6_8_8_8_8_UNORM, solid color */
        *p++ = 0; /* RB_A2D_PIXEL_CNTL: blending disabled */
        *p++ = KCPType4(0x8400, 1);
        *p++ = 0x00f03080u;
        *p++ = KCPType4(0xacc0, 1);
        *p++ = 0x0000f180u; /* SP output: float IFMT, same format, RGBA mask */
        *p++ = KCPType4(0x8c17, 4);
        *p++ = 0x30; /* RGBA8, TILE6_LINEAR, WZYX, no UBWC */
        *p++ = KAR_TARGET_IOVA + KARTargetOffset(pass);
        *p++ = 0;
        *p++ = KAR_PITCH >> 6;
        *p++ = KCPType7(0x46, 1);
        *p++ = 0x3f; /* DEBUG_LABEL */
        *p++ = KCPType7(0x2c, 1);
        *p++ = 3; /* BLIT_OP_SCALE */
    }

    /* CP_BLIT writes through the color CCU. Preserve its independent
     * hardware timestamp as well as the qualified CP completion fence. */
    p = KARFlushEvent(p, 0x1d, KAREventOffset(pass, 2));
    *p++ = KCPType7(0x26, 0);
    /* Standard CP_REG_TO_MEM, one dword, no ACCUMULATE/64B. Copy to
     * unused private NC fence slots only, after the terminal color clean.
     * Raw diagnostics are not pixels and do not relax any render check. */
    for (unsigned index = 0; index < KAR_DIAGNOSTIC_COUNT; ++index) {
        *p++ = KCPType7(0x3e, 3);
        *p++ = KARDiagnosticRegisters[index] | (1u << 18);
        *p++ = KCP_FENCE_IOVA + KARDiagnosticOffset(pass, index);
        *p++ = 0;
    }
    *p++ = KCPType7(0x12, 0); /* CP_WAIT_MEM_WRITES before completion fence */
    KCPFenceWords(p, pass + 3);
    return 0;
}

/* Validate the complete private BO after each pass. The second target must
 * still be untouched after the first pass, and both must match after pass 2. */
static inline uint32_t
KARFirstMismatch(const uint8_t (*bo)[KAR_BO_BYTES], unsigned completed)
{
    if (!bo || completed > KAR_PASSES) {
        return 0;
    }
    for (uint32_t offset = 0; offset < KAR_BO_BYTES; ++offset) {
        uint8_t expected = KAR_SENTINEL;
        for (unsigned pass = 0; pass < completed; ++pass) {
            const uint32_t control = offset - (KAR_CONTROL_OFFSET + pass * KAR_TARGET_SPACING);
            if (control < 4) {
                expected = (uint8_t)((KAR_CONTROL_VALUE + pass) >> (control * 8));
            }
            const uint32_t relative = offset - KARTargetOffset(pass);
            if (relative < KAR_TARGET_BYTES && relative % KAR_PITCH < KAR_WIDTH * 4) {
                const unsigned x = relative % KAR_PITCH / 4;
                const unsigned y = relative / KAR_PITCH;
                expected = KARColors[(y / 16) * 2 + x / 16][relative % 4];
            }
        }
        if ((*bo)[offset] != expected) {
            return offset;
        }
    }
    return KAR_BO_BYTES;
}
#endif
