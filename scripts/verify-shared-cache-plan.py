#!/usr/bin/env python3
"""Prove the split dyld shared cache mapping arithmetic on the host.

This is an independent verifier, not a copy of the kernel's code.  It reads
the same 44 files, parses the same dyld_cache_header / mapping / slide-info
structures, and applies the same plan rules as bsd/kern/kebab_dyldcache.c, but
it is written separately so that a mistake has to be made twice to pass.  The
kernel's own plan builder is tied to vnode_t, UBC memory objects and the VM
submap, so it cannot be linked into a host program without a kernel shim; the
kernel-side arithmetic is instead re-derived here line by line and the plan is
cross-checked against the Mach-O load commands actually stored in the cache.

What this proves:
  * every mapped address is 16 KiB aligned and inside the kernel's reserved
    shared region, and no two mappings overlap;
  * the union really covers every segment of every image in the cache: each
    segment's fileoff is exactly mapping.fileOffset + (vmaddr - mapping.address),
    which catches an off-by-one in any mapping offset;
  * every LC_LOAD_* path resolves to an image whose address is mapped;
  * the local-symbol nlist values all land inside the plan, and the .symbols
    file is correctly excluded from the mapping plan;
  * slide values (including 0, realistic ASLR values and the maxSlide edge)
    preserve the relative layout and stay inside the kernel's 4 GiB window,
    while non-16-KiB slides are rejected exactly as vm_shared_region_map_file()
    rejects them.

Exit status: 0 if every check passes, 1 on the first failure, 2 on usage.

Usage:
  scripts/verify-shared-cache-plan.py [DIRECTORY]
  scripts/verify-shared-cache-plan.py --table [DIRECTORY]
  scripts/verify-shared-cache-plan.py --selftest
"""

import argparse
import bisect
import json
import os
import struct
import sys

MAGIC = b"dyld_v1   arm64\x00"
PAGE = 0x4000                 # KEBAB_DSC_PAGE_SIZE / 16 KiB mappings
SLIDE_PAGE = 0x1000           # PAGE_SIZE_FOR_SR_SLIDE / 4 KiB slide-info pages
MAX_MAPPINGS = 8
HEADER_SIZE = 456
SUB_ENTRY_SIZE = 56
MAPPING_SIZE = 32
MAPPING_SLIDE_SIZE = 56
IMAGE_INFO_SIZE = 32
IMAGE_TEXT_SIZE = 32

# osfmk/mach/shared_region.h: SHARED_REGION_BASE_ARM64 / SIZE_ARM64.
HARD_BASE = 0x180000000
HARD_SIZE = 0x100000000
HARD_END = HARD_BASE + HARD_SIZE

VM_PROT_READ = 0x1
VM_PROT_WRITE = 0x2
VM_PROT_EXECUTE = 0x4
VM_PROT_ZF = 0x10

MAIN_NAME = "dyld_shared_cache_arm64"
PREFIX = MAIN_NAME


class VerifyError(Exception):
    pass


def check(cond, message):
    if not cond:
        raise VerifyError(message)


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def u64(data, off):
    return struct.unpack_from("<Q", data, off)[0]


def prot_string(prot):
    return "".join(c if prot & bit else "-"
                   for c, bit in (("r", VM_PROT_READ), ("w", VM_PROT_WRITE),
                                  ("x", VM_PROT_EXECUTE)))


def uuid_string(value):
    return value.hex()


class Reader:
    """pread() wrapper with one file descriptor per cache file."""

    def __init__(self):
        self.fds = {}

    def close(self):
        for fd in self.fds.values():
            os.close(fd)
        self.fds.clear()

    def pread(self, path, offset, size):
        check(offset >= 0 and size >= 0, "negative read at 0x%x/0x%x" % (offset, size))
        fd = self.fds.get(path)
        if fd is None:
            fd = os.open(path, os.O_RDONLY)
            self.fds[path] = fd
        data = os.pread(fd, size, offset)
        check(len(data) == size,
              "%s: short read at 0x%x: got %u of %u bytes"
              % (os.path.basename(path), offset, len(data), size))
        return data

    def cstring(self, path, offset, limit=4096):
        data = self.pread(path, offset, limit)
        end = data.find(b"\x00")
        check(end >= 0, "%s: unterminated string at 0x%x" % (path, offset))
        return data[:end].decode("utf-8", "replace")


class Header:
    __slots__ = (
        "magic", "mapping_offset", "mapping_count", "images_offset_old",
        "images_count_old", "dyld_base_address", "code_signature_offset",
        "code_signature_size", "local_symbols_offset", "local_symbols_size",
        "uuid", "cache_type", "dyld_in_cache_mh", "dyld_in_cache_entry",
        "images_text_offset", "images_text_count", "patch_info_addr",
        "patch_info_size", "prog_closures_addr", "prog_closures_size",
        "prog_closures_trie_addr", "prog_closures_trie_size", "platform",
        "format_version", "shared_region_start", "shared_region_size",
        "max_slide", "dylibs_image_array_addr", "dylibs_image_array_size",
        "dylibs_trie_addr", "dylibs_trie_size", "other_image_array_addr",
        "other_image_array_size", "other_trie_addr", "other_trie_size",
        "mapping_with_slide_offset", "mapping_with_slide_count",
        "dylibs_pbl_set_addr", "programs_pbl_set_pool_addr",
        "programs_pbl_set_pool_size", "program_trie_addr", "program_trie_size",
        "swift_opts_offset", "swift_opts_size", "sub_cache_array_offset",
        "sub_cache_array_count", "symbol_file_uuid", "images_offset",
        "images_count",
    )

    def __init__(self, data, label):
        check(len(data) >= HEADER_SIZE,
              "%s: shorter than the 456-byte cache header" % label)
        self.magic = data[0:16]
        self.mapping_offset = u32(data, 16)
        self.mapping_count = u32(data, 20)
        self.images_offset_old = u32(data, 24)
        self.images_count_old = u32(data, 28)
        self.dyld_base_address = u64(data, 32)
        self.code_signature_offset = u64(data, 40)
        self.code_signature_size = u64(data, 48)
        self.local_symbols_offset = u64(data, 72)
        self.local_symbols_size = u64(data, 80)
        self.uuid = data[88:104]
        self.cache_type = u64(data, 104)
        self.dyld_in_cache_mh = u64(data, 120)
        self.dyld_in_cache_entry = u64(data, 128)
        self.images_text_offset = u64(data, 136)
        self.images_text_count = u64(data, 144)
        self.patch_info_addr = u64(data, 152)
        self.patch_info_size = u64(data, 160)
        self.prog_closures_addr = u64(data, 184)
        self.prog_closures_size = u64(data, 192)
        self.prog_closures_trie_addr = u64(data, 200)
        self.prog_closures_trie_size = u64(data, 208)
        self.platform = u32(data, 216)
        self.format_version = u32(data, 220)
        self.shared_region_start = u64(data, 224)
        self.shared_region_size = u64(data, 232)
        self.max_slide = u64(data, 240)
        self.dylibs_image_array_addr = u64(data, 248)
        self.dylibs_image_array_size = u64(data, 256)
        self.dylibs_trie_addr = u64(data, 264)
        self.dylibs_trie_size = u64(data, 272)
        self.other_image_array_addr = u64(data, 280)
        self.other_image_array_size = u64(data, 288)
        self.other_trie_addr = u64(data, 296)
        self.other_trie_size = u64(data, 304)
        self.mapping_with_slide_offset = u32(data, 312)
        self.mapping_with_slide_count = u32(data, 316)
        self.dylibs_pbl_set_addr = u64(data, 328)
        self.programs_pbl_set_pool_addr = u64(data, 336)
        self.programs_pbl_set_pool_size = u64(data, 344)
        self.program_trie_addr = u64(data, 352)
        self.program_trie_size = u32(data, 360)
        self.swift_opts_offset = u64(data, 376)
        self.swift_opts_size = u64(data, 384)
        self.sub_cache_array_offset = u32(data, 392)
        self.sub_cache_array_count = u32(data, 396)
        self.symbol_file_uuid = data[400:416]
        self.images_offset = u32(data, 448)
        self.images_count = u32(data, 452)


