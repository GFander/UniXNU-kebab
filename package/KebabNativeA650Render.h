/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_RENDER_H
#define KEBAB_NATIVE_A650_RENDER_H

#include "KebabA650RenderNative.h"
#include <kern/KebabA650RenderBinding.h>

static KebabA650RenderObservation *kar_observation;
static uint32_t kar_once;
static int kar_result = EAGAIN;
extern "C" int kebab_rpmh_gpu_cp_first_render(KebabA650CpOwnerToken *, uint32_t *);

static uint32_t
KARReadGpu(uint32_t word)
{
    return *(volatile const uint32_t *)(kcp_native.base + word * 4);
}

static void
KARAcquireNC()
{
#if defined(__aarch64__)
    __asm__ volatile("dmb oshld" ::: "memory");
#else
    OSMemoryBarrier();
#endif
}

/* Read only the proven CP registers, owned CB1/global fault status and the
 * fixed NC fence/scratch slots. All GPU reads follow the same-owner gate. */
static int
KARSampleHardware(KARSample *sample, unsigned pass)
{
    int error = KCPNativeGate(&kcp_native);
    if (error) {
        return error;
    }
    const auto *fence = (volatile const uint32_t *)kvg_engine.backing.region[5].cpu;
    sample->fence = fence[1];
    KARAcquireNC();
    sample->shadow = fence[0];
    sample->marker = fence[16];
    for (unsigned event = 0; event < 3; ++event) {
        unsigned index = KAREventOffset(pass, event) / sizeof(*fence);
        sample->timestamp[event][0] = fence[index];
        sample->timestamp[event][1] = fence[index + 1];
    }

    sample->wptr = KARReadGpu(KCP_CP_RB_WPTR);
    sample->rptr = KARReadGpu(KCP_CP_RB_RPTR);
    sample->status = KARReadGpu(KCP_RBBM_STATUS);
    sample->rbbm_irq = KARReadGpu(KCP_RBBM_INT_0_STATUS);
    sample->irq_mask = KARReadGpu(KCP_RBBM_INT_0_MASK);
    sample->cp_fault = KARReadGpu(KCP_CP_HW_FAULT);
    sample->cp_irq = KARReadGpu(KCP_CP_INTERRUPT_STATUS);
    sample->cp_protect = KARReadGpu(KCP_CP_PROTECT_STATUS);
    sample->sqe = KARReadGpu(KCP_CP_SQE_CNTL);
    error = KCPNativeRead(&kcp_native, 1, 0x24c48, &sample->ahb_fence);
    if (!error) {
        error = KHO_VM_R32(&kvg_native.owner, 0x48, &sample->gfsr);
    }
    if (!error) {
        error = KHO_VM_R32(&kvg_native.owner, 0x9058, &sample->fsr);
    }
    if (error) {
        return error;
    }

    /* A6XX RBBM AHB/GPC/CP errors, hang and UCHE OOB/trap. Retain the
     * complete raw status, including non-fault CCU and CACHE_FLUSH_TS bits. */
    constexpr uint32_t rbbm_faults = (1u << 1) | (1u << 7) | (1u << 9) |
        (1u << 23) | (1u << 24) | (1u << 25);
    if ((sample->rbbm_irq & rbbm_faults) || sample->cp_fault ||
        (sample->cp_irq & 0xf7u) || sample->cp_protect || (sample->ahb_fence & 1u) ||
        sample->gfsr || KFContextFaultBits(sample->fsr) ||
        sample->marker != KCP_MARKER || sample->irq_mask || sample->sqe != 1) {
        return EIO;
    }
    return 0;
}

