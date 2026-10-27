#!/usr/bin/env python3
"""Extract and inspect the static trust cache shipped in an iOS IPSW.

The IPSW carries one trust cache per signed volume.  The one the kernel cares
about for platform-binary classification is the OS volume's, which
BuildManifest.plist calls "StaticTrustCache" and which lives at
Firmware/<OS dmg>.trustcache.

Format (20H392, iPhone10,3 / iPhone10,6, iOS 16.7.16):

    DER SEQUENCE {
        IA5String "IM4P"
        IA5String "trst"
        IA5String "1"
        OCTET STRING payload
    }

and payload is a trust cache module:

    u32 version (0, 1 or 2)
    u8  uuid[16]
    u32 numEntries
    entries[]  (20 bytes for v0, 22 for v1, 24 for v2)
        u8 cdhash[20]        first 20 bytes of the CodeDirectory hash
        u8 hashType          (v1/v2; 1=SHA-1 2=SHA-256 3=SHA-256-trunc 4=SHA-384)
        u8 flags             (v1/v2)
        u8 constraintCategory, u8 reserved   (v2)

Examples:

    # summary + where it would have to be placed on the root volume
    scripts/extract-ios-trustcache.py --ipsw ~/ios-work/iPhoneX-16.7.16.ipsw \
        --verify

    # write the raw IPSW member (this is what the kernel parser wants)
    scripts/extract-ios-trustcache.py --ipsw ... \
        --write ~/ios-userland/static.trustcache

    # is this Mach-O in Apple's trust cache?
    scripts/extract-ios-trustcache.py --ipsw ... --check /sbin/launchd

Only the zip member is read, so a local IPSW is enough; no range requests and
no APFS code are involved.
"""

import argparse
import hashlib
import plistlib
import struct
import sys
import zipfile

HEADER_SIZE = 24
CDHASH_SIZE = 20
ENTRY_SIZE = {0: 20, 1: 22, 2: 24}
HASH_NAMES = {1: "sha1", 2: "sha256", 3: "sha256-trunc", 4: "sha384"}
# Mirrors hashPriorities in bsd/kern/ubc_subr.c: higher wins.
HASH_RANK = {1: 1, 3: 2, 2: 3, 4: 4}
HASH_NAME = {1: "sha1", 2: "sha256", 3: "sha256", 4: "sha384"}
HASH_DIGEST = {1: 20, 2: 32, 3: 20, 4: 48}


def die(msg):
    raise SystemExit("error: " + msg)


def der_length(buf, off):
    if off >= len(buf):
        die("truncated DER length")
    first = buf[off]
    off += 1
    if first & 0x80 == 0:
        return first, off
    n = first & 0x7F
    if n == 0 or n > 4 or off + n > len(buf):
        die("bad DER length")
    return int.from_bytes(buf[off:off + n], "big"), off + n


def der_tlv(buf, off):
    if off >= len(buf):
        die("truncated DER tag")
    tag = buf[off]
    length, off = der_length(buf, off + 1)
    if off + length > len(buf):
        die("DER value runs past the buffer")
    return tag, buf[off:off + length], off + length


def im4p_payload(buf):
    """Return the OCTET STRING payload of an IM4P ("trst") wrapper."""
    tag, body, end = der_tlv(buf, 0)
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
    return payload