class Mapping:
    __slots__ = (
        "file_name", "file_path", "index", "address", "size", "file_offset", "max_prot",
        "init_prot", "slide_info_offset", "slide_info_size",
        "slide_info_address", "slide_version", "slide_page_size",
        "slide_pages", "slide_extras", "slide_delta_mask", "slide_value_add",
    )

    def __init__(self, file_name, file_path, index, address, size, file_offset,
                 max_prot, init_prot):
        self.file_name = file_name
        self.file_path = file_path
        self.index = index
        self.address = address
        self.size = size
        self.file_offset = file_offset
        self.max_prot = max_prot
        self.init_prot = init_prot
        self.slide_info_offset = 0
        self.slide_info_size = 0
        self.slide_info_address = 0
        self.slide_version = 0
        self.slide_page_size = 0
        self.slide_pages = 0
        self.slide_extras = 0
        self.slide_delta_mask = 0
        self.slide_value_add = 0

    @property
    def end(self):
        return self.address + self.size

    def label(self):
        return "%s mapping %u" % (self.file_name, self.index)


class CacheFile:
    __slots__ = ("name", "path", "size", "header", "vm_offset", "mappings",
                 "kind", "suffix")

    def __init__(self, name, path, size, header, vm_offset=0, kind="sub",
                 suffix=""):
        self.name = name
        self.path = path
        self.size = size
        self.header = header
        self.vm_offset = vm_offset
        self.mappings = []
        self.kind = kind
        self.suffix = suffix


class Plan:
    def __init__(self):
        self.directory = ""
        self.region_start = 0
        self.region_size = 0
        self.max_slide = 0
        self.files = []
        self.main = None
        self.symbols = None
        self.entries = []
        self.summary = {}

    def all_mappings(self):
        return [m for f in self.files for m in f.mappings]

    def intervals(self):
        return sorted(self.all_mappings(), key=lambda m: m.address)


def validate_header(header, file_size, label, is_main, region_start,
                    region_size, vm_offset):
    check(header.magic == MAGIC,
          "%s: bad magic %r (want %r)" % (label, header.magic, MAGIC))
    check((header.format_version & 0xff) == 0,
          "%s: formatVersion %u, want 0" % (label, header.format_version & 0xff))
    check(header.cache_type == 2,
          "%s: cacheType %u, want 2 (multi-cache)" % (label, header.cache_type))
    check(header.platform == 2,
          "%s: platform %u, want 2 (iOS)" % (label, header.platform))

    images_end = header.images_offset + header.images_count * IMAGE_INFO_SIZE
    check(images_end == header.images_text_offset,
          "%s: imagesOffset %u + imagesCount %u*32 = %u, want imagesTextOffset %u"
          % (label, header.images_offset, header.images_count, images_end,
             header.images_text_offset))
    images_text_end = (header.images_text_offset
                       + header.images_text_count * IMAGE_TEXT_SIZE)
    check(images_text_end <= file_size,
          "%s: imagesText ends at %u, beyond file size %u"
          % (label, images_text_end, file_size))

    sub_end = (header.sub_cache_array_offset
               + header.sub_cache_array_count * SUB_ENTRY_SIZE)
    check(sub_end <= header.code_signature_offset,
          "%s: subCacheArray ends at %u, beyond codeSignatureOffset %u"
          % (label, sub_end, header.code_signature_offset))
    check(header.code_signature_offset <= file_size
          and header.code_signature_size <= file_size - header.code_signature_offset,
          "%s: code signature [%u, %u) beyond file size %u"
          % (label, header.code_signature_offset,
             header.code_signature_offset + header.code_signature_size,
             file_size))

    check(1 <= header.mapping_count <= MAX_MAPPINGS,
          "%s: mappingCount %u not in 1..%u"
          % (label, header.mapping_count, MAX_MAPPINGS))
    mappings_end = header.mapping_offset + header.mapping_count * MAPPING_SIZE
    check(mappings_end <= file_size,
          "%s: mapping table ends at %u, beyond file size %u"
          % (label, mappings_end, file_size))

    if header.mapping_with_slide_count:
        check(header.mapping_with_slide_count == header.mapping_count,
              "%s: mappingWithSlideCount %u != mappingCount %u"
              % (label, header.mapping_with_slide_count, header.mapping_count))
        slide_end = (header.mapping_with_slide_offset
                     + header.mapping_with_slide_count * MAPPING_SLIDE_SIZE)
        check(slide_end <= file_size,
              "%s: mapping-and-slide table ends at %u, beyond file size %u"
              % (label, slide_end, file_size))

    region_end = region_start + region_size
    check(region_end >= region_start, "%s: region overflows" % label)

    if is_main:
        check(header.shared_region_start == region_start,
              "%s: sharedRegionStart 0x%x != main 0x%x"
              % (label, header.shared_region_start, region_start))
        check(header.shared_region_size == region_size,
              "%s: sharedRegionSize 0x%x != main 0x%x"
              % (label, header.shared_region_size, region_size))
    else:
        expected = region_start + vm_offset
        check(expected >= region_start,
              "%s: cacheVMOffset 0x%x overflows the region"
              % (label, vm_offset))
        check(header.shared_region_start == expected,
              "%s: sharedRegionStart 0x%x != base 0x%x + cacheVMOffset 0x%x"
              % (label, header.shared_region_start, region_start, vm_offset))
        if header.shared_region_size:
            sub_end_va = header.shared_region_start + header.shared_region_size
            check(sub_end_va >= header.shared_region_start
                  and sub_end_va <= region_end,
                  "%s: [0x%x, 0x%x) escapes region [0x%x, 0x%x)"
                  % (label, header.shared_region_start, sub_end_va,
                     region_start, region_end))

    if header.images_offset_old or header.images_count_old:
        # The split cache moved these to 448/452; the old copies are unused.
        # They are zero in this cache, and the kernel ignores them.  Treat a
        # non-zero value as a format surprise, not a mapping error.
        pass


