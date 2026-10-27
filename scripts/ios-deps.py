#!/usr/bin/env python3
"""Resolve the library dependencies of an iOS Mach-O against what we own.

For every `LC_LOAD_DYLIB` / `LC_LOAD_WEAK_DYLIB` / `LC_REEXPORT_DYLIB` /
`LC_LOAD_UPWARD_DYLIB` in the binary, decide where the path would come from:

  cache     the path is one of the images listed in the dyld shared cache
            (`imagesText` array of the main cache file),
  volume    the path exists as a file on the root volume (mtree inventory),
  NOWHERE   neither -- the loader would fail with "image not found".

It also dumps the load commands that decide *how* the binary can run at all:
`LC_DYLD_CHAINED_FIXUPS` / `LC_DYLD_INFO_ONLY`, `LC_MAIN`, `LC_CODE_SIGNATURE`,
`LC_UUID`, `LC_BUILD_VERSION` and `LC_RPATH`.

Examples
--------
    python3 scripts/ios-deps.py --cache cryptex-cache/dyld_shared_cache_arm64 \
        --mtree mtree-paths.txt extracted/sbin/launchd extracted/usr/lib/dyld

    python3 scripts/ios-deps.py ... --json

Nothing here parses the 2.7 GB of subcaches: the main cache file carries the
whole image list (2,710 paths for 16.7.16), which is all a name lookup needs.
"""
import argparse
import json
import os
import struct
import sys

# ---- Mach-O constants ----------------------------------------------------
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF

LC_REQ_DYLD = 0x80000000
LC_SEGMENT_64 = 0x19
LC_UUID = 0x1B
LC_CODE_SIGNATURE = 0x1D
LC_LOAD_DYLIB = 0x0C
LC_ID_DYLIB = 0x0D
LC_LOAD_WEAK_DYLIB = 0x18 | LC_REQ_DYLD
LC_REEXPORT_DYLIB = 0x1F | LC_REQ_DYLD
LC_LAZY_LOAD_DYLIB = 0x20
LC_LOAD_UPWARD_DYLIB = 0x23 | LC_REQ_DYLD
LC_RPATH = 0x1C | LC_REQ_DYLD
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x22 | LC_REQ_DYLD
LC_MAIN = 0x28 | LC_REQ_DYLD
LC_ENCRYPTION_INFO_64 = 0x2C
LC_BUILD_VERSION = 0x32
LC_VERSION_MIN_IPHONEOS = 0x25
LC_DYLD_CHAINED_FIXUPS = 0x34 | LC_REQ_DYLD

DYLIB_KINDS = {
    LC_LOAD_DYLIB: "load",
    LC_LOAD_WEAK_DYLIB: "weak",
    LC_REEXPORT_DYLIB: "reexport",
    LC_LAZY_LOAD_DYLIB: "lazy",
    LC_LOAD_UPWARD_DYLIB: "upward",
}

CPU_NAMES = {
    0x0100000C: "arm64",
    0x0200000C: "arm64e",
    7: "i386",
    0x01000007: "x86_64",
    12: "arm",
}

PLATFORM_NAMES = {
    1: "macOS", 2: "iOS", 3: "tvOS", 4: "watchOS", 5: "bridgeOS",
    6: "macCatalyst", 7: "iOS-simulator", 8: "tvOS-simulator",
    9: "watchOS-simulator", 10: "driverKit",
}

FILETYPE_NAMES = {
    1: "object", 2: "executable", 3: "fvmlib", 4: "core", 5: "preload",
    6: "dylib", 7: "dylinker", 8: "bundle", 9: "dylib_stub", 10: "dSYM",
    11: "kext_bundle", 12: "fileset",
}


def lc_str(data, base, off, limit):
    """NUL-terminated string at load-command offset `off`."""
    start = base + off
    end = data.find(b"\0", start, base + limit)
    if end < 0:
        end = base + limit
    return data[start:end].decode("utf-8", "replace")