static int
KARRunPass(KebabA650RenderObservation *record, unsigned pass, uint64_t begin)
{
    KARPass *proof = &record->pass[pass];
    const uint32_t start = KAR_RING_START + pass * KAR_WORDS;
    const uint32_t end = start + KAR_WORDS;
    const uint32_t sequence = pass + 3;
    static_assert(KAR_RING_START + KAR_PASSES * KAR_WORDS < 1024, "no ring wrap");
    KARSample prior = {};
    int error = KARSampleHardware(&prior, pass);
    if (error) {
        return error;
    }
    if (prior.wptr != start || prior.rptr != start || prior.shadow != start ||
        prior.fence != sequence - 1 || (prior.status & ~1u)) {
        return EIO;
    }
    for (const auto &timestamp : prior.timestamp) {
        if (timestamp[0] || timestamp[1]) {
            return EIO;
        }
    }
    const auto *fence = (volatile const uint32_t *)kvg_engine.backing.region[5].cpu;
    static_assert(KAR_DIAGNOSTIC_OFFSET >
        KAR_EVENT_OFFSET + KAR_EVENT_PASS_SPACING + 2 * KAR_EVENT_SPACING + 8,
        "diagnostics do not overlap CP or CCU slots");
    static_assert(KAR_DIAGNOSTIC_OFFSET + KAR_DIAGNOSTIC_SPACING +
        KAR_DIAGNOSTIC_COUNT * 4 <= 4096, "diagnostics fit owned fence BO");
    for (unsigned index = 0; index < KAR_DIAGNOSTIC_COUNT; ++index) {
        if (fence[KARDiagnosticOffset(pass, index) / sizeof(*fence)] !=
            KARDiagnosticSentinel(pass, index)) {
            return EIO;
        }
    }

    const uintptr_t ring = kvg_engine.backing.region[0].cpu;
    error = KARBuildWords(pass, &proof->commands);
    if (error) {
        return error;
    }
    memcpy((void *)(ring + start * 4), proof->commands, sizeof proof->commands);
    flush_dcache((vm_offset_t)ring, 4096, 0);
    KCPNativeBarrier(&kcp_native);
    error = KCPNativeGate(&kcp_native);
    if (error) {
        return error;
    }

    /* The sole GPU MMIO store in a render pass. The existing 148-store CP
     * contract stays intact, and there is no retry or reset on failure. */
    proof->submitted = end;
    *(volatile uint32_t *)(kcp_native.base + KCP_CP_RB_WPTR * 4) = end;
    KCPNativeBarrier(&kcp_native);
    const uint64_t submitted_at = KVGNativeNow(NULL);
    for (unsigned poll = 0; poll < 1000; ++poll) {
        const uint64_t now = KVGNativeNow(NULL);
        if (now < submitted_at || now - submitted_at >= 100000 ||
            now < begin || now - begin >= 500000) {
            return ETIMEDOUT;
        }
        ++proof->polls;
        auto *sample = &proof->terminal;
        error = KARSampleHardware(sample, pass);
        if (error) {
            return error;
        }
        if (sample->wptr != end || sample->rptr < start || sample->rptr > end ||
            sample->shadow < start || sample->shadow > end ||
            sample->fence < sequence - 1 || sample->fence > sequence) {
            return EIO;
        }
        if (sample->fence == sequence && sample->rptr == end && sample->shadow == end &&
            !(sample->status & ~1u) && (sample->timestamp[2][0] || sample->timestamp[2][1])) {
            /* The terminal color CCU timestamp and the CP fence must both
             * be observed before CPU reads from the noncacheable target. */
            KARAcquireNC();
            /* Preserve actual state even if the first pixel fails below.
             * No whole-word register equality gate or synthesized readback. */
            for (unsigned index = 0; index < KAR_DIAGNOSTIC_COUNT; ++index) {
                proof->diagnostics[index] =
                    fence[KARDiagnosticOffset(pass, index) / sizeof(*fence)];
            }
            const auto *target = (volatile const uint8_t *)kvg_engine.backing.region[4].cpu;
            for (uint32_t i = 0; i < KAR_BO_BYTES; ++i) {
                record->readback[pass][i] = target[i];
            }
            KARAcquireNC();
            proof->captured = KAR_BO_BYTES;
            proof->mismatch = KARFirstMismatch(&record->readback[pass], pass + 1);
            if (proof->mismatch != KAR_BO_BYTES) {
                return EIO;
            }
            record->completed = pass + 1;
            return KCPNativeGate(&kcp_native);
        }
        IODelay(100);
    }
    return ETIMEDOUT;
}