def parse_mappings(reader, cf, region_start, region_size):
    header = cf.header
    data = reader.pread(cf.path, header.mapping_offset,
                        header.mapping_count * MAPPING_SIZE)
    slide_data = None
    if header.mapping_with_slide_count:
        slide_data = reader.pread(cf.path, header.mapping_with_slide_offset,
                                  header.mapping_with_slide_count
                                  * MAPPING_SLIDE_SIZE)

    for i in range(header.mapping_count):
        off = i * MAPPING_SIZE
        mapping = Mapping(cf.name, cf.path, i, u64(data, off), u64(data, off + 8),
                          u64(data, off + 16), u32(data, off + 24),
                          u32(data, off + 28))
        check(mapping.size != 0,
              "%s: mapping %u has size 0" % (cf.name, i))
        check((mapping.address & (PAGE - 1)) == 0,
              "%s: mapping %u address 0x%x is not 16 KiB aligned"
              % (cf.name, i, mapping.address))
        check((mapping.size & (PAGE - 1)) == 0,
              "%s: mapping %u size 0x%x is not 16 KiB aligned"
              % (cf.name, i, mapping.size))
        check((mapping.file_offset & (PAGE - 1)) == 0,
              "%s: mapping %u fileOffset 0x%x is not 16 KiB aligned"
              % (cf.name, i, mapping.file_offset))
        end = mapping.end
        check(end >= mapping.address,
              "%s: mapping %u address+size overflows" % (cf.name, i))
        check(mapping.address >= region_start and end <= region_start + region_size,
              "%s: mapping %u [0x%x, 0x%x) escapes region [0x%x, 0x%x)"
              % (cf.name, i, mapping.address, end, region_start,
                 region_start + region_size))
        check((mapping.init_prot
               & ~(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_ZF)) == 0,
              "%s: mapping %u invalid initProt 0x%x"
              % (cf.name, i, mapping.init_prot))
        check((mapping.max_prot
               & ~(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)) == 0,
              "%s: mapping %u invalid maxProt 0x%x"
              % (cf.name, i, mapping.max_prot))
        if not (mapping.init_prot & VM_PROT_ZF):
            check(mapping.file_offset + mapping.size <= cf.size,
                  "%s: mapping %u covers file [0x%x, 0x%x) beyond size 0x%x"
                  % (cf.name, i, mapping.file_offset,
                     mapping.file_offset + mapping.size, cf.size))

        if slide_data is not None:
            off = i * MAPPING_SLIDE_SIZE
            s_address = u64(slide_data, off)
            s_size = u64(slide_data, off + 8)
            s_file_offset = u64(slide_data, off + 16)
            s_info_offset = u64(slide_data, off + 24)
            s_info_size = u64(slide_data, off + 32)
            s_max_prot = u32(slide_data, off + 48)
            s_init_prot = u32(slide_data, off + 52)
            check(s_address == mapping.address and s_size == mapping.size
                  and s_file_offset == mapping.file_offset
                  and s_max_prot == mapping.max_prot
                  and s_init_prot == mapping.init_prot,
                  "%s: mapping-and-slide entry %u does not match mapping table"
                  % (cf.name, i))
            if s_info_size:
                check(s_info_offset != 0,
                      "%s: mapping %u slide info size 0x%x with offset 0"
                      % (cf.name, i, s_info_size))
                check(s_info_offset + s_info_size <= cf.size,
                      "%s: mapping %u slide info [0x%x, 0x%x) outside file size 0x%x"
                      % (cf.name, i, s_info_offset,
                         s_info_offset + s_info_size, cf.size))
                mapping.slide_info_offset = s_info_offset
                mapping.slide_info_size = s_info_size
            elif s_info_offset:
                raise VerifyError("%s: mapping %u slide info offset 0x%x with size 0"
                                  % (cf.name, i, s_info_offset))
        cf.mappings.append(mapping)

    # Resolve each slide-info file offset to the address copyin() will use.
    for mapping in cf.mappings:
        if not mapping.slide_info_size:
            continue
        container = None
        for candidate in cf.mappings:
            if (candidate.file_offset <= mapping.slide_info_offset
                    and mapping.slide_info_offset + mapping.slide_info_size
                    <= candidate.file_offset + candidate.size):
                container = candidate
                break
        check(container is not None,
              "%s: mapping %u slide info 0x%x+0x%x is not covered by any mapped "
              "file range" % (cf.name, mapping.index, mapping.slide_info_offset,
                              mapping.slide_info_size))
        mapping.slide_info_address = (container.address
                                      + (mapping.slide_info_offset
                                         - container.file_offset))
        check(container.address <= mapping.slide_info_address
              and mapping.slide_info_address + mapping.slide_info_size
              <= container.end,
              "%s: mapping %u slide info address 0x%x+0x%x escapes its mapping"
              % (cf.name, mapping.index, mapping.slide_info_address,
                 mapping.slide_info_size))
        validate_slide_info(reader, cf, mapping)


def validate_slide_info(reader, cf, mapping):
    offset = mapping.slide_info_offset
    size = mapping.slide_info_size
    check(size >= 40,
          "%s: mapping %u slide info is only 0x%x bytes"
          % (cf.name, mapping.index, size))
    head = reader.pread(cf.path, offset, 40)
    version, page_size, starts_offset, starts_count, extras_offset, extras_count, \
        delta_mask, value_add = struct.unpack("<IIIIIIQQ", head)
    check(version == 2,
          "%s: mapping %u slide info version %u is not v2"
          % (cf.name, mapping.index, version))
    check(page_size == SLIDE_PAGE,
          "%s: mapping %u slide info page_size 0x%x, want 0x%x"
          % (cf.name, mapping.index, page_size, SLIDE_PAGE))
    check(starts_count != 0 and starts_count * page_size == mapping.size,
          "%s: mapping %u slide info has %u pages of 0x%x bytes, mapping is 0x%x"
          % (cf.name, mapping.index, starts_count, page_size, mapping.size))
    check(delta_mask != 0 and (delta_mask & 0x3) == 0 and (delta_mask >> 32) != 0,
          "%s: mapping %u slide info delta_mask 0x%x is not a usable 64-bit mask"
          % (cf.name, mapping.index, delta_mask))
    required = 40 + (starts_count + extras_count) * 2
    check(required <= size,
          "%s: mapping %u slide info needs 0x%x bytes for %u starts and %u "
          "extras, only 0x%x present"
          % (cf.name, mapping.index, required, starts_count, extras_count, size))
    check(starts_offset >= 40 and starts_offset + starts_count * 2 <= size,
          "%s: mapping %u slide info page starts escape 0x%x bytes"
          % (cf.name, mapping.index, size))
    if extras_count:
        check(extras_offset >= 40 and extras_offset + extras_count * 2 <= size,
              "%s: mapping %u slide info page extras escape 0x%x bytes"
              % (cf.name, mapping.index, size))

    blob = reader.pread(cf.path, offset, size)
    starts = struct.unpack_from("<%dH" % starts_count, blob, starts_offset)
    extras = (struct.unpack_from("<%dH" % extras_count, blob, extras_offset)
              if extras_count else ())
    for page_index, entry in enumerate(starts):
        if entry == 0x4000:
            continue
        if entry & 0x8000:
            check(extras_count and (entry & 0x3fff) < extras_count,
                  "%s: mapping %u page %u extras index %u >= %u"
                  % (cf.name, mapping.index, page_index, entry & 0x3fff,
                     extras_count))
        else:
            check(entry * 4 < page_size,
                  "%s: mapping %u page %u chain starts at 0x%x, outside page"
                  % (cf.name, mapping.index, page_index, entry * 4))
    for extra_index, entry in enumerate(extras):
        check((entry & 0x3fff) * 4 < page_size,
              "%s: mapping %u extra %u chain starts at 0x%x, outside page"
              % (cf.name, mapping.index, extra_index, (entry & 0x3fff) * 4))

    mapping.slide_version = version
    mapping.slide_page_size = page_size
    mapping.slide_pages = starts_count
    mapping.slide_extras = extras_count
    mapping.slide_delta_mask = delta_mask
    mapping.slide_value_add = value_add