def ver(v):
    return "%d.%d.%d" % ((v >> 16) & 0xFFFF, (v >> 8) & 0xFF, v & 0xFF)


def parse_macho(data, offset=0):
    """Parse one thin slice that starts at `offset`. Returns a dict."""
    magic = struct.unpack_from("<I", data, offset)[0]
    if magic != MH_MAGIC_64:
        raise ValueError("not a 64-bit little-endian Mach-O at %d (0x%08x)"
                         % (offset, magic))
    cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _res = \
        struct.unpack_from("<iiIIIII", data, offset + 4)

    info = {
        "cputype": cputype,
        "cpu": CPU_NAMES.get(cputype & 0xFFFFFFFF, hex(cputype)),
        "cpusubtype": cpusubtype & 0x00FFFFFF,
        "filetype": filetype,
        "filetype_name": FILETYPE_NAMES.get(filetype, str(filetype)),
        "ncmds": ncmds,
        "sizeofcmds": sizeofcmds,
        "flags": flags,
        "uuid": None,
        "build_version": None,
        "min_version": None,
        "main": None,
        "dyld_info": None,
        "chained_fixups": None,
        "code_signature": None,
        "encryption": None,
        "rpaths": [],
        "id": None,
        "deps": [],
    }

    p = offset + 32
    end = p + sizeofcmds
    for _ in range(ncmds):
        if p + 8 > end:
            break
        cmd, cmdsize = struct.unpack_from("<II", data, p)
        if cmdsize < 8:
            break
        if cmd == LC_UUID:
            u = data[p + 8:p + 24]
            info["uuid"] = "%s-%s-%s-%s-%s" % (
                u[0:4].hex(), u[4:6].hex(), u[6:8].hex(),
                u[8:10].hex(), u[10:16].hex())
        elif cmd == LC_BUILD_VERSION:
            plat, minos, sdk = struct.unpack_from("<III", data, p + 8)
            info["build_version"] = {
                "platform": plat,
                "platform_name": PLATFORM_NAMES.get(plat, str(plat)),
                "minos": ver(minos), "sdk": ver(sdk),
            }
        elif cmd == LC_VERSION_MIN_IPHONEOS:
            v, sdk = struct.unpack_from("<II", data, p + 8)
            info["min_version"] = {"platform": 2, "platform_name": "iOS",
                                   "minos": ver(v), "sdk": ver(sdk)}
        elif cmd == LC_MAIN:
            entryoff, stacksize = struct.unpack_from("<QQ", data, p + 8)
            info["main"] = {"entryoff": entryoff, "stacksize": stacksize}
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            vals = struct.unpack_from("<10I", data, p + 8)
            info["dyld_info"] = {
                "cmd": "LC_DYLD_INFO_ONLY" if cmd & LC_REQ_DYLD
                       else "LC_DYLD_INFO",
                "rebase": [vals[0], vals[1]], "bind": [vals[2], vals[3]],
                "weak_bind": [vals[4], vals[5]],
                "lazy_bind": [vals[6], vals[7]],
                "export": [vals[8], vals[9]],
            }
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            off, size = struct.unpack_from("<II", data, p + 8)
            info["chained_fixups"] = {"dataoff": off, "datasize": size}
        elif cmd == LC_CODE_SIGNATURE:
            off, size = struct.unpack_from("<II", data, p + 8)
            info["code_signature"] = {"dataoff": off, "datasize": size}
        elif cmd == LC_ENCRYPTION_INFO_64:
            cryptoff, cryptsize, cryptid = struct.unpack_from("<III",
                                                              data, p + 8)
            info["encryption"] = {"cryptoff": cryptoff,
                                  "cryptsize": cryptsize, "cryptid": cryptid}
        elif cmd == LC_RPATH:
            off = struct.unpack_from("<I", data, p + 8)[0]
            info["rpaths"].append(lc_str(data, p, off, cmdsize))
        elif cmd == LC_ID_DYLIB:
            off = struct.unpack_from("<I", data, p + 8)[0]
            info["id"] = lc_str(data, p, off, cmdsize)
        elif cmd in DYLIB_KINDS:
            off = struct.unpack_from("<I", data, p + 8)[0]
            ts, cur, compat = struct.unpack_from("<III", data, p + 12)
            info["deps"].append({
                "path": lc_str(data, p, off, cmdsize),
                "kind": DYLIB_KINDS[cmd],
                "current_version": ver(cur),
                "compatibility_version": ver(compat),
            })
        p += cmdsize
    return info


