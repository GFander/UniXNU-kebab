#!/usr/bin/env python3
"""Verify Apple's static trust cache against the real launchd, dyld and
xpcproxy.

This is the host-side counterpart of the kernel's code-signing classification:

  * bsd/kern/ubc_subr.c:cs_validate_csblob() walks the embedded signature,
    ranks every CodeDirectory with hash_rank() (SHA-1 < SHA-256-truncated <
    SHA-256 < SHA-384), rejects duplicate ranks, and hashes the selected
    CodeDirectory with SHA-256_* to produce the 20-byte cdhash.
  * bsd/kern/kebab_codesign.c:kebab_tc_lookup() looks that cdhash up in
    Apple's static trust cache and, on a hit, marks the blob platform.

A mismatch between those two computations is indistinguishable from "the
cache was not installed" in the kernel log, so this tool recomputes both from
the actual files and states the answer explicitly.

Trust cache format (20H392, iPhone10,3 / iPhone10,6, iOS 16.7.16):

    DER SEQUENCE {
        IA5String "IM4P"
        IA5String "trst"
        IA5String "1"
        OCTET STRING payload
    }

and payload is a trust cache module:

    u32 version (2 here)
    u8  uuid[16]
    u32 numEntries (2760 here)
    entries[] (24 bytes each for v2)
        u8 cdhash[20]       first 20 bytes of the CodeDirectory hash
        u8 hashType         (2 = SHA-256 here)
        u8 flags
        u8 constraintCategory
        u8 reserved

The old host helper in this workspace treated the file as a bare "trst"
module and used 20-byte entries; that is wrong for the IPSW member, which is
DER-wrapped and holds 24-byte v2 entries.  This tool does not share that code.

Examples:

    # defaults to ~/ios-userland/{extracted/sbin/launchd,
    # extracted/usr/lib/dyld,static.trustcache}
    scripts/verify-trustcache-cdhash.py

    scripts/verify-trustcache-cdhash.py --trust-cache /tmp/static.trustcache
        --launchd /tmp/launchd --dyld /tmp/dyld
"""

import argparse
import hashlib
import os
import struct
import sys

CDHASH_SIZE = 20
TC_HEADER_SIZE = 24
TC_ENTRY_SIZE = {0: 20, 1: 22, 2: 24}

# Mirrors hashPriorities[] in bsd/kern/ubc_subr.c; higher wins.
HASH_RANK = {1: 1, 3: 2, 2: 3, 4: 4}
HASH_NAME = {1: "sha1", 2: "sha256", 3: "sha256-trunc", 4: "sha384"}
HASH_DIGEST_SIZE = {1: 20, 3: 20, 2: 32, 4: 48}

CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CSSLOT_CODEDIRECTORY = 0
CSSLOT_ALTERNATE_CODEDIRECTORIES = 0x1000
CSSLOT_ALTERNATE_CODEDIRECTORY_LIMIT = 0x1005
LC_CODE_SIGNATURE = 0x1D
MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF

EXPECTED = {
    "launchd": {
        "cdhash": "b65c36c9f5b287a6a8f5ca83d9427fb55ef04f4e",
        "cd_offset": 0x7E124,
        "cd_length": 4398,
        "hash_type": 2,
        "page_shift": 12,
        "slots": (127, 127),
    },
    "dyld": {
        "cdhash": "684d847c40e6e873eeaec693011d4d997a869d2a",
        "cd_offset": 0xDB284,
        "cd_length": 7218,
        "hash_type": 2,
        "page_shift": 12,
        "slots": (220, 220),
    },
    "xpcproxy": {
        "cdhash": "f7f090b0a9b94dc944026136fc6154372763c846",
        "cd_offset": 0x12AD4,
        "cd_length": 940,
        "hash_type": 2,
        "page_shift": 12,
        "slots": (19, 19),
    },
}


def die(msg):
    raise SystemExit("error: " + msg)


def be32(buf, off):
    if off + 4 > len(buf):
        die("truncated 32-bit field at 0x%x" % off)
    return struct.unpack_from(">I", buf, off)[0]


def le32(buf, off):
    if off + 4 > len(buf):
        die("truncated 32-bit field at 0x%x" % off)
    return struct.unpack_from("<I", buf, off)[0]


def der_length(buf, off):
    if off >= len(buf):
        die("truncated DER length")
    first = buf[off]
    off += 1
    if first & 0x80 == 0:
        return first, off
    count = first & 0x7F
    if count == 0 or count > 4 or off + count > len(buf):
        die("bad DER length")
    return int.from_bytes(buf[off:off + count], "big"), off + count