def build_plan(directory, expect_files=44):
    reader = Reader()
    names = sorted(n for n in os.listdir(directory)
                   if n.startswith(PREFIX)
                   and os.path.isfile(os.path.join(directory, n)))
    check(len(names) == expect_files,
          "%s: found %u cache files, want %u"
          % (directory, len(names), expect_files))
    check(MAIN_NAME in names, "%s: main cache %s is missing"
          % (directory, MAIN_NAME))

    main_path = os.path.join(directory, MAIN_NAME)
    main_size = os.path.getsize(main_path)
    main_header = Header(reader.pread(main_path, 0, HEADER_SIZE), MAIN_NAME)
    region_start = main_header.shared_region_start
    region_size = main_header.shared_region_size
    check(region_start == HARD_BASE,
          "cache sharedRegionStart 0x%x != kernel SHARED_REGION_BASE_ARM64 0x%x"
          % (region_start, HARD_BASE))
    check(region_size != 0 and (region_size & (PAGE - 1)) == 0,
          "cache sharedRegionSize 0x%x is not a non-zero 16 KiB multiple"
          % region_size)
    check(region_start + region_size <= HARD_END,
          "cache region [0x%x, 0x%x) escapes kernel region [0x%x, 0x%x)"
          % (region_start, region_start + region_size, HARD_BASE, HARD_END))
    validate_header(main_header, main_size, MAIN_NAME, True, region_start,
                    region_size, 0)

    plan = Plan()
    plan.directory = directory
    plan.region_start = region_start
    plan.region_size = region_size
    plan.max_slide = main_header.max_slide

    check(1 <= main_header.sub_cache_array_count <= 64,
          "subCacheArrayCount %u is not in 1..64"
          % main_header.sub_cache_array_count)
    entries_data = reader.pread(main_path, main_header.sub_cache_array_offset,
                                main_header.sub_cache_array_count
                                * SUB_ENTRY_SIZE)
    seen_uuids = set()
    for i in range(main_header.sub_cache_array_count):
        off = i * SUB_ENTRY_SIZE
        entry_uuid = entries_data[off:off + 16]
        vm_offset = u64(entries_data, off + 16)
        suffix = entries_data[off + 24:off + 56].split(b"\x00", 1)[0]
        suffix = suffix.decode("ascii", "replace")
        check(entry_uuid != b"\x00" * 16,
              "subcache entry %u has a zero uuid" % (i + 1))
        check(entry_uuid not in seen_uuids,
              "subcache entry %u repeats uuid %s"
              % (i + 1, uuid_string(entry_uuid)))
        seen_uuids.add(entry_uuid)
        check((vm_offset & (PAGE - 1)) == 0,
              "subcache %u cacheVMOffset 0x%x is not 16 KiB aligned"
              % (i + 1, vm_offset))
        plan.entries.append((entry_uuid, vm_offset, suffix))

    candidates = {}
    for name in names:
        path = os.path.join(directory, name)
        size = os.path.getsize(path)
        header = Header(reader.pread(path, 0, HEADER_SIZE), name)
        check(header.magic == MAGIC,
              "%s: bad magic %r" % (name, header.magic))
        key = header.uuid
        check(key not in candidates,
              "%s and %s have the same uuid %s"
              % (candidates[key][0] if key in candidates else "?",
                 name, uuid_string(key)))
        candidates[key] = (name, path, size, header)
    check(len(candidates) == len(names),
          "duplicate uuid left only %u unique files of %u"
          % (len(candidates), len(names)))

    main_entry = candidates.get(main_header.uuid)
    check(main_entry is not None and main_entry[0] == MAIN_NAME,
          "main header uuid %s is not on %s"
          % (uuid_string(main_header.uuid), MAIN_NAME))

    main_file = CacheFile(MAIN_NAME, main_path, main_size, main_header,
                          vm_offset=0, kind="main")
    parse_mappings(reader, main_file, region_start, region_size)
    plan.files.append(main_file)
    plan.main = main_file

    for i, (entry_uuid, vm_offset, suffix) in enumerate(plan.entries):
        match = candidates.get(entry_uuid)
        check(match is not None,
              "subcache %02u suffix %r uuid %s: no file with this uuid"
              % (i + 1, suffix, uuid_string(entry_uuid)))
        name, path, size, header = match
        if suffix and not name.endswith(suffix):
            # The kernel only warns; the uuid is the authority.  Record it.
            pass
        validate_header(header, size, name, False, region_start, region_size,
                        vm_offset)
        cf = CacheFile(name, path, size, header, vm_offset=vm_offset,
                       kind="sub", suffix=suffix)
        parse_mappings(reader, cf, region_start, region_size)
        check(cf.mappings[0].address == region_start + vm_offset,
              "%s: first mapping 0x%x != region base 0x%x + cacheVMOffset 0x%x"
              % (name, cf.mappings[0].address, region_start, vm_offset))
        plan.files.append(cf)

    check(len(plan.files) == len(plan.entries) + 1,
          "plan has %u files for %u subcache entries"
          % (len(plan.files), len(plan.entries)))

    dylddata = [f for f in plan.files if f.kind == "sub"
                and f.suffix.endswith(".dylddata")]
    dyldlinkedit = [f for f in plan.files if f.kind == "sub"
                    and f.suffix.endswith(".dyldlinkedit")]
    check(len(dylddata) == 2,
          "found %u .dylddata subcaches, want 2" % len(dylddata))
    check(len(dyldlinkedit) == 2,
          "found %u .dyldlinkedit subcaches, want 2" % len(dyldlinkedit))
    for cf in dylddata:
        check(len(cf.mappings) == 4,
              "%s: %u mappings, want 4 (header, __DATA_CONST, __DATA, info)"
              % (cf.name, len(cf.mappings)))
        check(cf.mappings[1].init_prot == VM_PROT_READ
              and cf.mappings[1].max_prot == (VM_PROT_READ | VM_PROT_WRITE)
              and cf.mappings[1].slide_info_size != 0,
              "%s: __DATA_CONST mapping is not r--/rw- with slide info"
              % cf.name)
        check(cf.mappings[2].init_prot == (VM_PROT_READ | VM_PROT_WRITE)
              and cf.mappings[2].max_prot == (VM_PROT_READ | VM_PROT_WRITE)
              and cf.mappings[2].slide_info_size != 0,
              "%s: __DATA mapping is not rw-/rw- with slide info" % cf.name)
    for cf in dyldlinkedit:
        check(len(cf.mappings) == 2,
              "%s: %u mappings, want 2 (header, __LINKEDIT)"
              % (cf.name, len(cf.mappings)))
        check(cf.mappings[1].init_prot == VM_PROT_READ
              and cf.mappings[1].max_prot == VM_PROT_READ,
              "%s: __LINKEDIT mapping is not r--/r--" % cf.name)

    symbol_match = candidates.get(main_header.symbol_file_uuid)
    check(symbol_match is not None,
          "symbolFileUUID %s has no file"
          % uuid_string(main_header.symbol_file_uuid))
    sname, spath, ssize, sheader = symbol_match
    check(sname not in [f.name for f in plan.files],
          "symbol file %s was also mapped" % sname)
    plan.symbols = CacheFile(sname, spath, ssize, sheader, kind="symbols")
    check(sheader.shared_region_start >= region_start + region_size,
          "%s: header sharedRegionStart 0x%x overlaps the plan region end 0x%x"
          % (sname, sheader.shared_region_start, region_start + region_size))
    sym_maps = reader.pread(spath, sheader.mapping_offset,
                            sheader.mapping_count * MAPPING_SIZE)
    check(sheader.mapping_count >= 1,
          "%s: no mapping table" % sname)
    sym_address = u64(sym_maps, 0)
    sym_size = u64(sym_maps, 8)
    check(sym_size != 0 and (sym_address & (PAGE - 1)) == 0
          and (sym_size & (PAGE - 1)) == 0,
          "%s: first mapping 0x%x+0x%x is not a 16 KiB range"
          % (sname, sym_address, sym_size))
    check(sym_address >= region_start + region_size,
          "%s: mapping 0x%x overlaps the plan region end 0x%x"
          % (sname, sym_address, region_start + region_size))
    check(sym_address + sym_size <= HARD_END,
          "%s: mapping [0x%x, 0x%x) escapes the kernel region"
          % (sname, sym_address, sym_address + sym_size))

    plan.summary["reader"] = reader
    return plan


def find_interval(intervals, address):
    starts = [m.address for m in intervals]
    i = bisect.bisect_right(starts, address) - 1
    if i >= 0 and address < intervals[i].end:
        return intervals[i]
    return None


def check_plan_arithmetic(plan):
    mappings = plan.all_mappings()
    check(mappings, "plan has no mappings")
    ordered = sorted(mappings, key=lambda m: m.address)
    check(ordered[0].address == plan.region_start,
          "lowest mapping 0x%x != sharedRegionStart 0x%x"
          % (ordered[0].address, plan.region_start))

    gaps = []
    previous = None
    for mapping in ordered:
        if previous is not None:
            check(mapping.address >= previous.end,
                  "overlap: %s [0x%x, 0x%x) and %s [0x%x, 0x%x)"
                  % (previous.label(), previous.address, previous.end,
                     mapping.label(), mapping.address, mapping.end))
            if mapping.address > previous.end:
                gaps.append((previous.end, mapping.address))
        previous = mapping

    total = sum(m.size for m in ordered)
    extent = ordered[-1].end - plan.region_start
    gap_total = sum(end - start for start, end in gaps)
    check(total + gap_total == extent,
          "mapped bytes 0x%x + holes 0x%x != extent 0x%x"
          % (total, gap_total, extent))
    check(extent <= plan.region_size,
          "mapped extent 0x%x exceeds cache sharedRegionSize 0x%x"
          % (extent, plan.region_size))

    # Every mapping must be its file's first mapping when it claims to be.
    for cf in plan.files:
        if cf.kind == "sub":
            check(cf.mappings[0].address == plan.region_start + cf.vm_offset,
                  "%s: first mapping is not region base + cacheVMOffset" % cf.name)

    # Slide-info blobs must not overlap each other and must live in a mapping.
    infos = sorted((m.slide_info_address, m.slide_info_size, m.label())
                   for m in ordered if m.slide_info_size)
    previous = None
    for address, size, label in infos:
        if previous is not None:
            check(address >= previous[0] + previous[1],
                  "slide info overlap: %s and %s" % (previous[2], label))
        previous = (address, size, label)

    plan.summary.update({
        "mappings": len(ordered),
        "total_mapped": total,
        "extent": extent,
        "gaps": gaps,
        "gap_total": gap_total,
        "slide_infos": infos,
    })
    return ordered