def slices(data):
    """Yield (label, info) for every architecture slice in the file."""
    if len(data) < 8:
        raise ValueError("file too short")
    magic_be = struct.unpack_from(">I", data, 0)[0]
    if magic_be in (FAT_MAGIC, FAT_MAGIC_64):
        n = struct.unpack_from(">I", data, 4)[0]
        wide = magic_be == FAT_MAGIC_64
        for i in range(n):
            if wide:
                cputype, cpusub, off, size = struct.unpack_from(
                    ">iiQQ", data, 8 + i * 32)
            else:
                cputype, cpusub, off, size = struct.unpack_from(
                    ">iiII", data, 8 + i * 20)
            yield CPU_NAMES.get(cputype & 0xFFFFFFFF, hex(cputype)), \
                parse_macho(data, off)
        return
    yield "thin", parse_macho(data, 0)


# ---- the two resolution sources -----------------------------------------
def load_cache_images(path):
    """Return {path: {uuid, load_address, text_size}} from the main cache."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"dyld_v1"):
        raise SystemExit("%s: not a dyld shared cache (magic %r)"
                         % (path, data[:16]))
    mapping_off, mapping_cnt = struct.unpack_from("<II", data, 16)
    uuid = data[88:104]
    images_text_off, images_text_cnt = struct.unpack_from("<QQ", data, 136)
    platform = struct.unpack_from("<I", data, 216)[0]
    fmt = data[220]
    shared_start, shared_size = struct.unpack_from("<QQ", data, 224)

    images = {}
    per = 32  # dyld_cache_image_text_info
    for i in range(images_text_cnt):
        e = images_text_off + i * per
        if e + per > len(data):
            break
        img_uuid = data[e:e + 16]
        load_addr, text_size, path_off = struct.unpack_from("<QII", data,
                                                           e + 16)
        end = data.find(b"\0", path_off)
        if end < 0:
            continue
        p = data[path_off:end].decode("utf-8", "replace")
        images[p] = {
            "uuid": str(img_uuid[:4].hex()) + "-" + img_uuid[4:6].hex() + "-"
                    + img_uuid[6:8].hex() + "-" + img_uuid[8:10].hex() + "-"
                    + img_uuid[10:].hex(),
            "load_address": load_addr,
            "text_size": text_size,
        }
    header = {
        "magic": data[:16].decode("latin-1"),
        "mapping_offset": mapping_off,
        "mapping_count": mapping_cnt,
        "images_text_offset": images_text_off,
        "images_text_count": images_text_cnt,
        "code_signature_offset": struct.unpack_from("<Q", data, 40)[0],
        "code_signature_size": struct.unpack_from("<Q", data, 48)[0],
        "subcache_array_offset": struct.unpack_from("<I", data, 392)[0],
        "subcache_array_count": struct.unpack_from("<I", data, 396)[0],
        "platform": platform,
        "format_version": fmt,
        "shared_region_start": shared_start,
        "shared_region_size": shared_size,
        "max_slide": struct.unpack_from("<Q", data, 240)[0],
        "file_size": len(data),
    }
    return images, header


def load_mtree(path):
    """Return {path: type} from an inventory produced by decode-ios-mtree.py."""
    out = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            out[parts[-1]] = parts[0]
    return out


def resolve(path, rpaths, exe_dir, images, volume):
    """Return (source, detail) for one dependency path."""
    candidates = []
    if path.startswith("@rpath/"):
        for r in rpaths:
            if r.startswith("@executable_path"):
                r = exe_dir + r[len("@executable_path"):]
            elif r.startswith("@loader_path"):
                r = exe_dir + r[len("@loader_path"):]
            candidates.append(r.rstrip("/") + "/" + path[len("@rpath/"):])
    elif path.startswith("@executable_path/"):
        candidates.append(exe_dir + path[len("@executable_path"):])
    elif path.startswith("@loader_path/"):
        candidates.append(exe_dir + path[len("@loader_path"):])
    else:
        candidates.append(path)

    for c in candidates:
        if c in images:
            return "cache", c
        if c in volume:
            return "volume", c
    return "NOWHERE", candidates[0] if candidates else path


def note(path):
    """Print a load-command field only when it is present."""
    return "present" if path else "absent"


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="resolve iOS Mach-O dependencies against the shared "
                    "cache and the root-volume inventory")
    ap.add_argument("machos", nargs="*", help="Mach-O files to analyse")
    ap.add_argument("--cache",
                    default="cryptex-cache/dyld_shared_cache_arm64",
                    help="main dyld shared cache file (default: %(default)s)")
    ap.add_argument("--mtree", default="mtree-paths.txt",
                    help="path inventory from decode-ios-mtree.py "
                         "(default: %(default)s)")
    ap.add_argument("--cache-info", action="store_true",
                    help="print the cache header fields and exit")
    ap.add_argument("--list-images", metavar="SUBSTRING",
                    help="list cache image paths containing SUBSTRING")
    ap.add_argument("--json", action="store_true", help="machine-readable dump")
    args = ap.parse_args(argv)

    if not args.machos and not (args.cache_info or args.list_images):
        ap.error("give at least one Mach-O, or --cache-info / --list-images")

    images, header = None, None
    if os.path.exists(args.cache):
        images, header = load_cache_images(args.cache)
    else:
        print("warning: no cache at %s -- every dependency will read NOWHERE"
              % args.cache, file=sys.stderr)
    if args.cache_info or args.list_images:
        if header is None:
            raise SystemExit("no cache at %s" % args.cache)
        if args.cache_info:
            print("cache file       %s" % args.cache)
            print("  magic            %r" % header["magic"])
            print("  file size        %d bytes" % header["file_size"])
            print("  platform         %d (%s)"
                  % (header["platform"],
                     PLATFORM_NAMES.get(header["platform"], "?")))
            print("  format version   %d" % header["format_version"])
            print("  mappings         %d at offset %d"
                  % (header["mapping_count"], header["mapping_offset"]))
            print("  imagesText       %d images at offset %d"
                  % (header["images_text_count"],
                     header["images_text_offset"]))
            print("  subcache array   %d entries at offset %d"
                  % (header["subcache_array_count"],
                     header["subcache_array_offset"]))
            print("  code signature   %d bytes at offset %d"
                  % (header["code_signature_size"],
                     header["code_signature_offset"]))
            print("  shared region    0x%x size 0x%x (%.1f MiB), max slide 0x%x"
                  % (header["shared_region_start"],
                     header["shared_region_size"],
                     header["shared_region_size"] / 1048576.0,
                     header["max_slide"]))
            print("  image paths       %d resolvable from imagesText"
                  % len(images))
        if args.list_images:
            pat = args.list_images.lower()
            hits = sorted(p for p in images if pat in p.lower())
            print("  %d of %d images match %r"
                  % (len(hits), len(images), args.list_images))
            for p in hits:
                print("    0x%011x  %s" % (images[p]["load_address"], p))
        if not args.machos:
            return 0

    volume = {}
    if os.path.exists(args.mtree):
        volume = load_mtree(args.mtree)
    else:
        print("warning: no inventory at %s -- on-disk resolution disabled"
              % args.mtree, file=sys.stderr)

    report = {"cache": header, "mtree_paths": len(volume), "binaries": []}

    for mpath in args.machos:
        with open(mpath, "rb") as f:
            data = f.read()
        exe_dir = os.path.dirname(os.path.abspath(mpath))
        entry = {"file": mpath, "size": len(data), "slices": []}
        for label, info in slices(data):
            deps = []
            for d in info["deps"]:
                src, via = resolve(d["path"], info["rpaths"], exe_dir,
                                   images or {}, volume)
                d = dict(d)
                d["source"] = src
                d["resolved_path"] = via
                if images and via in images:
                    d["cache_load_address"] = images[via]["load_address"]
                    d["cache_text_size"] = images[via]["text_size"]
                deps.append(d)
            info = dict(info)
            info["deps"] = deps
            info["cpu_label"] = label
            entry["slices"].append(info)

            if not args.json:
                print("=" * 78)
                print("%s  (%d bytes)" % (mpath, len(data)))
                print("  slice %-6s %s  filetype=%s  ncmds=%d"
                      % (label, info["cpu"], info["filetype_name"],
                         info["ncmds"]))
                print("  LC_UUID                 %s" % (info["uuid"] or "-"))
                bv = info["build_version"] or info["min_version"]
                if bv:
                    print("  build version           platform=%d (%s) "
                          "minos=%s sdk=%s"
                          % (bv["platform"], bv["platform_name"],
                             bv["minos"], bv["sdk"]))
                else:
                    print("  build version           ABSENT")
                cf = info["chained_fixups"]
                print("  LC_DYLD_CHAINED_FIXUPS  %s%s"
                      % (note(cf),
                         "  dataoff=%d datasize=%d" % (cf["dataoff"],
                                                      cf["datasize"])
                         if cf else ""))
                di = info["dyld_info"]
                print("  LC_DYLD_INFO            %s%s"
                      % (note(di), "  " + di["cmd"] if di else ""))
                cs = info["code_signature"]
                print("  LC_CODE_SIGNATURE       %s%s"
                      % (note(cs),
                         "  dataoff=%d datasize=%d" % (cs["dataoff"],
                                                      cs["datasize"])
                         if cs else ""))
                enc = info["encryption"]
                print("  LC_ENCRYPTION_INFO_64   %s%s"
                      % (note(enc),
                         "  cryptid=%d" % enc["cryptid"] if enc else ""))
                mn = info["main"]
                print("  LC_MAIN                 %s%s"
                      % (note(mn),
                         "  entryoff=0x%x" % mn["entryoff"] if mn else ""))
                print("  LC_RPATH                %s"
                      % (", ".join(info["rpaths"]) or "none"))
                print("  LC_ID_DYLIB             %s" % (info["id"] or "-"))
                print()
                if not deps:
                    print("  no LC_LOAD_DYLIB of any kind")
                else:
                    print("  %-46s %-9s %s"
                          % ("dependency", "kind", "resolves from"))
                    print("  " + "-" * 74)
                    for d in deps:
                        if d["source"] == "cache":
                            detail = ("shared cache @0x%x"
                                      % d["cache_load_address"])
                        elif d["source"] == "volume":
                            detail = "root volume file"
                        else:
                            detail = "NOTHING -- load would fail"
                        print("  %-46s %-9s %s"
                              % (d["path"], d["kind"], detail))
                counts = {"cache": 0, "volume": 0, "NOWHERE": 0}
                for d in deps:
                    counts[d["source"]] += 1
                print()
                print("  closure: %d dependencies -- %d from cache, "
                      "%d from volume, %d NOWHERE"
                      % (len(deps), counts["cache"], counts["volume"],
                         counts["NOWHERE"]))
                if counts["NOWHERE"]:
                    print("  MISSING:")
                    for d in deps:
                        if d["source"] == "NOWHERE":
                            print("    %s (%s)" % (d["path"], d["kind"]))
        report["binaries"].append(entry)

    if args.json:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
