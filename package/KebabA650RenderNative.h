/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_RENDER_NATIVE_H
#define KEBAB_A650_RENDER_NATIVE_H
#include "KebabA650CpNative.h"
#include <kern/KebabA650RenderCore.h>

struct KARSample {
    uint32_t wptr, rptr, shadow, fence, marker;
    uint32_t status, rbbm_irq, irq_mask, cp_fault, cp_irq, cp_protect;
    uint32_t ahb_fence, gfsr, fsr, sqe, timestamp[3][2];
};

struct KARPass {
    uint32_t commands[KAR_WORDS];
    KARSample terminal;
    uint32_t submitted, polls, captured, mismatch;
    uint32_t diagnostics[KAR_DIAGNOSTIC_COUNT];
};

struct KebabA650RenderObservation {
    uint32_t magic, schema, bytes, result, touched, quarantined, completed, nc_verified;
    uint64_t epoch, elapsed_us;
    KARSample initial;
    KARPass pass[KAR_PASSES];
    KGDiscovery vm_before, vm_after;
    uint8_t readback[KAR_PASSES][KAR_BO_BYTES];
};

static_assert(sizeof(KARPass) == 1020, "CAP38 schema6 pass1020");
static_assert(offsetof(KebabA650RenderObservation, vm_before) == 2176,
    "CAP38 schema6 aligned VM");
static_assert(offsetof(KebabA650RenderObservation, readback) == 5440,
    "CAP38 schema6 full target snapshots");
static_assert(sizeof(KebabA650RenderObservation) == 136512, "CAP38 schema6 bytes");

extern "C" int kebab_a650_gpu_render_prepare(void);
extern "C" int kebab_a650_gpu_render_snapshot(KebabA650RenderObservation *);
#endif