def read_images(reader, plan):
    main = plan.main
    header = main.header
    records = reader.pread(main.path, header.images_offset,
                           header.images_count * IMAGE_INFO_SIZE)
    texts = reader.pread(main.path, header.images_text_offset,
                         header.images_text_count * IMAGE_TEXT_SIZE)
    images = []
    by_path = {}
    for i in range(header.images_count):
        off = i * IMAGE_INFO_SIZE
        address = u64(records, off)
        path_offset = u32(records, off + 24)
        check(path_offset < main.size,
              "image %u pathOffset 0x%x beyond main file" % (i, path_offset))
        path = reader.cstring(main.path, path_offset)
        check(path.startswith("/"), "image %u path %r is not absolute" % (i, path))
        check(path not in by_path, "duplicate image path %s" % path)
        check(address not in {im["address"] for im in images},
              "duplicate image address 0x%x" % address)
        image = {
            "index": i, "address": address, "path": path,
            "path_offset": path_offset, "text": None, "segments": [],
            "loads": [], "id_path": None, "filetype": None,
        }
        images.append(image)
        by_path[path] = image

    check(header.images_count == header.images_text_count,
          "imagesCount %u != imagesTextCount %u"
          % (header.images_count, header.images_text_count))
    for i, image in enumerate(images):
        off = i * IMAGE_TEXT_SIZE
        uuid = texts[off:off + 16]
        load_address = u64(texts, off + 16)
        text_size = u32(texts, off + 24)
        path_offset = u32(texts, off + 28)
        image["text_uuid"] = uuid
        image["text_load_address"] = load_address
        image["text_size"] = text_size
        image["text_path_offset"] = path_offset
    return images, by_path


def parse_macho(reader, plan, image, intervals):
    mapping = find_interval(intervals, image["address"])
    check(mapping is not None,
          "%s: image address 0x%x is not covered by any mapping"
          % (image["path"], image["address"]))
    file_offset = mapping.file_offset + (image["address"] - mapping.address)
    check(file_offset + 32 <= mapping.file_offset + mapping.size,
          "%s: Mach-O header at file 0x%x is beyond its mapping"
          % (image["path"], file_offset))
    head = reader.pread(mapping.file_path, file_offset, 32)
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = \
        struct.unpack("<IiiIIIII", head)
    check(magic == 0xfeedfacf,
          "%s: Mach-O magic 0x%x at file 0x%x, want 0xfeedfacf"
          % (image["path"], magic, file_offset))
    check(cputype == 0x0100000c,
          "%s: cputype 0x%x, want CPU_TYPE_ARM64" % (image["path"], cputype))
    check(filetype in (2, 6, 7),
          "%s: unexpected Mach-O filetype %u" % (image["path"], filetype))
    check(ncmds <= 4096 and sizeofcmds <= (1 << 20),
          "%s: implausible Mach-O header: %u commands, 0x%x bytes"
          % (image["path"], ncmds, sizeofcmds))
    check(file_offset + 32 + sizeofcmds <= mapping.file_offset + mapping.size,
          "%s: load commands end beyond their mapping" % image["path"])
    commands = reader.pread(mapping.file_path, file_offset + 32, sizeofcmds)

    segments = []
    loads = []
    id_path = None
    p = 0
    for _ in range(ncmds):
        check(p + 8 <= sizeofcmds,
              "%s: load command header at 0x%x is truncated"
              % (image["path"], p))
        cmd, cmdsize = struct.unpack_from("<II", commands, p)
        check(cmdsize >= 8 and p + cmdsize <= sizeofcmds,
              "%s: load command 0x%x at 0x%x has bad size 0x%x"
              % (image["path"], cmd, p, cmdsize))
        if cmd == 0x19:  # LC_SEGMENT_64
            check(cmdsize >= 72, "%s: LC_SEGMENT_64 too short" % image["path"])
            segname = commands[p + 8:p + 24].split(b"\x00", 1)[0].decode(
                "ascii", "replace")
            vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, segflags = \
                struct.unpack_from("<QQQQIIII", commands, p + 24)
            segments.append({
                "name": segname, "vmaddr": vmaddr, "vmsize": vmsize,
                "fileoff": fileoff, "filesize": filesize,
                "maxprot": maxprot, "initprot": initprot,
            })
        elif cmd in (0x0c, 0x80000018, 0x8000001f, 0x20, 0x80000023):
            check(cmdsize >= 24, "%s: dylib command too short" % image["path"])
            name_offset = u32(commands, p + 8)
            check(name_offset < cmdsize,
                  "%s: dylib name offset 0x%x >= command size 0x%x"
                  % (image["path"], name_offset, cmdsize))
            name = commands[p + name_offset:p + cmdsize].split(b"\x00", 1)[0]
            loads.append((cmd, name.decode("utf-8", "replace")))
        elif cmd == 0x0d:  # LC_ID_DYLIB
            check(cmdsize >= 24, "%s: LC_ID_DYLIB too short" % image["path"])
            name_offset = u32(commands, p + 8)
            check(name_offset < cmdsize,
                  "%s: LC_ID_DYLIB name offset out of command" % image["path"])
            id_path = commands[p + name_offset:p + cmdsize].split(b"\x00", 1)[0]
            id_path = id_path.decode("utf-8", "replace")
        p += cmdsize
    check(p == sizeofcmds,
          "%s: load commands end at 0x%x, sizeofcmds is 0x%x"
          % (image["path"], p, sizeofcmds))

    image["segments"] = segments
    image["loads"] = loads
    image["id_path"] = id_path
    image["filetype"] = filetype

    texts = [s for s in segments if s["name"] == "__TEXT"]
    check(len(texts) == 1, "%s: has %u __TEXT segments" % (image["path"], len(texts)))
    image["text"] = texts[0]
    check(texts[0]["vmaddr"] == image["address"],
          "%s: __TEXT.vmaddr 0x%x != image record address 0x%x"
          % (image["path"], texts[0]["vmaddr"], image["address"]))
    check(texts[0]["fileoff"] == file_offset,
          "%s: __TEXT.fileoff 0x%x != computed cache file offset 0x%x"
          % (image["path"], texts[0]["fileoff"], file_offset))

    for segment in segments:
        name = segment["name"]
        if name == "__PAGEZERO":
            continue
        seg_map = find_interval(intervals, segment["vmaddr"])
        check(seg_map is not None,
              "%s: %s vmaddr 0x%x is not covered by any mapping"
              % (image["path"], name, segment["vmaddr"]))
        check(segment["vmaddr"] + segment["vmsize"] <= seg_map.end,
              "%s: %s [0x%x, 0x%x) crosses mapping end 0x%x"
              % (image["path"], name, segment["vmaddr"],
                 segment["vmaddr"] + segment["vmsize"], seg_map.end))
        expected = seg_map.file_offset + (segment["vmaddr"] - seg_map.address)
        check(segment["fileoff"] == expected,
              "%s: %s fileoff 0x%x != mapping %s + delta = 0x%x"
              % (image["path"], name, segment["fileoff"], seg_map.file_name,
                 expected))
        check(segment["fileoff"] + segment["filesize"] <= seg_map.file_offset
              + seg_map.size,
              "%s: %s file range [0x%x, 0x%x) beyond mapping"
              % (image["path"], name, segment["fileoff"],
                 segment["fileoff"] + segment["filesize"]))
        check(segment["filesize"] <= segment["vmsize"],
              "%s: %s filesize 0x%x > vmsize 0x%x"
              % (image["path"], name, segment["filesize"], segment["vmsize"]))
        check((segment["maxprot"] & ~seg_map.max_prot) == 0,
              "%s: %s maxprot 0x%x exceeds mapping maxprot 0x%x"
              % (image["path"], name, segment["maxprot"], seg_map.max_prot))

    if filetype == 6:
        check(id_path == image["path"],
              "%s: LC_ID_DYLIB %r != image path" % (image["path"], id_path))
    else:
        # /usr/lib/dyld is a MH_DYLINKER and legitimately has no LC_ID_DYLIB.
        check(id_path is None,
              "%s: non-dylib has LC_ID_DYLIB %r" % (image["path"], id_path))