class TrustCache:
    def __init__(self, payload):
        if len(payload) < HEADER_SIZE:
            die("trust cache module is too short")
        self.version, = struct.unpack_from("<I", payload, 0)
        if self.version not in ENTRY_SIZE:
            die("unsupported trust cache version %u" % self.version)
        self.uuid = payload[4:20]
        self.count, = struct.unpack_from("<I", payload, 20)
        self.entry_size = ENTRY_SIZE[self.version]
        needed = HEADER_SIZE + self.count * self.entry_size
        if needed > len(payload):
            die("trust cache claims %u entries but the payload only holds %u"
                % (self.count, (len(payload) - HEADER_SIZE) // self.entry_size))
        self.entries = []
        previous = None
        self.sorted = True
        for i in range(self.count):
            off = HEADER_SIZE + i * self.entry_size
            cdhash = payload[off:off + CDHASH_SIZE]
            hash_type = payload[off + CDHASH_SIZE] if self.version >= 1 else None
            flags = payload[off + CDHASH_SIZE + 1] if self.version >= 1 else 0
            category = payload[off + 22] if self.version >= 2 else 0
            self.entries.append((cdhash, hash_type, flags, category))
            if previous is not None and previous >= cdhash:
                self.sorted = False
            previous = cdhash

    def lookup(self, cdhash, hash_type):
        for entry in self.entries:
            if entry[0] == cdhash and (entry[1] is None or entry[1] == hash_type):
                return entry
        return None

    def summary(self):
        counts = {}
        for _cdhash, hash_type, _flags, _category in self.entries:
            key = HASH_NAMES.get(hash_type, "v0/none")
            counts[key] = counts.get(key, 0) + 1
        print("version      %u" % self.version)
        print("uuid         %s" % self.uuid.hex())
        print("entries      %u" % self.count)
        print("entry size   %u bytes" % self.entry_size)
        print("hash types   %s" % ", ".join("%s=%u" % kv for kv in sorted(counts.items())))
        print("sorted       %s" % ("yes" if self.sorted else "no"))
        print("cdhash[0]    %s" % self.entries[0][0].hex() if self.entries else "-")
        print("cdhash[-1]   %s" % self.entries[-1][0].hex() if self.entries else "-")


def macho_slices(buf):
    magic = struct.unpack_from(">I", buf, 0)[0]
    if magic in (0xCAFEBABE, 0xCAFEBABF):
        count = struct.unpack_from(">I", buf, 4)[0]
        for i in range(count):
            _cputype, _cpusub, off, size, _align = struct.unpack_from(">iiIII", buf, 8 + i * 20)
            yield off, size
        return
    yield 0, len(buf)


def macho_cdhash(buf, base, size):
    """Return (cdhash, hash_type) for the best CodeDirectory in one slice."""
    magic, = struct.unpack_from("<I", buf, base)
    if magic != 0xFEEDFACF:
        die("only little-endian 64-bit Mach-O slices are supported")
    _cputype, _cpusub, _filetype, ncmds, _sizeofcmds, _flags, _reserved = \
        struct.unpack_from("<iiIIIII", buf, base + 4)
    off = base + 32
    sig_off = sig_size = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", buf, off)
        if cmd == 0x1D:  # LC_CODE_SIGNATURE
            sig_off, sig_size = struct.unpack_from("<II", buf, off + 8)
        off += cmdsize
    if sig_off is None:
        die("slice at 0x%x has no LC_CODE_SIGNATURE" % base)

    sb = base + sig_off
    magic, length, count = struct.unpack_from(">III", buf, sb)
    if magic != 0xFADE0CC0:  # CSMAGIC_EMBEDDED_SIGNATURE
        die("LC_CODE_SIGNATURE does not point at a SuperBlob")
    best = None
    for i in range(count):
        slot_type, slot_off = struct.unpack_from(">II", buf, sb + 12 + i * 8)
        blob = sb + slot_off
        blob_magic, blob_len = struct.unpack_from(">II", buf, blob)
        if blob_magic != 0xFADE0C02:  # CSMAGIC_CODEDIRECTORY
            continue
        if slot_type != 0 and not (0x1000 <= slot_type < 0x1005):
            continue
        hash_type = buf[blob + 37]
        rank = HASH_RANK.get(hash_type, 0)
        if rank == 0:
            continue
        if best is None or rank > best[0]:
            best = (rank, blob, blob_len, hash_type)
    if best is None:
        die("no usable CodeDirectory in the signature")

    _rank, blob, blob_len, hash_type = best
    digest = hashlib.new(HASH_NAME[hash_type], buf[blob:blob + blob_len]).digest()
    return digest[:CDHASH_SIZE], hash_type


def find_static_trust_cache(zf):
    try:
        manifest = plistlib.loads(zf.read("BuildManifest.plist"))
        for identity in manifest.get("BuildIdentities", []):
            entry = identity.get("Manifest", {}).get("StaticTrustCache")
            if entry:
                path = entry.get("Info", {}).get("Path")
                if path:
                    return path
    except KeyError:
        pass
    candidates = [n for n in zf.namelist() if n.endswith(".trustcache")]
    if not candidates:
        die("no .trustcache member and no StaticTrustCache in BuildManifest.plist")
    return candidates[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ipsw", required=True, help="local IPSW (zip) file")
    parser.add_argument("--member", help="trust cache member (default: BuildManifest StaticTrustCache)")
    parser.add_argument("--write", metavar="FILE", help="write the raw IPSW member")
    parser.add_argument("--verify", action="store_true", help="parse and print a summary")
    parser.add_argument("--dump", action="store_true", help="print every entry")
    parser.add_argument("--check", metavar="MACHO", help="check a Mach-O's cdhash against the cache")
    args = parser.parse_args()

    with zipfile.ZipFile(args.ipsw) as zf:
        member = args.member or find_static_trust_cache(zf)
        raw = zf.read(member)
    print("member       %s (%u bytes)" % (member, len(raw)))

    payload = im4p_payload(raw)
    tc = TrustCache(payload)

    if args.write:
        with open(args.write, "wb") as f:
            f.write(raw)
        print("wrote        %s" % args.write)
    if args.verify or not (args.dump or args.check or args.write):
        tc.summary()
    if args.dump:
        for cdhash, hash_type, flags, category in tc.entries:
            print("%s %-12s flags=0x%02x category=%u"
                  % (cdhash.hex(), HASH_NAMES.get(hash_type, "-"), flags, category))
    if args.check:
        with open(args.check, "rb") as f:
            buf = f.read()
        found = False
        for base, size in macho_slices(buf):
            cdhash, hash_type = macho_cdhash(buf, base, size)
            entry = tc.lookup(cdhash, hash_type)
            print("slice 0x%-8x cdhash %s (%s): %s"
                  % (base, cdhash.hex(), HASH_NAMES.get(hash_type, "?"),
                     "in trust cache" if entry else "NOT in trust cache"))
            found = found or bool(entry)
        if not found:
            sys.exit(1)


if __name__ == "__main__":
    main()
