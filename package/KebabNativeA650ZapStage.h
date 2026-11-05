/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_A650_ZAP_STAGE_H
#define KEBAB_NATIVE_A650_ZAP_STAGE_H
#include <pexpert/device_tree.h>
#include "KebabA650ZapCore.h"

/* CPU staging before GPU VM publication only. The old namespace gate is
 * mandatory and unchanged. No PAS request, address-aperture qualification,
 * HYP transfer, secure-exit command or firmware execution is performed. */
static uint32_t kzs_state;
static kebab_zap_stage_observation kzs_observation;
static uint8_t *kzs_input;
static_assert(sizeof(kebab_zap_stage_observation) == 256, "CAP44 stage256");

static void kzs_hash(const void *data, size_t size, uint8_t out[32])
{
    SHA256_CTX context;
    SHA256_Init(&context); SHA256_Update(&context, data, size);
    SHA256_Final(out, &context);
}

static int kzs_pages(unsigned index, const kebab_low_dma_nc_observation *parent,
    uint64_t start)
{
    IOMemoryDescriptor *memory = klnc_memory[index];
    IOMemoryMap *map = klnc_mapping[index];
    const uint64_t pa = index ? parent->metadata : parent->image;
    const uint64_t bytes = index ? parent->metadata_bytes : parent->image_bytes;
    const uintptr_t va = index ? parent->metadata_va : parent->image_va;
    if (!memory || !map || memory->getLength() != bytes || map->getLength() != bytes ||
        map->getVirtualAddress() != va || !va || (va & 16383) || va > UINTPTR_MAX - bytes) return 3;
    for (uint32_t offset = 0; offset < bytes; offset += 4096) {
        if (!kram_budget(start)) return 7;
        IOByteCount extent = 0;
        if (memory->getPhysicalSegment(offset, &extent, kIOMemoryMapperNone) != pa + offset || extent < 4096) return 4;
        const uint64_t par = klnc_translate(va + offset);
        if ((par & 1) || (par & 0x00ffffff00000000ULL) ||
            (par & 0x0000fffffffff000ULL) != pa + offset || (par >> 56) != 0x44) return 5;
        ++kzs_observation.checked_pages;
    }
    return 0;
}

static int kzs_current_input(void)
{
    DTEntry entry = NULL; const void *metadata = NULL, *load = NULL;
    unsigned metadata_bytes = 0, load_bytes = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &entry) != kSuccess || !entry ||
        SecureDTGetProperty(entry, "kebab-gpu-zap-metadata", &metadata, &metadata_bytes) != kSuccess ||
        SecureDTGetProperty(entry, "kebab-gpu-zap-load", &load, &load_bytes) != kSuccess ||
        !metadata || !load || metadata_bytes != 6860 || load_bytes != 1676) return 6;
    kzs_input = (uint8_t *)IOMalloc(6860 + 1676);
    if (!kzs_input) return 12;
    memcpy(kzs_input, metadata, 6860); memcpy(kzs_input + 6860, load, 1676);
    kzs_hash(kzs_input, 6860, kzs_observation.input_metadata_sha256);
    kzs_hash(kzs_input + 6860, 1676, kzs_observation.input_load_sha256);
    static const uint8_t metadata_sha[32] = {0xe2,0xde,0xf9,0xeb,0x44,0x84,0x66,0xbd,0xc3,0x45,0x57,0x3d,0x53,0x11,0x2e,0xcf,0x1f,0x22,0xca,0x24,0x5e,0xb8,0xd2,0xa2,0x90,0x3e,0xcd,0xa7,0x8e,0x43,0x1b,0x85};
    static const uint8_t load_sha[32] = {0xa4,0x15,0xe5,0x45,0x2f,0xa8,0xf5,0x97,0x67,0x0a,0x6e,0x51,0x01,0x0e,0x97,0xbf,0xab,0xca,0x7d,0x20,0xfb,0xce,0x80,0x44,0xca,0xed,0x64,0xd9,0xd5,0x87,0x31,0x13};
    return memcmp(metadata_sha, kzs_observation.input_metadata_sha256, 32) ||
        memcmp(load_sha, kzs_observation.input_load_sha256, 32) ? 8 : 0;
}