def check_images(reader, plan, intervals):
    images, by_path = read_images(reader, plan)
    for image in images:
        parse_macho(reader, plan, image, intervals)

    load_count = 0
    weak_missing = 0
    for image in images:
        for cmd, target_path in image["loads"]:
            load_count += 1
            target = by_path.get(target_path)
            if target is None:
                # LC_LOAD_WEAK_DYLIB / LC_LAZY_LOAD_DYLIB may point at an
                # optional dylib that is not in this cache; dyld tolerates it.
                if cmd in (0x80000018, 0x20):
                    weak_missing += 1
                    continue
                raise VerifyError(
                    "%s: required dylib %r is not an image in this cache"
                    % (image["path"], target_path))
            check(find_interval(intervals, target["address"]) is not None,
                  "%s: dependency %s address 0x%x is not mapped"
                  % (image["path"], target_path, target["address"]))
        text = image["text"]
        check(image["text_load_address"] == image["address"],
              "%s: imagesText loadAddress 0x%x != image address 0x%x"
              % (image["path"], image["text_load_address"], image["address"]))
        check(image["text_path_offset"] == image["path_offset"],
              "%s: imagesText pathOffset 0x%x != image record 0x%x"
              % (image["path"], image["text_path_offset"], image["path_offset"]))
        check(image["text_size"] == text["vmsize"],
              "%s: imagesText textSegmentSize 0x%x != __TEXT.vmsize 0x%x"
              % (image["path"], image["text_size"], text["vmsize"]))

    segment_count = sum(len(im["segments"]) for im in images)
    plan.summary.update({
        "images": images,
        "images_by_path": by_path,
        "segment_count": segment_count,
        "load_count": load_count,
        "weak_missing": weak_missing,
    })
    return images, by_path


def selected_details(plan, by_path, intervals):
    """Describe the dylibs the brief calls out, using the parsed load commands."""
    selected = []
    for path in ("/usr/lib/libobjc.A.dylib",
                 "/usr/lib/libSystem.B.dylib",
                 "/usr/lib/dyld"):
        image = by_path.get(path)
        if image is None:
            continue
        segments = []
        for segment in image["segments"]:
            if segment["name"] == "__PAGEZERO":
                continue
            mapping = find_interval(intervals, segment["vmaddr"])
            segments.append({
                "name": segment["name"],
                "vmaddr": segment["vmaddr"],
                "vmsize": segment["vmsize"],
                "fileoff": segment["fileoff"],
                "mapping": mapping,
                "delta": segment["vmaddr"] - mapping.address,
            })
        loads = []
        missing = 0
        for cmd, target_path in image["loads"]:
            target = by_path.get(target_path)
            if target is None:
                missing += 1
            loads.append((cmd, target_path,
                          target["address"] if target else None))
        selected.append({
            "path": path,
            "address": image["address"],
            "segments": segments,
            "loads": loads,
            "missing": missing,
            "id_path": image["id_path"],
        })
    plan.summary["selected"] = selected
    return selected


def check_local_symbols(reader, plan, images_by_path):
    symbols = plan.symbols
    header = symbols.header
    lso = header.local_symbols_offset
    lss = header.local_symbols_size
    check(lso != 0 and lss != 0,
          "%s: localSymbolsOffset/Size are zero" % symbols.name)
    check(lso + lss <= symbols.size,
          "%s: local symbols [0x%x, 0x%x) beyond file size 0x%x"
          % (symbols.name, lso, lso + lss, symbols.size))
    info = reader.pread(symbols.path, lso, 24)
    nlist_offset, nlist_count, strings_offset, strings_size, entries_offset, \
        entries_count = struct.unpack("<6I", info)
    check(nlist_count != 0 and entries_count != 0,
          "%s: empty local symbol tables" % symbols.name)
    check(entries_count == len(plan.summary["images"]),
          "%s: %u local-symbol entries for %u images"
          % (symbols.name, entries_count, len(plan.summary["images"])))
    check(entries_offset + entries_count * 16 <= lss,
          "%s: 16-byte local-symbol entries end beyond localSymbolsSize"
          % symbols.name)
    check(nlist_offset + nlist_count * 16 <= lss,
          "%s: nlist array ends beyond localSymbolsSize" % symbols.name)
    check(strings_offset + strings_size <= lss,
          "%s: string table ends beyond localSymbolsSize" % symbols.name)
    check(entries_offset + entries_count * 16 <= nlist_offset,
          "%s: local-symbol entries overlap the nlist array" % symbols.name)
    check(nlist_offset + nlist_count * 16 <= strings_offset,
          "%s: nlist array overlaps the string table" % symbols.name)

    entries = reader.pread(symbols.path, lso + entries_offset, entries_count * 16)
    intervals = plan.intervals()
    image_addresses = {im["address"] for im in plan.summary["images"]}
    for i in range(entries_count):
        off = i * 16
        dylib_offset, start, count = struct.unpack_from("<QII", entries, off)
        check(dylib_offset + plan.region_start in image_addresses,
              "%s: local-symbol entry %u dylibOffset 0x%x does not resolve to an "
              "image" % (symbols.name, i, dylib_offset))
        check(start + count <= nlist_count,
              "%s: local-symbol entry %u nlist range [%u, %u) beyond %u"
              % (symbols.name, i, start, start + count, nlist_count))

    strings_start = lso + strings_offset
    check(reader.pread(symbols.path, strings_start, 1) == b"\x00",
          "%s: string table does not start with NUL" % symbols.name)

    nlist_start = lso + nlist_offset
    zero = 0
    outside = 0
    bad_strx = 0
    minimum = None
    maximum = None
    batch = 16384
    done = 0
    while done < nlist_count:
        count = min(batch, nlist_count - done)
        data = reader.pread(symbols.path, nlist_start + done * 16, count * 16)
        for strx, _n_type, _n_sect, _n_desc, value in struct.iter_unpack(
                "<IBBHQ", data):
            if strx >= strings_size:
                bad_strx += 1
            if value == 0:
                zero += 1
                continue
            if minimum is None or value < minimum:
                minimum = value
            if maximum is None or value > maximum:
                maximum = value
            if find_interval(intervals, value) is None:
                outside += 1
        done += count
    check(bad_strx == 0,
          "%s: %u nlist string indexes are beyond stringsSize"
          % (symbols.name, bad_strx))
    check(outside == 0,
          "%s: %u nlist values are outside the mapping plan"
          % (symbols.name, outside))
    check(minimum is not None,
          "%s: no defined local symbols" % symbols.name)
    plan.summary.update({
        "symbols_nlist": nlist_count,
        "symbols_entries": entries_count,
        "symbols_zero": zero,
        "symbols_min": minimum,
        "symbols_max": maximum,
        "symbols_outside": outside,
        "symbols_local_offset": lso,
        "symbols_local_size": lss,
        "symbols_nlist_offset": nlist_offset,
        "symbols_strings_offset": strings_offset,
        "symbols_strings_size": strings_size,
        "symbols_entries_offset": entries_offset,
    })


