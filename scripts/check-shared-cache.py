#!/usr/bin/env python3
"""Validate a split dyld shared cache and print the mapping plan.

This is the host-side twin of bsd/kern/kebab_dyldcache.c.  It reads only the
headers of the 44 files (a few KiB each), applies exactly the checks the kernel
applies, resolves every subcache by UUID against the files actually present in
the directory, and prints the plan the kernel would hand to the VM:

  * the main cache at sharedRegionStart;
  * every subcache at sharedRegionStart + cacheVMOffset, using the address,
    size, file offset, initProt and maxProt from that subcache's own mapping
    table;
  * the .symbols file reported but not mapped.

Exit status is 0 only if the header validates, all subcaches resolve, and every
mapping is inside the region.

Usage:
  scripts/check-shared-cache.py [DIRECTORY]
  scripts/check-shared-cache.py --json [DIRECTORY]
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"dyld_v1   arm64\x00"
MAIN_NAME = "dyld_shared_cache_arm64"
PREFIX = MAIN_NAME

PAGE = 0x4000
MAX_MAPPINGS = 8
SUB_ENTRY_SIZE = 56
MAPPING_SIZE = 32
IMAGE_INFO_SIZE = 32

# Field offsets in struct dyld_cache_header (dyld_cache_format.h).  The names
# follow that header; the *_old fields are the pre-split locations and are
# reported as unused.  imagesOffset/imagesCount moved to the end of the header
# precisely so old readers would not crash; 448/452 is where they live now.
OFF_MAGIC = 0
OFF_MAPPING_OFFSET = 16
OFF_MAPPING_COUNT = 20
OFF_IMAGES_OFFSET_OLD = 24
OFF_IMAGES_COUNT_OLD = 28
OFF_DYLD_BASE = 32
OFF_CODE_SIGNATURE_OFFSET = 40
OFF_CODE_SIGNATURE_SIZE = 48
OFF_UUID = 88
OFF_CACHE_TYPE = 104
OFF_IMAGES_TEXT_OFFSET = 136
OFF_IMAGES_TEXT_COUNT = 144
OFF_PLATFORM = 216
OFF_FORMAT_VERSION = 220
OFF_SHARED_REGION_START = 224
OFF_SHARED_REGION_SIZE = 232
OFF_MAX_SLIDE = 240
OFF_MAPPING_WITH_SLIDE_OFFSET = 312
OFF_MAPPING_WITH_SLIDE_COUNT = 316
OFF_SUB_CACHE_ARRAY_OFFSET = 392
OFF_SUB_CACHE_ARRAY_COUNT = 396
OFF_SYMBOL_FILE_UUID = 400
OFF_IMAGES_OFFSET = 448
OFF_IMAGES_COUNT = 452
OFF_CACHE_SUB_TYPE = 456
HEADER_SIZE = 456

# VM protection bits (mach/vm_prot.h).
VM_PROT_READ = 0x1
VM_PROT_WRITE = 0x2
VM_PROT_EXECUTE = 0x4
VM_PROT_SLIDE = 0x10
VM_PROT_ZF = 0x20


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def prot_string(prot):
    return "".join(
        c if prot & bit else "-"
        for c, bit in (("r", VM_PROT_READ), ("w", VM_PROT_WRITE), ("x", VM_PROT_EXECUTE))
    )


def uuid_string(value):
    return value.hex()


class CheckError(Exception):
    pass


class Header:
    def __init__(self, data, label):
        if len(data) < HEADER_SIZE:
            raise CheckError("%s: file is shorter than the 416-byte cache header" % label)
        self.label = label
        self.magic = data[OFF_MAGIC:OFF_MAGIC + 16]
        self.mapping_offset = u32(data, OFF_MAPPING_OFFSET)
        self.mapping_count = u32(data, OFF_MAPPING_COUNT)
        self.images_offset_old = u32(data, OFF_IMAGES_OFFSET_OLD)
        self.images_count_old = u32(data, OFF_IMAGES_COUNT_OLD)
        self.code_signature_offset = u64(data, OFF_CODE_SIGNATURE_OFFSET)
        self.code_signature_size = u64(data, OFF_CODE_SIGNATURE_SIZE)
        self.uuid = data[OFF_UUID:OFF_UUID + 16]
        self.cache_type = u64(data, OFF_CACHE_TYPE)
        self.images_text_offset = u64(data, OFF_IMAGES_TEXT_OFFSET)
        self.images_text_count = u64(data, OFF_IMAGES_TEXT_COUNT)
        self.platform = u32(data, OFF_PLATFORM)
        self.format_version = u32(data, OFF_FORMAT_VERSION) & 0xFF
        self.raw_format_version = u32(data, OFF_FORMAT_VERSION)
        self.shared_region_start = u64(data, OFF_SHARED_REGION_START)
        self.shared_region_size = u64(data, OFF_SHARED_REGION_SIZE)
        self.max_slide = u64(data, OFF_MAX_SLIDE)
        self.mapping_with_slide_offset = u32(data, OFF_MAPPING_WITH_SLIDE_OFFSET)
        self.mapping_with_slide_count = u32(data, OFF_MAPPING_WITH_SLIDE_COUNT)
        self.sub_cache_array_offset = u32(data, OFF_SUB_CACHE_ARRAY_OFFSET)
        self.sub_cache_array_count = u32(data, OFF_SUB_CACHE_ARRAY_COUNT)
        self.symbol_file_uuid = data[OFF_SYMBOL_FILE_UUID:OFF_SYMBOL_FILE_UUID + 16]
        self.images_offset = u32(data, OFF_IMAGES_OFFSET)
        self.images_count = u32(data, OFF_IMAGES_COUNT)
        self.cache_sub_type = u32(data, OFF_CACHE_SUB_TYPE)


class Mapping:
    def __init__(self, data, off):
        self.address = u64(data, off)
        self.size = u64(data, off + 8)
        self.file_offset = u64(data, off + 16)
        self.max_prot = u32(data, off + 24)
        self.init_prot = u32(data, off + 28)


class PlanFile:
    def __init__(self, name, path, size, header, vm_offset, suffix, kind):
        self.name = name
        self.path = path
        self.size = size
        self.header = header
        self.vm_offset = vm_offset
        self.suffix = suffix
        self.kind = kind
        self.mappings = []


def read_header(path, label):
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            data = f.read(4096)
    except OSError as exc:
        raise CheckError("%s: cannot read: %s" % (label, exc))
    header = Header(data, label)
    return header, size, data


def validate_header(header, file_size, region_start, region_size, is_main, vm_offset=0):
    """Apply the kernel's checks; return a list of (description, detail)."""
    checks = []

    if header.magic != MAGIC:
        raise CheckError("%s: bad magic %r (want %r)" % (header.label, header.magic, MAGIC))
    checks.append(("magic", repr(header.magic)))

    if header.format_version != 0:
        raise CheckError("%s: formatVersion %u, want 0" % (header.label, header.format_version))
    checks.append(("formatVersion", str(header.format_version)))

    if header.cache_type != 2:
        raise CheckError("%s: cacheType %u, want 2 (multi-cache)" % (header.label, header.cache_type))
    checks.append(("cacheType", str(header.cache_type)))

    if header.platform != 2:
        raise CheckError("%s: platform %u, want 2 (iOS)" % (header.label, header.platform))
    checks.append(("platform", str(header.platform)))

    images_end = header.images_offset + header.images_count * IMAGE_INFO_SIZE
    if images_end != header.images_text_offset:
        raise CheckError(
            "%s: imagesOffset + imagesCount*32 = %u + %u*32 = %u, want imagesTextOffset %u"
            % (header.label, header.images_offset, header.images_count, images_end,
               header.images_text_offset))
    checks.append(("images", "%u + %u*32 = %u == imagesTextOffset"
                   % (header.images_offset, header.images_count, images_end)))

    images_text_end = header.images_text_offset + header.images_text_count * IMAGE_INFO_SIZE
    if images_text_end > file_size:
        raise CheckError("%s: imagesText array ends at %u beyond file size %u"
                         % (header.label, images_text_end, file_size))
    checks.append(("imagesText", "%u + %u*32 = %u <= file size %u"
                   % (header.images_text_offset, header.images_text_count,
                      images_text_end, file_size)))

    sub_end = header.sub_cache_array_offset + header.sub_cache_array_count * SUB_ENTRY_SIZE
    if sub_end > header.code_signature_offset:
        raise CheckError(
            "%s: subCacheArray ends at %u (%u + %u*56), beyond codeSignatureOffset %u"
            % (header.label, sub_end, header.sub_cache_array_offset,
               header.sub_cache_array_count, header.code_signature_offset))
    checks.append(("subCacheArray", "%u + %u*56 = %u <= codeSignatureOffset %u"
                   % (header.sub_cache_array_offset, header.sub_cache_array_count,
                      sub_end, header.code_signature_offset)))

    if header.code_signature_offset + header.code_signature_size > file_size:
        raise CheckError("%s: code signature [%u, %u) beyond file size %u"
                         % (header.label, header.code_signature_offset,
                            header.code_signature_offset + header.code_signature_size,
                            file_size))

    if header.mapping_count == 0 or header.mapping_count > MAX_MAPPINGS:
        raise CheckError("%s: mappingCount %u not in 1..%u"
                         % (header.label, header.mapping_count, MAX_MAPPINGS))
    mappings_end = header.mapping_offset + header.mapping_count * MAPPING_SIZE
    if mappings_end > file_size:
        raise CheckError("%s: mapping table ends at %u beyond file size %u"
                         % (header.label, mappings_end, file_size))
    checks.append(("mappings", "%u at %u, ends %u <= file size %u"
                   % (header.mapping_count, header.mapping_offset, mappings_end, file_size)))

    if header.mapping_with_slide_count:
        slide_end = (header.mapping_with_slide_offset
                     + header.mapping_with_slide_count * 56)
        if slide_end > file_size:
            raise CheckError("%s: mapping-and-slide table ends at %u beyond file size %u"
                             % (header.label, slide_end, file_size))
        if header.mapping_with_slide_count != header.mapping_count:
            raise CheckError("%s: mappingWithSlideCount %u != mappingCount %u"
                             % (header.label, header.mapping_with_slide_count,
                                header.mapping_count))
        checks.append(("mappingWithSlide", "%u entries, consistent"
                       % header.mapping_with_slide_count))

    if is_main:
        if header.shared_region_start != region_start:
            raise CheckError("%s: sharedRegionStart %#x != main %#x"
                             % (header.label, header.shared_region_start, region_start))
        if header.shared_region_size != region_size:
            raise CheckError("%s: sharedRegionSize %#x != main %#x"
                             % (header.label, header.shared_region_size, region_size))
    else:
        if header.shared_region_start != region_start + vm_offset:
            raise CheckError("%s: sharedRegionStart %#x != region base %#x + vmOffset %#x"
                             % (header.label, header.shared_region_start,
                                region_start, vm_offset))
        if header.shared_region_start < region_start:
            raise CheckError("%s: sharedRegionStart %#x below region base %#x"
                             % (header.label, header.shared_region_start, region_start))
        if header.shared_region_size and (
                header.shared_region_start + header.shared_region_size
                > region_start + region_size):
            raise CheckError("%s: [%#x, %#x) escapes region [%#x, %#x)"
                             % (header.label, header.shared_region_start,
                                header.shared_region_start + header.shared_region_size,
                                region_start, region_start + region_size))

    if header.images_offset_old or header.images_count_old:
        checks.append(("imagesOld", "nonzero (%u/%u), ignored"
                       % (header.images_offset_old, header.images_count_old)))
    return checks