extern "C" int kebab_zap_stage_prepare(void)
{
    if (panic_active() || KFNativeWorker(NULL)) return 2;
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&kzs_state, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return expected == 2 ? 0 : 16;
    auto *out = &kzs_observation; *out = {};
    out->magic = KEBAB_ZAP_STAGE_MAGIC; out->schema = 1; out->bytes = sizeof(*out);
    out->state = 2; out->result = 1;
    const uint64_t start = mach_absolute_time(); uint64_t cookie = 0;
    kebab_low_dma_nc_observation parent = {}, again = {};
    kebab_low_dma_ram_observation ram = {};
    KebabZapInputs inputs = {}; KebabZapPlan plan = {};
    int error = 0;
    if (kebab_low_dma_nc_snapshot(&parent, sizeof parent) || parent.magic != KEBAB_LOW_DMA_NC_MAGIC ||
        parent.schema != 1 || parent.bytes != sizeof parent || parent.state != 2 || parent.result ||
        parent.flags != 15 || parent.checked_pages != 260 || parent.image_bytes != 0x100000 || parent.metadata_bytes != 0x4000 ||
        !parent.image || !parent.metadata || (parent.image & 0xfffff) || (parent.metadata & 0x3fff) ||
        parent.image > 0x100000000ULL - parent.image_bytes || parent.metadata > 0x100000000ULL - parent.metadata_bytes ||
        !(parent.image + parent.image_bytes <= parent.metadata || parent.metadata + parent.metadata_bytes <= parent.image)) goto done;
    if (kebab_low_dma_ram_snapshot(&ram, sizeof ram) || ram.magic != KEBAB_LOW_DMA_RAM_MAGIC || ram.schema != 1 ||
        ram.bytes != sizeof ram || ram.state != 2 || ram.result || ram.phase != 8 || ram.flags != 127 || ram.guard ||
        ram.checked_pages != 260 || ram.barriers != 4 || ram.pattern_written_words != 266240 || ram.pattern_read_words != 266240 ||
        ram.zero_written_words != 266240 || ram.zero_read_words != 266240 ||
        ram.image != parent.image || ram.metadata != parent.metadata || ram.image_bytes != parent.image_bytes || ram.metadata_bytes != parent.metadata_bytes) goto done;
    out->flags |= 1; out->phase = 1;
    error = kzs_current_input(); if (error) {out->result = error; goto done;}
    inputs = {{kzs_input,6860},{{kzs_input,148},{kzs_input+148,6712},{kzs_input+6860,1676}}};
    error = KebabA650ZapPlan(&inputs, &plan);
    if (error || plan.metadata_bytes != 6860 || plan.load_file_bytes != 1676 || plan.logical_region_bytes != 4096 ||
        plan.allocation_bytes != 0x100000 || plan.region_alignment != 0x100000) {out->result = 9; goto done;}
    out->flags |= 2; out->phase = 2;
    error = kebab_gpu_namespace_change_begin(KF_OP_PAS13_SEPARATE_IMAGE, &cookie);
    if (error || cookie != UINT64_MAX) {out->result = 10; out->guard = error; goto done;}
    out->flags |= 4;
    out->image = parent.image; out->image_bytes = parent.image_bytes;
    out->metadata = parent.metadata; out->metadata_bytes = parent.metadata_bytes;
    for (unsigned i = 0; i < 2; ++i) {error = kzs_pages(i, &parent, start); if (error) {out->result = error; goto release;}}
    out->flags |= 8; out->phase = 3;
    out->input_metadata_bytes = 6860; out->load_bytes = 1676; out->logical_bytes = 4096; out->allocation_bytes = 0x100000;
    /* All bounds, input hashes and ownership checks precede the first write.
     * Retain the input snapshot and dedicated memory after any refusal. */
    for (unsigned i = 0; i < 2; ++i) {
        const uintptr_t va = i ? parent.metadata_va : parent.image_va;
        const uint32_t bytes = i ? 0x4000 : 0x100000;
        const uint8_t *source = i ? kzs_input : kzs_input + 6860;
        const uint32_t source_bytes = i ? 6860 : 1676;
        for (uint32_t off = 0; off < bytes; ++off) {
            if (!(off & 4095) && !kram_budget(start)) {out->result = 7; goto release;}
            out->flags |= 16;
            *(volatile uint8_t *)(va + off) = off < source_bytes ? source[off] : 0;
            ++out->copied_bytes;
        }
        __asm__ volatile("dsb sy" ::: "memory"); ++out->barriers;
        for (uint32_t off = 0; off < bytes; ++off) {
            if (!(off & 4095) && !kram_budget(start)) {out->result = 7; goto release;}
            if (*(volatile const uint8_t *)(va + off) != (off < source_bytes ? source[off] : 0)) {out->result = 11; goto release;}
            ++out->verified_bytes;
        }
        kzs_hash((const void *)va, bytes, i ? out->staged_metadata_sha256 : out->staged_image_sha256);
    }
    if (!kram_budget(start) || kebab_low_dma_nc_snapshot(&again, sizeof again) || memcmp(&again, &parent, sizeof parent)) {out->result = 13; goto release;}
    out->flags |= 32; out->phase = 4; out->result = 0;
release:
    error = kebab_gpu_namespace_change_end(cookie, 0); out->guard = error;
    if (error && !out->result) out->result = 14;
done:
    out->elapsed_us = kram_elapsed(start) / 1000;
    if (!out->result && out->elapsed_us > 2000000) out->result = 7;
    if (out->result && (out->flags & 16)) out->flags |= 64;
    __atomic_store_n(&kzs_state, 2, __ATOMIC_RELEASE); return 0;
}

extern "C" int kebab_zap_stage_snapshot(kebab_zap_stage_observation *out, uint32_t bytes)
{
    if (!out || bytes != sizeof(*out) || (uintptr_t)out > UINTPTR_MAX - bytes) return 22;
    if (__atomic_load_n(&kzs_state, __ATOMIC_ACQUIRE) != 2) return 35;
    const uintptr_t a = (uintptr_t)out, b = (uintptr_t)&kzs_observation;
    if (a < b + bytes && b < a + bytes) return 22;
    memcpy(out, &kzs_observation, bytes); return 0;
}
#endif