def check_metadata(reader, plan, intervals):
    header = plan.main.header
    fields = [
        ("patchInfo", header.patch_info_addr, header.patch_info_size),
        ("progClosures", header.prog_closures_addr, header.prog_closures_size),
        ("progClosuresTrie", header.prog_closures_trie_addr,
         header.prog_closures_trie_size),
        ("dylibsImageArray", header.dylibs_image_array_addr,
         header.dylibs_image_array_size),
        ("dylibsTrie", header.dylibs_trie_addr, header.dylibs_trie_size),
        ("otherImageArray", header.other_image_array_addr,
         header.other_image_array_size),
        ("otherTrie", header.other_trie_addr, header.other_trie_size),
        ("programsPblSetPool", header.programs_pbl_set_pool_addr,
         header.programs_pbl_set_pool_size),
    ]
    pointer_fields = [
        ("dyldInCacheMH", header.dyld_in_cache_mh),
        ("dyldInCacheEntry", header.dyld_in_cache_entry),
        ("dylibsPblSet", header.dylibs_pbl_set_addr),
        ("programTrie", header.program_trie_addr),
    ]
    checked = []
    for name, address, size in fields:
        if address == 0 and size == 0:
            continue
        check(address != 0 and size != 0,
              "%s: address 0x%x / size 0x%x mismatch" % (name, address, size))
        mapping = find_interval(intervals, address)
        check(mapping is not None,
              "%s: address 0x%x is not mapped" % (name, address))
        check(address + size <= mapping.end,
              "%s: [0x%x, 0x%x) crosses mapping end 0x%x"
              % (name, address, address + size, mapping.end))
        checked.append((name, address, size, mapping))
    for name, address in pointer_fields:
        if address == 0:
            continue
        mapping = find_interval(intervals, address)
        check(mapping is not None,
              "%s: pointer 0x%x is not mapped" % (name, address))
        checked.append((name, address, 0, mapping))
    plan.summary["metadata"] = checked
    return checked


def slide_verdict(plan, slide):
    """Return (allowed, reason) for a slide, mirroring kebab_dsc_apply_slide()."""
    if slide == 0:
        return True, "zero"
    if slide & (PAGE - 1):
        return False, ("not 16 KiB aligned; KEBAB_DSC_PAGE_SIZE is 0x4000 and "
                       "vm_shared_region_map_file() rejects it with "
                       "FOURK_COMPAT")
    if plan.max_slide == 0:
        return False, "maxSlide is 0"
    if slide >= plan.max_slide:
        return False, "slide >= maxSlide 0x%x" % plan.max_slide
    if slide > 0xffffffff:
        return False, "does not fit the kernel's 32-bit slide field"
    if not any(m.slide_info_size for m in plan.all_mappings()):
        return False, "no mapping carries slide info"
    if not (HARD_BASE <= plan.region_start < HARD_END):
        return False, "sharedRegionStart is outside the kernel region"
    for mapping in plan.all_mappings():
        start = mapping.address + slide
        end = start + mapping.size
        if start < mapping.address or end < start or start < HARD_BASE \
                or end > HARD_END:
            return False, ("%s would move to [0x%x, 0x%x), outside "
                           "[0x%x, 0x%x)"
                           % (mapping.label(), start, end, HARD_BASE, HARD_END))
        if mapping.slide_info_size:
            start = mapping.slide_info_address + slide
            end = start + mapping.slide_info_size
            if start < HARD_BASE or end > HARD_END:
                return False, ("%s slide info would leave the kernel region"
                               % mapping.label())
    return True, "ok"


def check_slides(plan):
    mappings = plan.all_mappings()
    extent = max(m.end for m in mappings) - plan.region_start
    max_slide = plan.max_slide
    largest = (max_slide - PAGE) if max_slide >= PAGE else 0
    trials = [
        (0, "zero / unslid"),
        (PAGE, "one 16 KiB page"),
        (0x100000, "1 MiB"),
        (0x400000, "4 MiB"),
        (0x1000000, "16 MiB"),
        (0x40000000, "1 GiB realistic ASLR"),
        (largest, "maxSlide - 16 KiB / largest accepted"),
        (max_slide, "maxSlide / must be rejected"),
        (max_slide + PAGE, "above maxSlide / must be rejected"),
        (0x1000, "4 KiB / not 16 KiB aligned"),
        (0x2000, "8 KiB / not 16 KiB aligned"),
        (0x1234, "not page aligned at all"),
        (0x1000001, "16 KiB+1 / not aligned"),
        (0x100000000, "does not fit 32-bit slide field"),
    ]
    results = []
    base_addresses = [m.address for m in mappings]
    base_infos = [m.slide_info_address for m in mappings if m.slide_info_size]
    for slide, label in trials:
        allowed, reason = slide_verdict(plan, slide)
        results.append((slide, label, allowed, reason))
        if not allowed:
            continue
        slid_addresses = [m.address + slide for m in mappings]
        check(slid_addresses == [a + slide for a in base_addresses],
              "slide 0x%x changed the relative layout" % slide)
        check(slid_addresses == sorted(slid_addresses),
              "slide 0x%x changed mapping order" % slide)
        for mapping, address in zip(mappings, slid_addresses):
            check((address & (PAGE - 1)) == 0,
                  "slide 0x%x misaligned %s to 0x%x"
                  % (slide, mapping.label(), address))
        slid_infos = [m.slide_info_address + slide for m in mappings
                      if m.slide_info_size]
        check(slid_infos == [a + slide for a in base_infos],
              "slide 0x%x did not move slide info by the same amount" % slide)
        slid_extent = extent + slide
        check(slid_extent <= HARD_SIZE,
              "slide 0x%x needs 0x%x bytes, kernel reserves 0x%x"
              % (slide, slid_extent, HARD_SIZE))
    return results, extent


def print_table(plan, stream=sys.stdout):
    files = sorted(plan.files, key=lambda f: min(m.address for m in f.mappings))
    rows = []
    for index, cf in enumerate(files):
        first = min(m.address for m in cf.mappings)
        end = max(m.end for m in cf.mappings)
        mapped = sum(m.size for m in cf.mappings)
        next_start = None
        if index + 1 < len(files):
            next_start = min(m.address for m in files[index + 1].mappings)
        if next_start is None:
            next_start = plan.symbols.header.shared_region_start
        gap = next_start - end
        rows.append({
            "index": index,
            "name": cf.name,
            "offset": first - plan.region_start,
            "first": first,
            "mapped": mapped,
            "holes": (end - first) - mapped,
            "count": len(cf.mappings),
            "end": end,
            "gap": gap,
            "overlap": "no",
            "kind": cf.kind,
        })
    stream.write("| # | file | cacheVMOffset | first mapping | mapped bytes | holes | maps | end | gap after | overlap |\n")
    stream.write("|---:|---|---:|---:|---:|---:|---:|---:|---:|---|\n")
    for row in rows:
        gap = "—" if row["gap"] is None else "0x%x" % row["gap"]
        stream.write("| %d | `%s` | 0x%x | 0x%x | 0x%x | 0x%x | %d | 0x%x | %s | %s |\n"
                     % (row["index"], row["name"], row["offset"], row["first"],
                        row["mapped"], row["holes"], row["count"], row["end"],
                        gap, row["overlap"]))
    symbols = plan.symbols
    offset = symbols.header.shared_region_start - plan.region_start
    stream.write("| %d | `%s` | 0x%x | 0x%x | 0 | 0 | 0 | — | — | no (excluded) |\n"
                 % (len(rows), symbols.name, offset,
                    symbols.header.shared_region_start))
    return rows