def build_plan(directory):
    main_path = os.path.join(directory, MAIN_NAME)
    main, main_size, _ = read_header(main_path, MAIN_NAME)

    # The main header has to be read before any subcache entry can be resolved,
    # so validate it first and use its region fields as the authority.
    validate_header(main, main_size, main.shared_region_start,
                    main.shared_region_size, is_main=True)
    region_start = main.shared_region_start
    region_size = main.shared_region_size

    if main.sub_cache_array_count == 0:
        raise CheckError("%s: subCacheArrayCount is 0; not a split cache" % MAIN_NAME)
    if main.sub_cache_array_count > 64:
        raise CheckError("%s: subCacheArrayCount %u is implausibly large"
                         % (MAIN_NAME, main.sub_cache_array_count))

    entries_end = (main.sub_cache_array_offset
                   + main.sub_cache_array_count * SUB_ENTRY_SIZE)
    if entries_end > main_size:
        raise CheckError("%s: subCacheArray needs bytes up to %u, beyond file size %u"
                         % (MAIN_NAME, entries_end, main_size))
    with open(main_path, "rb") as f:
        main_data = f.read(entries_end)
    if len(main_data) != entries_end:
        raise CheckError("%s: short read for subCacheArray (%u/%u)"
                         % (MAIN_NAME, len(main_data), entries_end))

    entries = []
    for i in range(main.sub_cache_array_count):
        off = main.sub_cache_array_offset + i * SUB_ENTRY_SIZE
        entry_uuid = main_data[off:off + 16]
        vm_offset = u64(main_data, off + 16)
        suffix = main_data[off + 24:off + 56].split(b"\0", 1)[0].decode("ascii", "replace")
        entries.append((entry_uuid, vm_offset, suffix))

    # Index every cache-looking regular file by the UUID in its own header.  A
    # duplicate UUID is an error: it would make "the file that matches" vague.
    candidates = {}
    scanned = []
    for name in sorted(os.listdir(directory)):
        if not name.startswith(PREFIX):
            continue
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        try:
            header, size, _ = read_header(path, name)
        except CheckError as exc:
            print("KEBAB DSC: warning: %s" % exc)
            continue
        scanned.append((name, size))
        key = header.uuid
        if key in candidates:
            raise CheckError("%s and %s have the same uuid %s"
                             % (candidates[key][0], name, uuid_string(key)))
        candidates[key] = (name, path, size, header)

    plan = []
    main_plan = PlanFile(MAIN_NAME, main_path, main_size, main, 0, "", "main")
    for i in range(main.mapping_count):
        main_plan.mappings.append(Mapping(main_data, main.mapping_offset + i * MAPPING_SIZE))
    plan.append(main_plan)

    resolved = 0
    missing = []
    kinds = {"dylddata": 0, "dyldlinkedit": 0}
    symbols = None
    for i, (entry_uuid, vm_offset, suffix) in enumerate(entries):
        match = candidates.get(entry_uuid)
        if match is None:
            missing.append((i + 1, suffix, uuid_string(entry_uuid)))
            print("KEBAB DSC: subcache %02d %-18r uuid %s: NO FILE WITH THIS UUID"
                  % (i + 1, suffix, uuid_string(entry_uuid)))
            continue
        name, path, size, header = match
        if header.magic != MAGIC:
            raise CheckError("%s: matched by uuid but magic is %r" % (name, header.magic))
        validate_header(header, size, region_start, region_size, is_main=False,
                        vm_offset=vm_offset)
        if not name.endswith(suffix):
            print("KEBAB DSC: note: %s matched subcache %d, suffix field says %r"
                  % (name, i + 1, suffix))
        pf = PlanFile(name, path, size, header, vm_offset, suffix, "sub")
        data = open(path, "rb").read(4096)
        for j in range(header.mapping_count):
            pf.mappings.append(Mapping(data, header.mapping_offset + j * MAPPING_SIZE))
        if not pf.mappings:
            raise CheckError("%s: no mappings" % name)
        first = pf.mappings[0]
        if first.address != region_start + vm_offset:
            raise CheckError(
                "%s: first mapping %#x != sharedRegionStart %#x + cacheVMOffset %#x = %#x"
                % (name, first.address, region_start, vm_offset, region_start + vm_offset))
        for m in pf.mappings:
            if m.address < region_start or m.address + m.size > region_start + region_size:
                raise CheckError("%s: mapping [%#x, %#x) escapes region [%#x, %#x)"
                                 % (name, m.address, m.address + m.size,
                                    region_start, region_start + region_size))
            if not (m.init_prot & VM_PROT_ZF):
                if m.file_offset + m.size > size:
                    raise CheckError("%s: mapping [%#x, %#x) covers file [%#x, %#x) "
                                     "beyond size %#x"
                                     % (name, m.address, m.address + m.size,
                                        m.file_offset, m.file_offset + m.size, size))
        plan.append(pf)
        resolved += 1
        if suffix.endswith(".dylddata"):
            kinds["dylddata"] += 1
        elif suffix.endswith(".dyldlinkedit"):
            kinds["dyldlinkedit"] += 1
        if header.symbol_file_uuid == entry_uuid:
            raise CheckError("%s: subcache UUID equals symbolFileUUID" % name)

    if resolved != len(entries):
        raise CheckError("resolved %u/%u subcaches; missing: %s"
                         % (resolved, len(entries),
                            ", ".join("%d%s" % (i, s) for i, s, _ in missing)))

    for kind, want in (("dylddata", 2), ("dyldlinkedit", 2)):
        if kinds[kind] != want:
            raise CheckError("found %u %s subcaches, want %u"
                             % (kinds[kind], kind, want))

    symbol_match = candidates.get(main.symbol_file_uuid)
    if symbol_match is not None:
        symbols = symbol_match
    else:
        print("KEBAB DSC: warning: symbolFileUUID %s has no file in %s"
              % (uuid_string(main.symbol_file_uuid), directory))

    scanned_names = {name for name, _ in scanned}
    main_name = candidates.get(main.uuid, (None,))[0]
    if main_name != MAIN_NAME:
        raise CheckError("main header uuid %s is on file %r, not %r"
                         % (uuid_string(main.uuid), main_name, MAIN_NAME))

    total_mapped = sum(m.size for f in plan for m in f.mappings)
    total_mappings = sum(len(f.mappings) for f in plan)
    highest_end = max(m.address + m.size for f in plan for m in f.mappings)
    return main, region_start, region_size, plan, symbols, scanned_names, total_mapped, total_mappings, highest_end