def der_tlv(buf, off):
    if off >= len(buf):
        die("truncated DER tag")
    tag = buf[off]
    length, off = der_length(buf, off + 1)
    if off + length > len(buf):
        die("DER value runs past the buffer")
    return tag, buf[off:off + length], off + length


def im4p_payload(buf):
    """Return (payload, wrapper_bytes) for the IM4P ("trst") wrapper."""
    tag, body, _end = der_tlv(buf, 0)
    if tag != 0x30:
        die("not a DER SEQUENCE")
    off = 0
    tag, value, off = der_tlv(body, off)
    if tag != 0x16 or value != b"IM4P":
        die("not an IM4P payload")
    tag, value, off = der_tlv(body, off)
    if tag != 0x16 or value != b"trst":
        die("IM4P payload is %r, not 'trst'" % value)
    tag, value, off = der_tlv(body, off)
    if tag != 0x16:
        die("missing IM4P payload version")
    tag, payload, off = der_tlv(body, off)
    if tag != 0x04:
        die("missing IM4P OCTET STRING")
    return payload, len(buf) - len(payload)


class TrustCache:
    def __init__(self, payload, source):
        if len(payload) < TC_HEADER_SIZE:
            die("trust cache module is too short")
        self.source = source
        self.version = struct.unpack_from("<I", payload, 0)[0]
        if self.version not in TC_ENTRY_SIZE:
            die("unsupported trust cache version %u" % self.version)
        self.uuid = payload[4:20]
        self.count = struct.unpack_from("<I", payload, 20)[0]
        self.entry_size = TC_ENTRY_SIZE[self.version]
        self.needed = TC_HEADER_SIZE + self.count * self.entry_size
        if self.needed > len(payload):
            die("trust cache claims %u entries of %u bytes but payload is %u"
                % (self.count, self.entry_size, len(payload)))
        self.residual = len(payload) - self.needed
        self.entries = []
        self.hash_types = {}
        previous = None
        self.sorted = True
        for i in range(self.count):
            off = TC_HEADER_SIZE + i * self.entry_size
            cdhash = payload[off:off + CDHASH_SIZE]
            hash_type = payload[off + CDHASH_SIZE] if self.version >= 1 else 0
            flags = payload[off + CDHASH_SIZE + 1] if self.version >= 1 else 0
            category = payload[off + 22] if self.version >= 2 else 0
            self.entries.append((cdhash, hash_type, flags, category))
            self.hash_types[hash_type] = self.hash_types.get(hash_type, 0) + 1
            if previous is not None and previous >= cdhash:
                self.sorted = False
            previous = cdhash

    def lookup(self, cdhash):
        """Match like the kernel: 20-byte cdhash only, no hash-type compare."""
        if self.sorted:
            lo, hi = 0, self.count
            while lo < hi:
                mid = (lo + hi) // 2
                entry = self.entries[mid][0]
                if entry == cdhash:
                    return self.entries[mid]
                if entry < cdhash:
                    lo = mid + 1
                else:
                    hi = mid
            return None
        for entry in self.entries:
            if entry[0] == cdhash:
                return entry
        return None

    def summary(self):
        types = ", ".join("%s=%u" % (HASH_NAME.get(k, "type-%u" % k), v)
                          for k, v in sorted(self.hash_types.items()))
        print("trust cache  %s (%u bytes)" % (self.source.path,
                                              self.source.size))
        print("  IM4P       %u-byte DER wrapper, %u-byte payload"
              % (self.source.wrapper, len(self.source.payload)))
        print("  module     v%u, %u entries x %u bytes, %u used, "
              "%u residual, %s"
              % (self.version, self.count, self.entry_size, self.needed,
                 self.residual, "sorted" if self.sorted else "UNSORTED"))
        print("  hash types %s" % types)
        if self.residual:
            tail = self.source.payload[self.needed:self.needed + 8]
            print("  residual   %u bytes, first %s"
                  % (self.residual, " ".join("%02x" % b for b in tail)))


class TrustCacheSource:
    def __init__(self, path, raw):
        self.path = path
        self.size = len(raw)
        self.payload, self.wrapper = im4p_payload(raw)


def macho_slices(data):
    """Yield (file_offset, size, cputype, cpusubtype) for each slice."""
    if len(data) < 8:
        die("file is too short to be a Mach-O")
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic in (FAT_MAGIC, FAT_MAGIC_64):
        count = struct.unpack_from(">I", data, 4)[0]
        if count == 0 or count > 32:
            die("implausible fat slice count %u" % count)
        step = 32 if magic == FAT_MAGIC_64 else 20
        for i in range(count):
            off = 8 + i * step
            if magic == FAT_MAGIC_64:
                cputype, cpusub, base, size, _align, _res = \
                    struct.unpack_from(">iiQQII", data, off)
            else:
                cputype, cpusub, base, size, _align = \
                    struct.unpack_from(">iiIII", data, off)
            yield base, size, cputype, cpusub
        return
    yield 0, len(data), 0, 0