extern "C" int
kebab_a650_gpu_render_locked(uint32_t *touched)
{
    auto *record = kar_observation;
    if (!touched || !record || *touched || !KARSourceBound() ||
        __atomic_load_n(&kar_once, __ATOMIC_ACQUIRE) != 1 ||
        __atomic_load_n(&kcp_native_once, __ATOMIC_ACQUIRE) != 2 || kcp_native_result ||
        !kcp_native.observation || !kcp_native.observation->receipt.NC_verified) {
        return ENOTSUP;
    }
    int error = KCPNativeGate(&kcp_native);
    if (error) {
        return error;
    }
    const auto &backing = kvg_engine.backing;
    for (unsigned region = 4; region <= 5; ++region) {
        const auto &descriptor = backing.region[region];
        for (uint32_t off = 0; off < descriptor.allocation_bytes; off += PAGE_SIZE) {
            const auto pp = (ppnum_t)((descriptor.physical + off) >> PAGE_SHIFT);
            if ((pmap_cache_attributes(pp) & VM_WIMG_MASK) != VM_WIMG_WCOMB) {
                return EACCES;
            }
        }
    }
    record->nc_verified = 1;
    record->epoch = kcp_native.observation->receipt.epoch;
    error = KHO_VM_Snapshot(&kvg_native.owner, &record->vm_before);
    if (!error) {
        error = KARSampleHardware(&record->initial, 0);
    }
    if (error) {
        return error;
    }
    if (record->initial.wptr != KAR_RING_START || record->initial.rptr != KAR_RING_START ||
        record->initial.shadow != KAR_RING_START || record->initial.fence != 2 ||
        (record->initial.status & ~1u)) {
        return EIO;
    }
    auto *target = (volatile uint8_t *)backing.region[4].cpu;
    auto *fence = (volatile uint32_t *)backing.region[5].cpu;
    const uint64_t begin = KVGNativeNow(NULL);
    *touched = record->touched = 1;
    for (uint32_t i = 0; i < KAR_BO_BYTES; ++i) {
        target[i] = KAR_SENTINEL;
    }
    for (unsigned pass = 0; pass < KAR_PASSES; ++pass) {
        for (unsigned index = 0; index < KAR_DIAGNOSTIC_COUNT; ++index) {
            fence[KARDiagnosticOffset(pass, index) / sizeof(*fence)] =
                KARDiagnosticSentinel(pass, index);
        }
        for (unsigned event = 0; event < 3; ++event) {
            unsigned index = KAREventOffset(pass, event) / sizeof(*fence);
            fence[index] = 0;
            fence[index + 1] = 0;
        }
    }
    KCPNativeBarrier(&kcp_native);
    for (uint32_t i = 0; i < KAR_BO_BYTES; ++i) {
        if (target[i] != KAR_SENTINEL) {
            return EIO;
        }
    }

    for (unsigned pass = 0; pass < KAR_PASSES && !error; ++pass) {
        error = KARRunPass(record, pass, begin);
    }
    const int snapshot = KHO_VM_Snapshot(&kvg_native.owner, &record->vm_after);
    const uint64_t end = KVGNativeNow(NULL);
    record->elapsed_us = end >= begin ? end - begin : UINT64_MAX;
    if (error) {
        return error;
    }
    if (snapshot) {
        return snapshot;
    }
    return record->elapsed_us < 500000 ? KCPNativeGate(&kcp_native) : ETIMEDOUT;
}

extern "C" int
kebab_a650_gpu_render_prepare(void)
{
    int error = KFNativeWorker(NULL);
    if (error) {
        return error;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&kar_once, &expected, 1, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return expected == 2 ? kar_result : EBUSY;
    }

    kar_observation = (KebabA650RenderObservation *)IOMalloc(sizeof(*kar_observation));
    if (!kar_observation) {
        error = ENOMEM;
    } else {
        auto *record = kar_observation;
        memset(record, 0, sizeof(*record));
        record->magic = 0x3152414bu; /* KAR1 */
        record->schema = KAR_SCHEMA;
        record->bytes = sizeof(*record);
        uint32_t touched = 0;
        error = kebab_rpmh_gpu_cp_first_render(&kcp_native.owner, &touched);
        record->quarantined = error && touched;
        record->result = error;
    }
    kar_result = error;
    __atomic_store_n(&kar_once, 2, __ATOMIC_RELEASE);
    return error;
}

extern "C" int
kebab_a650_gpu_render_snapshot(KebabA650RenderObservation *out)
{
    if (!out) {
        return EINVAL;
    }
    if (__atomic_load_n(&kar_once, __ATOMIC_ACQUIRE) != 2) {
        return EAGAIN;
    }
    if (!kar_observation) {
        return kar_result;
    }
    memcpy(out, kar_observation, sizeof(*out));
    return 0;
}
#endif