def print_plan(directory, main, region_start, region_size, plan, symbols,
               scanned_names, total_mapped, total_mappings, highest_end):
    print("KEBAB DSC: host checker: %s" % directory)
    print("KEBAB DSC: main %s: %u bytes, uuid %s"
          % (MAIN_NAME, os.path.getsize(os.path.join(directory, MAIN_NAME)),
             uuid_string(main.uuid)))
    print("KEBAB DSC: validate magic=%r formatVersion=%u cacheType=%u platform=%u"
          % (MAGIC, main.format_version, main.cache_type, main.platform))
    print("KEBAB DSC: validate images %u + %u*32 = %u == imagesTextOffset %u"
          % (main.images_offset, main.images_count,
             main.images_offset + main.images_count * IMAGE_INFO_SIZE,
             main.images_text_offset))
    print("KEBAB DSC: validate subcache array %u + %u*56 = %u <= codeSignatureOffset %u"
          % (main.sub_cache_array_offset, main.sub_cache_array_count,
             main.sub_cache_array_offset + main.sub_cache_array_count * SUB_ENTRY_SIZE,
             main.code_signature_offset))
    print("KEBAB DSC: region start=0x%x size=0x%x end=0x%x maxSlide=0x%x"
          % (region_start, region_size, region_start + region_size, main.max_slide))
    print("KEBAB DSC: resolved %u/%u subcaches by uuid; dylddata=2 dyldlinkedit=2"
          % (len(plan) - 1, main.sub_cache_array_count))
    print("KEBAB DSC: scanned %u cache files in directory" % len(scanned_names))
    if symbols is not None:
        name, path, size, header = symbols
        print("KEBAB DSC: symbols %s: %u bytes, uuid %s, NOT mapped"
              % (name, size, uuid_string(header.uuid)))
    else:
        print("KEBAB DSC: symbols: absent, NOT mapped")
    print("KEBAB DSC: plan %u files / %u mappings / 0x%x bytes (%u)"
          % (len(plan), total_mappings, total_mapped, total_mapped))
    print("KEBAB DSC: highest mapped end 0x%x, region slack 0x%x"
          % (highest_end, region_start + region_size - highest_end))
    for index, f in enumerate(plan):
        tag = "main" if f.kind == "main" else "sub "
        print("KEBAB DSC: file %02u %s %-38s size=0x%-9x mapped=0x%-9x vmOffset=0x%-10x uuid=%s"
              % (index, tag, f.name, f.size,
                 sum(m.size for m in f.mappings), f.vm_offset,
                 uuid_string(f.header.uuid)))
        for m in f.mappings:
            print("KEBAB DSC:      map 0x%09x size=0x%-9x file=0x%-9x init=%s max=%s"
                  % (m.address, m.size, m.file_offset,
                     prot_string(m.init_prot), prot_string(m.max_prot)))
    print("KEBAB DSC: dry run: validated only; the kernel maps nothing unless "
          "kebab_dsc_map=1")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", nargs="?",
                        default=os.path.expanduser("~/ios-userland/cryptex-cache"),
                        help="directory containing the 44 extracted cache files")
    parser.add_argument("--json", action="store_true",
                        help="also emit the plan as JSON on stdout")
    args = parser.parse_args(argv)

    directory = os.path.abspath(os.path.expanduser(args.directory))
    try:
        result = build_plan(directory)
    except CheckError as exc:
        print("KEBAB DSC: FAILED: %s" % exc, file=sys.stderr)
        return 1

    print_plan(directory, *result)

    if args.json:
        main_header, region_start, region_size, plan, symbols, scanned, total, count, end = result
        doc = {
            "directory": directory,
            "region": {"start": region_start, "size": region_size},
            "main_uuid": uuid_string(main_header.uuid),
            "files": [{
                "name": f.name,
                "size": f.size,
                "vmOffset": f.vm_offset,
                "uuid": uuid_string(f.header.uuid),
                "mappings": [{
                    "address": m.address,
                    "size": m.size,
                    "fileOffset": m.file_offset,
                    "initProt": m.init_prot,
                    "maxProt": m.max_prot,
                } for m in f.mappings],
            } for f in plan],
            "symbols": symbols[0] if symbols else None,
            "totalMappedBytes": total,
            "totalMappings": count,
        }
        print(json.dumps(doc, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