def code_directory(data, slice_off, slice_size):
    """Select the CodeDirectory exactly as cs_validate_csblob() would."""
    if slice_off + 32 > len(data):
        die("slice at 0x%x is truncated" % slice_off)
    magic = le32(data, slice_off)
    if magic != MH_MAGIC_64:
        die("slice 0x%x is not a 64-bit little-endian Mach-O (magic 0x%x)"
            % (slice_off, magic))
    ncmds = le32(data, slice_off + 16)
    off = slice_off + 32
    sig_off = sig_size = None
    for _ in range(ncmds):
        if off + 8 > slice_off + slice_size:
            die("load commands run past the slice")
        cmd = le32(data, off)
        cmdsize = le32(data, off + 4)
        if cmdsize < 8:
            die("bad load command size at 0x%x" % off)
        if cmd == LC_CODE_SIGNATURE:
            sig_off = le32(data, off + 8)
            sig_size = le32(data, off + 12)
        off += cmdsize
    if sig_off is None:
        die("slice 0x%x has no LC_CODE_SIGNATURE" % slice_off)
    if sig_off + sig_size > slice_size:
        die("LC_CODE_SIGNATURE runs past the slice")

    sb = slice_off + sig_off
    if be32(data, sb) != CSMAGIC_EMBEDDED_SIGNATURE:
        die("LC_CODE_SIGNATURE does not point at a SuperBlob")
    sb_len = be32(data, sb + 4)
    count = be32(data, sb + 8)
    if sb_len > sig_size or 12 + count * 8 > sb_len:
        die("bad SuperBlob length/index count")

    best = None
    best_rank = 0
    for i in range(count):
        slot_type = be32(data, sb + 12 + i * 8)
        blob_off = be32(data, sb + 16 + i * 8)
        if slot_type != CSSLOT_CODEDIRECTORY and not (
                CSSLOT_ALTERNATE_CODEDIRECTORIES <= slot_type <
                CSSLOT_ALTERNATE_CODEDIRECTORY_LIMIT):
            continue
        blob = sb + blob_off
        if blob + 40 > sb + sb_len:
            die("CodeDirectory index points past the SuperBlob")
        if be32(data, blob) != CSMAGIC_CODEDIRECTORY:
            continue
        blob_len = be32(data, blob + 4)
        if blob_len < 40 or blob + blob_len > sb + sb_len:
            die("CodeDirectory length runs past the SuperBlob")
        hash_type = data[blob + 37]
        rank = HASH_RANK.get(hash_type, 0)
        if rank == 0:
            continue
        if best is not None and rank == best_rank:
            die("duplicate hash type %u CodeDirectory (kernel rejects this)"
                % hash_type)
        if best is None or rank > best_rank:
            best = {
                "offset": blob,
                "length": blob_len,
                "hash_type": hash_type,
                "bytes": data[blob:blob + blob_len],
            }
            best_rank = rank
    if best is None:
        die("no supported CodeDirectory in the signature")
    return best


def check_slots(data, slice_off, cd):
    """Hash every code page and compare with the CodeDirectory's slots."""
    cd_bytes = cd["bytes"]
    hash_offset = struct.unpack_from(">I", cd_bytes, 16)[0]
    n_code = struct.unpack_from(">I", cd_bytes, 28)[0]
    code_limit = struct.unpack_from(">I", cd_bytes, 32)[0]
    hash_size = cd_bytes[36]
    page_shift = cd_bytes[39]
    if hash_size != HASH_DIGEST_SIZE.get(cd["hash_type"]):
        die("hashSize %u does not match hash type %u"
            % (hash_size, cd["hash_type"]))
    if hash_offset + n_code * hash_size > len(cd_bytes):
        die("code hash slots run past the CodeDirectory")
    page_size = (1 << page_shift) if page_shift else code_limit
    if page_size == 0:
        die("CodeDirectory has no page size and no code limit")
    matched = 0
    for i in range(n_code):
        start = slice_off + i * page_size
        end = min(slice_off + code_limit, start + page_size)
        if start >= slice_off + code_limit:
            want = b"\0" * hash_size
        else:
            want = hashlib.sha256(data[start:end]).digest()[:hash_size]
        have = cd_bytes[hash_offset + i * hash_size:
                        hash_offset + (i + 1) * hash_size]
        if want == have:
            matched += 1
    return matched, n_code, page_shift


