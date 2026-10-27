#!/usr/bin/env python3
"""Build the two KIST streams that make the smallest bootable userland.

`base.kist` carries the five files launchd needs before it can run a job:
launchd, dyld, the static trust cache, one job executable and its plist.
`cache.kist` carries the 44 split dyld shared cache files (43 mapped files and
the .symbols file the kernel reports but does not map).

The stream format itself is scripts/pack-userland.py's KIST v1; this script
only decides what goes in, checks the claims against the binaries, and then
runs the packer and its offline verifier.

Checks performed before a byte is packed:

  * every executable's direct LC_LOAD_DYLIB resolves inside the main cache
    file's imagesText array (scripts/ios-deps.py);
  * every executable's cdhash is in the extracted static trust cache
    (scripts/extract-ios-trustcache.py);
  * every cache file matches the size and SHA-256 in MANIFEST.txt.

Defaults point at the material used during bring-up:

    --userland ~/ios-userland/extracted
    --cache    ~/ios-userland/cryptex-cache
    --trustcache ~/ios-userland/static.trustcache
    --out      ~/ios-userland/kist

Examples:

    scripts/make-minimal-userland.py --out /tmp/kist
    scripts/make-minimal-userland.py --only base
    scripts/make-minimal-userland.py --only cache --no-symbols
"""

import argparse
import hashlib
import importlib.util
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.join(HERE, "pack-userland.py")
DEPS = os.path.join(HERE, "ios-deps.py")
TRUST = os.path.join(HERE, "extract-ios-trustcache.py")

ALLOW = [
    "/sbin/",
    "/usr/lib/",
    "/usr/standalone/",
    "/System/Library/Caches/com.apple.dyld/",
    "/System/Library/Caches/com.apple.kernelcaches/",
    "/System/Library/LaunchDaemons/",
]

STREAM_HDR = struct.Struct("<4sHHIIQQ")
ENTRY_HDR = struct.Struct("<4sHHHHQQ32s128sI")

PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
\t<key>Label</key>
\t<string>com.kebab.zprint</string>
\t<key>ProgramArguments</key>
\t<array>
\t\t<string>/usr/lib/zprint</string>
\t\t<string>-L</string>
\t</array>
\t<key>RunAtLoad</key>
\t<true/>
\t<key>KeepAlive</key>
\t<false/>
\t<key>StandardOutPath</key>
\t<string>/dev/console</string>
\t<key>StandardErrorPath</key>
\t<string>/dev/console</string>
</dict>
</plist>
"""


def die(msg):
    print("make-minimal-userland: %s" % msg, file=sys.stderr)
    return 1


def human(n):
    if n >= 1 << 30:
        return "%.2f GiB" % (n / float(1 << 30))
    if n >= 1 << 20:
        return "%.2f MiB" % (n / float(1 << 20))
    if n >= 1 << 10:
        return "%.1f KiB" % (n / float(1 << 10))
    return "%d B" % n


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load %s" % path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_manifest(cache_dir, with_symbols=True):
    path = os.path.join(cache_dir, "MANIFEST.txt")
    if not os.path.isfile(path):
        raise RuntimeError("%s is missing" % path)
    out = []
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                raise RuntimeError("%s:%d: want name<TAB>bytes<TAB>sha256"
                                   % (path, lineno))
            name, size, digest = parts
            if name.endswith(".symbols") and not with_symbols:
                continue
            out.append((name, int(size), digest.lower()))
    return out


def check_cache_sources(cache_dir, manifest):
    total = 0
    for name, size, digest in manifest:
        src = os.path.join(cache_dir, name)
        if not os.path.isfile(src):
            raise RuntimeError("%s is missing" % src)
        got = os.path.getsize(src)
        if got != size:
            raise RuntimeError("%s: size %d, MANIFEST says %d"
                               % (name, got, size))
        actual = sha256_file(src)
        if actual != digest:
            raise RuntimeError("%s: sha256 %s, MANIFEST says %s"
                               % (name, actual, digest))
        total += size
    print("cache sources: %d files, %s, MANIFEST sizes and hashes match"
          % (len(manifest), human(total)))


def macho_slices(buf):
    """Yield (offset, size) for each arm64 slice; thin files yield one."""
    if len(buf) < 8:
        raise RuntimeError("file too short")
    magic = struct.unpack_from(">I", buf, 0)[0]
    if magic in (0xCAFEBABE, 0xCAFEBABF):
        count = struct.unpack_from(">I", buf, 4)[0]
        for i in range(count):
            _ct, _cs, off, size, _al = struct.unpack_from(
                ">iiIII", buf, 8 + i * 20)
            yield off, size
        return
    yield 0, len(buf)


def check_trustcache(tcmod, trust_path, paths):
    raw = open(trust_path, "rb").read()
    tc = tcmod.TrustCache(tcmod.im4p_payload(raw))
    missing = []
    print("trust cache: v%d, %d entries, uuid %s"
          % (tc.version, tc.count, tc.uuid.hex()))
    for path in paths:
        with open(path, "rb") as f:
            buf = f.read()
        found = None
        for off, _size in macho_slices(buf):
            cdhash, hash_type = tcmod.macho_cdhash(buf, off, len(buf))
            entry = tc.lookup(cdhash, hash_type)
            if entry is not None:
                found = (cdhash, hash_type, entry)
                break
        if found is None:
            missing.append(path)
            print("  MISSING %s" % path)
        else:
            cdhash, hash_type, _entry = found
            print("  OK      %-46s cdhash %s (hash %d)"
                  % (path, cdhash.hex(), hash_type))
    if missing:
        raise RuntimeError("%d executable(s) are not in the static trust cache: "
                           "%s" % (len(missing), ", ".join(missing)))


def check_dependencies(deps, cache_main, paths):
    images, header = deps.load_cache_images(cache_main)
    print("cache images: %d paths, region 0x%x + 0x%x"
          % (len(images), header["shared_region_start"],
             header["shared_region_size"]))
    bad = []
    for path in paths:
        with open(path, "rb") as f:
            buf = f.read()
        print("%s:" % path)
        for label, info in deps.slices(buf):
            if not info["deps"]:
                print("  no LC_LOAD_DYLIB of any kind")
            for dep in info["deps"]:
                source, detail = deps.resolve(
                    dep["path"], info["rpaths"],
                    os.path.dirname(path), images, set())
                print("  %-62s %-8s %s" % (dep["path"], source, detail))
                if source != "cache":
                    bad.append((path, dep["path"], source, detail))
    if bad:
        raise RuntimeError("dependencies that are not in the split cache: "
                           + "; ".join("%s -> %s (%s)"
                                       % (p, d, s) for p, d, s, _ in bad))
    print("dependency closure: every path resolves inside the split cache")


def stream_stats(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        hdr = f.read(STREAM_HDR.size)
        magic, version, flags, count, hsize, usize, wire = \
            STREAM_HDR.unpack(hdr)
        if magic != b"KIST" or version != 1:
            raise RuntimeError("%s: not KIST v1" % path)
        entries = []
        payload = 0
        for i in range(count):
            h = f.read(ENTRY_HDR.size)
            if len(h) != ENTRY_HDR.size:
                raise RuntimeError("%s: entry %d truncated" % (path, i))
            (emagic, plen, mode, eflags, _r, w, fsize, digest,
             rawpath, _r2) = ENTRY_HDR.unpack(h)
            if emagic != b"KENT":
                raise RuntimeError("%s: entry %d bad magic" % (path, i))
            dest = rawpath[:plen].decode("utf-8")
            entries.append({"dest": dest, "mode": mode, "wire": w,
                            "size": fsize, "flags": eflags})
            payload += w
            f.seek(w, 1)
    padding = wire - payload - count * ENTRY_HDR.size
    ratio = (STREAM_HDR.size + wire) / float(usize) if usize else 0.0
    return {
        "path": path, "count": count, "usize": usize,
        "file": size, "wire": wire, "padding": padding, "ratio": ratio,
        "largest_size": max(entries, key=lambda e: e["size"]),
        "largest_wire": max(entries, key=lambda e: e["wire"]),
        "entries": entries,
    }


def write_spec(path, rows):
    with open(path, "w") as f:
        for src, dest, mode, _why in rows:
            f.write("%s\t%s\t%04o\n" % (src, dest, mode))


def run_packer(spec, out, compress):
    cmd = [sys.executable, PACK, "--spec", spec, "--compress", compress,
           "-o", out, "--chunk", "65536"]
    for prefix in ALLOW:
        cmd += ["--allow", prefix]
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)
    print("+ %s %s --verify %s" % (sys.executable, PACK, out))
    subprocess.run([sys.executable, PACK, "--verify", out], check=True)


def print_inventory(rows, title):
    print("\n%s" % title)
    print("%-64s %10s %5s  %s" % ("destination", "bytes", "mode", "source"))
    for src, dest, mode, why in rows:
        print("%-64s %10d %04o  %s" % (dest, os.path.getsize(src), mode, src))
        print("    %s" % why)


def print_stream(stats):
    print("\nstream %s" % stats["path"])
    print("  entries    %d" % stats["count"])
    print("  installed  %d bytes (%s)" % (stats["usize"], human(stats["usize"])))
    print("  wire       %d bytes (%s), %.1f%% of installed"
          % (stats["file"], human(stats["file"]), 100.0 * stats["ratio"]))
    print("  padding    %d bytes" % stats["padding"])
    print("  largest    %s, %d bytes (wire %d)"
          % (stats["largest_size"]["dest"], stats["largest_size"]["size"],
             stats["largest_size"]["wire"]))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--userland", default=os.path.expanduser(
        "~/ios-userland/extracted"), help="extracted root files")
    ap.add_argument("--cache", default=os.path.expanduser(
        "~/ios-userland/cryptex-cache"), help="44 split cache files")
    ap.add_argument("--trustcache", default=os.path.expanduser(
        "~/ios-userland/static.trustcache"), help="extracted static trust cache")
    ap.add_argument("--out", default=os.path.expanduser("~/ios-userland/kist"),
                    help="output directory (needs ~2.2 GB free for cache.kist)")
    ap.add_argument("--only", choices=("base", "cache", "both"),
                    default="both")
    ap.add_argument("--no-symbols", action="store_true",
                    help="leave .symbols out of cache.kist (43 mapped files)")
    ap.add_argument("--skip-checks", action="store_true",
                    help="pack without the trust-cache/dependency checks")
    ap.add_argument("--compress", choices=("auto", "none", "zlib"),
                    default="auto")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    launchd = os.path.join(args.userland, "sbin", "launchd")
    dyld = os.path.join(args.userland, "usr", "lib", "dyld")
    zprint = os.path.join(args.userland, "usr", "bin", "zprint")
    for path in (launchd, dyld, zprint, args.trustcache):
        if not os.path.isfile(path):
            return die("%s is missing" % path)

    plist = os.path.join(args.out, "com.kebab.zprint.plist")
    with open(plist, "w") as f:
        f.write(PLIST)

    base = [
        (launchd, "/sbin/launchd", 0o755,
         "PID 1; the kernel's load_init_program() execs exactly this path"),
        (dyld, "/usr/lib/dyld", 0o755,
         "exec interpreter; no LC_LOAD_DYLIB, so it is self-contained"),
        (args.trustcache,
         "/System/Library/Caches/com.apple.kernelcaches/static.trustcache",
         0o644,
         "kebab_cs_trustcache_load() default path; marks launchd platform"),
        (zprint, "/usr/lib/zprint", 0o755,
         "job payload; all five dylibs are in the split cache"),
        (plist, "/System/Library/LaunchDaemons/com.kebab.zprint.plist", 0o644,
         "RunAtLoad job: zprint -L writes the zone table to /dev/console"),
    ]
    manifest = []
    cache = []
    if args.only in ("cache", "both"):
        manifest = read_manifest(args.cache, with_symbols=not args.no_symbols)
        cache = [(os.path.join(args.cache, name),
                  "/System/Library/Caches/com.apple.dyld/" + name, 0o644,
                  "split cache %s" % ("(.symbols is reported but not mapped)"
                                      if name.endswith(".symbols")
                                      else "(mapped by kebab_dsc)"))
                 for name, _size, _digest in manifest]

    print_inventory(base, "base.kist inventory")
    if args.only in ("cache", "both"):
        print("\ncache.kist inventory: %d files under "
              "/System/Library/Caches/com.apple.dyld/" % len(cache))
        for src, dest, mode, why in cache:
            print("  %-64s %10d %04o" % (dest, os.path.getsize(src), mode))
    else:
        print("\ncache.kist inventory: %d files (not built by --only base)"
              % len(cache))

    if not args.skip_checks:
        deps = load_module(DEPS, "ios_deps")
        trust = load_module(TRUST, "ios_trustcache")
        if args.only in ("cache", "both"):
            check_cache_sources(args.cache, manifest)
        check_trustcache(trust, args.trustcache, [launchd, dyld, zprint])
        check_dependencies(deps, os.path.join(
            args.cache, "dyld_shared_cache_arm64"), [launchd, dyld, zprint])

    if args.only in ("base", "both"):
        spec = os.path.join(args.out, "base.spec")
        write_spec(spec, base)
        run_packer(spec, os.path.join(args.out, "base.kist"), args.compress)
    if args.only in ("cache", "both"):
        spec = os.path.join(args.out, "cache.spec")
        write_spec(spec, cache)
        run_packer(spec, os.path.join(args.out, "cache.kist"), args.compress)

    print("\nsummary")
    for name in ("base.kist", "cache.kist"):
        path = os.path.join(args.out, name)
        if os.path.isfile(path):
            print_stream(stream_stats(path))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as exc:
        sys.exit(die(str(exc)))