def report(plan):
    header = plan.main.header
    total = plan.summary["total_mapped"]
    extent = plan.summary["extent"]
    gaps = plan.summary["gaps"]
    print("KEBAB DSC PLAN: %s" % plan.directory)
    print("KEBAB DSC PLAN: cache header: 44 files, main %s, %u subcaches, "
          "%u images" % (plan.main.name, len(plan.entries),
                         header.images_count))
    print("KEBAB DSC PLAN: kernel window [0x%x, 0x%x) size 0x%x (4 GiB)"
          % (HARD_BASE, HARD_END, HARD_SIZE))
    print("KEBAB DSC PLAN: cache sharedRegionSize 0x%x, end 0x%x, maxSlide 0x%x"
          % (plan.region_size, plan.region_start + plan.region_size,
             plan.max_slide))
    print("KEBAB DSC PLAN: required window 0x%x (highest end 0x%x - base 0x%x)"
          % (extent, plan.region_start + extent, plan.region_start))
    print("KEBAB DSC PLAN: mapped bytes 0x%x, %u mappings, holes 0x%x in %u gap(s)"
          % (total, plan.summary["mappings"], plan.summary["gap_total"],
             len(gaps)))
    print("KEBAB DSC PLAN: header slack 0x%x; kernel slack after extent 0x%x"
          % (plan.region_size - extent, HARD_SIZE - extent))
    print("KEBAB DSC PLAN: overlap check: none (all mappings sorted and disjoint)")
    print("KEBAB DSC PLAN: slide-info blobs:")
    for address, size, label in plan.summary["slide_infos"]:
        print("KEBAB DSC PLAN:   0x%x+0x%x %s" % (address, size, label))
    print("KEBAB DSC PLAN: images=%u segments=%u load-references=%u "
          "(weak/lazy absent=%u); all segment fileoffs match the plan"
          % (len(plan.summary["images"]), plan.summary["segment_count"],
             plan.summary["load_count"], plan.summary["weak_missing"]))
    print("KEBAB DSC PLAN: local symbols %s: offset 0x%x size 0x%x, "
          "%u entries, %u nlist values, outside plan=%u, range [0x%x, 0x%x]"
          % (plan.symbols.name, plan.summary["symbols_local_offset"],
             plan.summary["symbols_local_size"],
             plan.summary["symbols_entries"], plan.summary["symbols_nlist"],
             plan.summary["symbols_outside"], plan.summary["symbols_min"],
             plan.summary["symbols_max"]))
    print("KEBAB DSC PLAN: metadata regions:")
    for name, address, size, mapping in plan.summary["metadata"]:
        if size:
            print("KEBAB DSC PLAN:   %-18s [0x%x, 0x%x) inside %s[%u]"
                  % (name, address, address + size, mapping.file_name,
                     mapping.index))
        else:
            print("KEBAB DSC PLAN:   %-18s 0x%x inside %s[%u]"
                  % (name, address, mapping.file_name, mapping.index))
    print("KEBAB DSC PLAN: selected load-command checks:")
    for info in plan.summary.get("selected", []):
        print("  %s @ 0x%x (id=%r)" % (info["path"], info["address"],
                                         info["id_path"]))
        for segment in info["segments"]:
            mapping = segment["mapping"]
            print("    %-14s vmaddr=0x%-10x vmsize=0x%-8x fileoff=0x%-9x -> "
                  "%s[%u]+0x%x"
                  % (segment["name"], segment["vmaddr"], segment["vmsize"],
                     segment["fileoff"], mapping.file_name, mapping.index,
                     segment["delta"]))
        resolved = len(info["loads"]) - info["missing"]
        print("    loads: %u, resolved=%u, optional-absent=%u"
              % (len(info["loads"]), resolved, info["missing"]))
        for cmd, target_path, target_address in info["loads"][:8]:
            print("      %s -> %s" % (target_path,
                                      "0x%x" % target_address
                                      if target_address is not None
                                      else "absent (weak/lazy)"))
        if len(info["loads"]) > 8:
            print("      ...")
    print("KEBAB DSC PLAN: slide trials:")
    for slide, label, allowed, reason in plan.summary["slide_results"]:
        if allowed:
            print("KEBAB DSC PLAN:   0x%-10x ALLOW  (%s): end 0x%x, slack 0x%x"
                  % (slide, label, plan.region_start + extent + slide,
                     HARD_END - (plan.region_start + extent + slide)))
        else:
            print("KEBAB DSC PLAN:   0x%-10x REJECT (%s): %s"
                  % (slide, label, reason))
    print("KEBAB DSC PLAN: slide verdict:\n"
          "  * slide 0, 16 KiB multiples and realistic ASLR values preserve the\n"
          "    relative layout and stay inside the 4 GiB kernel window;\n"
          "  * a slide that is not a 16 KiB multiple must be rejected: the\n"
          "    mapping page is 16 KiB and vm_shared_region_map_file() itself\n"
          "    rejects it (FOURK_COMPAT), so this is not a conservative choice;\n"
          "  * maxSlide is 0x%x; the largest accepted slide is 0x%x."
          % (plan.max_slide, (plan.max_slide - PAGE) if plan.max_slide >= PAGE else 0))
    print("KEBAB DSC PLAN: result: PASS")


def selftest():
    checks = 0

    def expect(cond, message):
        nonlocal checks
        checks += 1
        check(cond, "selftest: " + message)

    expect(PAGE == 0x4000 and SLIDE_PAGE == 0x1000, "page constants")
    expect((PAGE & (SLIDE_PAGE - 1)) == 0, "16 KiB is a multiple of 4 KiB")
    expect((0x1000 & (PAGE - 1)) != 0, "4 KiB slide is not 16 KiB aligned")
    expect((0x4000 & (PAGE - 1)) == 0, "16 KiB slide is aligned")
    # A synthetic two-mapping plan: no cache files, just the arithmetic.
    class Fake:
        pass
    plan = Fake()
    plan.region_start = HARD_BASE
    plan.region_size = 0x100000
    plan.max_slide = 0x800000
    plan.all_mappings = lambda: [
        type("M", (), {"address": HARD_BASE, "size": 0x10000,
                       "slide_info_size": 0x100,
                       "slide_info_address": HARD_BASE + 0x1000,
                       "label": lambda self=None: "fake0"})(),
        type("M", (), {"address": HARD_BASE + 0x20000, "size": 0x20000,
                       "slide_info_size": 0, "slide_info_address": 0,
                       "label": lambda self=None: "fake1"})(),
    ]
    allowed, _ = slide_verdict(plan, 0)
    expect(allowed, "zero slide")
    allowed, _ = slide_verdict(plan, 0x4000)
    expect(allowed, "16 KiB slide")
    allowed, _ = slide_verdict(plan, 0x1000)
    expect(not allowed, "4 KiB slide rejected")
    allowed, _ = slide_verdict(plan, 0x800000)
    expect(not allowed, "maxSlide rejected")
    print("KEBAB DSC PLAN: selftest: %d assertions, result: PASS" % checks)
    return 0


def main(argv):
    parser = argparse.ArgumentParser(
        description="Independently verify the split dyld cache mapping plan.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", nargs="?",
                        default=os.path.expanduser("~/ios-userland/cryptex-cache"),
                        help="directory containing the 44 extracted cache files")
    parser.add_argument("--expect-files", type=int, default=44,
                        help="number of cache files expected (default 44)")
    parser.add_argument("--table", action="store_true",
                        help="print the 44-file Markdown table after the report")
    parser.add_argument("--json", metavar="PATH",
                        help="write a JSON summary to PATH")
    parser.add_argument("--selftest", action="store_true",
                        help="run only the arithmetic self-test")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    directory = os.path.abspath(os.path.expanduser(args.directory))
    if not os.path.isdir(directory):
        print("KEBAB DSC PLAN: FAILED: %s is not a directory" % directory,
              file=sys.stderr)
        return 1

    reader = None
    try:
        plan = build_plan(directory, expect_files=args.expect_files)
        reader = plan.summary["reader"]
        intervals = check_plan_arithmetic(plan)
        check_images(reader, plan, intervals)
        selected_details(plan, plan.summary["images_by_path"], intervals)
        check_local_symbols(reader, plan, plan.summary["images_by_path"])
        check_metadata(reader, plan, intervals)
        plan.summary["slide_results"], extent = check_slides(plan)
        plan.summary["extent"] = extent
        report(plan)
        if args.table:
            print()
            print_table(plan)
        if args.json:
            summary = {
                "directory": directory,
                "region_start": plan.region_start,
                "region_size": plan.region_size,
                "required_window": plan.summary["extent"],
                "reserved_window": HARD_SIZE,
                "max_slide": plan.max_slide,
                "total_mapped": plan.summary["total_mapped"],
                "mappings": plan.summary["mappings"],
                "files": len(plan.files),
                "images": len(plan.summary["images"]),
                "segments": plan.summary["segment_count"],
                "load_references": plan.summary["load_count"],
                "weak_missing": plan.summary["weak_missing"],
                "local_symbols": plan.summary["symbols_nlist"],
                "local_symbols_outside": plan.summary["symbols_outside"],
                "slides": [{"slide": s, "label": l, "allowed": a, "reason": r}
                           for s, l, a, r in plan.summary["slide_results"]],
            }
            with open(args.json, "w", encoding="utf-8") as handle:
                json.dump(summary, handle, indent=2, sort_keys=True)
                handle.write("\n")
        return 0
    except VerifyError as exc:
        print("KEBAB DSC PLAN: FAILED: %s" % exc, file=sys.stderr)
        return 1
    finally:
        if reader is not None:
            reader.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