def check_binary(path, tc):
    with open(path, "rb") as f:
        data = f.read()
    name = os.path.basename(path)
    expected = EXPECTED.get(name)
    if expected is None:
        die("no expected values for %s" % name)
    rows = []
    for base, size, _cputype, _cpusub in macho_slices(data):
        cd = code_directory(data, base, size)
        cdhash = hashlib.sha256(cd["bytes"]).digest()[:CDHASH_SIZE]
        matched, n_code, page_shift = check_slots(data, base, cd)
        hit = tc.lookup(cdhash)
        rows.append({
            "slice": base,
            "cd_offset": cd["offset"],
            "cd_length": cd["length"],
            "hash_type": cd["hash_type"],
            "cdhash": cdhash.hex(),
            "matched": matched,
            "slots": n_code,
            "page_shift": page_shift,
            "hit": hit is not None,
            "hit_type": hit[1] if hit else None,
        })
    if len(rows) != 1:
        die("%s has %u slices; expected exactly one" % (name, len(rows)))
    row = rows[0]
    ok = (
        row["cdhash"] == expected["cdhash"] and
        row["cd_offset"] == expected["cd_offset"] and
        row["cd_length"] == expected["cd_length"] and
        row["hash_type"] == expected["hash_type"] and
        row["page_shift"] == expected["page_shift"] and
        (row["matched"], row["slots"]) == expected["slots"] and
        row["hit"]
    )
    return name, row, ok


def main():
    home = os.path.expanduser("~")
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--trust-cache",
        default=os.path.join(home, "ios-userland", "static.trustcache"),
        help="IM4P-wrapped static trust cache")
    parser.add_argument(
        "--launchd",
        default=os.path.join(home, "ios-userland", "extracted", "sbin",
                             "launchd"),
        help="real /sbin/launchd")
    parser.add_argument(
        "--dyld",
        default=os.path.join(home, "ios-userland", "extracted", "usr", "lib",
                             "dyld"),
        help="real /usr/lib/dyld")
    parser.add_argument(
        "--xpcproxy",
        default=os.path.join(home, "ios-userland", "extracted", "usr",
                             "libexec", "xpcproxy"),
        help="real /usr/libexec/xpcproxy")
    args = parser.parse_args()

    # SHA-256("abc"), FIPS 180-4.  The kernel's SHA256_Init/Update/Final
    # route (libkern/crypto/corecrypto_sha2.c) computes the same primitive;
    # this guards against a host Python whose hashlib is not SHA-256.
    if hashlib.sha256(b"abc").hexdigest() != (
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad"):
        die("host hashlib.sha256 self-test failed")

    with open(args.trust_cache, "rb") as f:
        raw = f.read()
    source = TrustCacheSource(args.trust_cache, raw)
    tc = TrustCache(source.payload, source)
    tc.summary()

    print()
    print("binary        slice  cd off    cd len  hash          page  "
          "slots     trust cache")
    results = []
    for path in (args.launchd, args.dyld, args.xpcproxy):
        name, row, ok = check_binary(path, tc)
        results.append((name, row, ok))
        print("%-13s 0x%-4x 0x%-7x %-7u %-13s %-5u %3u/%-3u   %s"
              % (name, row["slice"], row["cd_offset"], row["cd_length"],
                 "%s(%u)" % (HASH_NAME.get(row["hash_type"], "?"),
                             row["hash_type"]),
                 row["page_shift"], row["matched"], row["slots"],
                 "hit" if row["hit"] else "MISS"))
        print("              cdhash %s" % row["cdhash"])

    print()
    failures = 0
    for name, row, ok in results:
        expected = EXPECTED[name]
        if ok:
            print("PASS %-8s cdhash, CodeDirectory, %u/%u slots and trust "
                  "cache all match" % (name, row["matched"], row["slots"]))
        else:
            failures += 1
            print("FAIL %-8s expected cdhash %s cd 0x%x+%u hash %u page %u "
                  "slots %u/%u (got %s cd 0x%x+%u hash %u page %u %u/%u)"
                  % (name, expected["cdhash"], expected["cd_offset"],
                     expected["cd_length"], expected["hash_type"],
                     expected["page_shift"], expected["slots"][0],
                     expected["slots"][1], row["cdhash"], row["cd_offset"],
                     row["cd_length"], row["hash_type"], row["page_shift"],
                     row["matched"], row["slots"]))
    if failures:
        return 1
    print("OK: the kernel's cdhash and Apple's cache agree for all "
          "three binaries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
