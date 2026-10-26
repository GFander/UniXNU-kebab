#!/usr/bin/env python3
"""Turn the iOS 16 `.dmg.mtree` IPSW member into a plain path inventory.

Measured chain for iOS 16.7.16 (20H392), iPhone10,3 / D22AP:

    DER SEQUENCE {
        IA5String "IM4P"
        IA5String "msys"
        IA5String "0"
        OCTET STRING payload              (22,313,996 bytes)
    }

    payload:
        "pbze"
        u64be block_size                  (4 MiB)
        repeated until the payload ends:
            u64be uncompressed_size
            u64be compressed_size
            LZFSE stream  ("bvx2" ... "bvx$")

    decoded stream (93,000,868 bytes) is an Apple Archive:
        "AA01" + u16le header_size + typed fields
        field key = 3 ASCII chars + one type char:
            1 u8, 2 u16le, 4 u32le, 8 u64le,
            P u16le length + bytes (path),
            T 12 bytes (u64le seconds + u32le nanoseconds),
            B u32le length (data blob that follows the header)
        entries: "/" (dir), "digest.db" (file), "mtree.txt" (file)

    the `mtree.txt` payload (58,004,400 bytes) is Apple's hierarchical mtree:

        # ./usr/lib
        lib             type=dir inode=...
            dyld        xattrsdigest=... inode=...
        # ./usr/lib
        ..

    A "# path" comment opens a directory, the next entry is the directory
    itself, the indented entries are its non-directory children, and the
    repeated comment plus ".." closes the directory.  Each directory gets its
    own section, so every path appears exactly once.

The mtree records type, inode, siblingid, nxattr and xattrsdigest, but it does
not record mode or size.  Those two columns are written as "-" instead of
guessing them.

Examples:
    python3 scripts/decode-ios-mtree.py \
        --mtree ~/ios-userland/ipsw/Firmware/087-86698-044.dmg.mtree \
        --out ~/ios-userland/mtree-paths.txt --stats --check

    # read the 22 MB member out of the IPSW with range requests instead
    python3 scripts/decode-ios-mtree.py --url "$URL" \
        --out ~/ios-userland/mtree-paths.txt
"""
import argparse
import re
import struct
import sys
from pathlib import Path

try:
    import lzfse
except ImportError:  # pragma: no cover
    lzfse = None

PBZE_MAGIC = b"pbze"
AA_MAGIC = b"AA01"
DER_SEQUENCE = 0x30
DER_OCTET_STRING = 0x04
DER_IA5_STRING = 0x16
LZFSE_START = b"bvx2"
LZFSE_END = b"bvx$"

# The four paths the inventory is expected to answer questions about.
REQUIRED = [
    "/usr/lib/dyld",
    "/sbin/launchd",
    "/usr/lib/libSystem.B.dylib",
    "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e",
]


def log(msg):
    print(msg, file=sys.stderr)


# ---------------------------------------------------------------------------
# DER / IM4P
# ---------------------------------------------------------------------------
def _der_length(buf, pos):
    """Return (length, new_pos) for a DER length field at `buf[pos]`."""
    if pos >= len(buf):
        raise ValueError("truncated DER length")
    first = buf[pos]
    pos += 1
    if first < 0x80:
        return first, pos
    n = first & 0x7F
    if n == 0 or n > 4 or pos + n > len(buf):
        raise ValueError("unsupported DER length form 0x%02x" % first)
    return int.from_bytes(buf[pos:pos + n], "big"), pos + n


def der_octet_string(blob):
    """Pull the OCTET STRING payload out of an IM4P-style DER SEQUENCE."""
    if not blob or blob[0] != DER_SEQUENCE:
        raise ValueError("not a DER SEQUENCE (first byte 0x%02x)"
                         % (blob[0] if blob else -1))
    length, pos = _der_length(blob, 1)
    end = pos + length
    if end > len(blob):
        raise ValueError("DER SEQUENCE says %d bytes but only %d remain"
                         % (length, len(blob) - pos))
    items = []
    while pos < end:
        tag = blob[pos]
        pos += 1
        ln, pos = _der_length(blob, pos)
        if pos + ln > end:
            raise ValueError("DER item overruns SEQUENCE")
        items.append((tag, blob[pos:pos + ln]))
        pos += ln
    if not items or items[0][1] != b"IM4P":
        raise ValueError("DER SEQUENCE does not start with IA5String 'IM4P'")
    if len(items) < 2 or items[1][1] != b"msys":
        got = items[1][1] if len(items) > 1 else b""
        raise ValueError("unexpected IM4P type %r (wanted b'msys')" % got)
    for tag, value in items:
        if tag == DER_OCTET_STRING and value[:4] == PBZE_MAGIC:
            return value
    for tag, value in items:
        if tag == DER_OCTET_STRING:
            return value
    raise ValueError("no OCTET STRING in DER SEQUENCE")


# ---------------------------------------------------------------------------
# pbze
# ---------------------------------------------------------------------------
def decode_pbze(payload):
    """Decode pbze -> (bytes, block_size, [(usize, csize), ...])."""
    if payload[:4] != PBZE_MAGIC:
        raise ValueError("not pbze (starts %r)" % payload[:8])
    if len(payload) < 12:
        raise ValueError("truncated pbze header")
    (block_size,) = struct.unpack(">Q", payload[4:12])
    pos = 12
    out = bytearray()
    blocks = []
    while pos < len(payload):
        if pos + 16 > len(payload):
            raise ValueError("truncated block header at %d" % pos)
        usize, csize = struct.unpack(">QQ", payload[pos:pos + 16])
        pos += 16
        chunk = payload[pos:pos + csize]
        if len(chunk) != csize:
            raise ValueError("block %d: want %d compressed bytes, have %d"
                             % (len(blocks), csize, len(chunk)))
        if chunk[:4] != LZFSE_START or chunk[-4:] != LZFSE_END:
            raise ValueError("block %d is not a complete LZFSE stream "
                             "(head %r tail %r)"
                             % (len(blocks), chunk[:4], chunk[-4:]))
        if lzfse is None:
            raise RuntimeError("the lzfse module is required; run this with "
                               "~/ios-userland/venv/bin/python")
        raw = lzfse.decompress(chunk)
        if len(raw) != usize:
            raise ValueError("block %d: LZFSE gave %d bytes, header says %d"
                             % (len(blocks), len(raw), usize))
        out += raw
        blocks.append((usize, csize))
        pos += csize
    if pos != len(payload):
        raise ValueError("pbze block table overruns payload: %d != %d"
                         % (pos, len(payload)))
    return bytes(out), block_size, blocks


# ---------------------------------------------------------------------------
# Apple Archive
# ---------------------------------------------------------------------------
AA_TYPE = {ord("D"): "dir", ord("F"): "file", ord("L"): "link"}


def _aa_fields(header):
    """Parse the typed fields of one AA01 header.

    Returns (fields, data_length).  `data_length` is the DATB value: the
    number of payload bytes that follow this header.
    """
    fields = {}
    data_length = 0
    pos = 6  # AA01 + u16le header_size
    while pos < len(header):
        if pos + 4 > len(header):
            raise ValueError("truncated AA field key")
        key = header[pos:pos + 4].decode("ascii")
        pos += 4
        kind = key[3]
        if kind == "1":
            value = header[pos]
            pos += 1
        elif kind == "2":
            value = struct.unpack("<H", header[pos:pos + 2])[0]
            pos += 2
        elif kind == "4":
            value = struct.unpack("<I", header[pos:pos + 4])[0]
            pos += 4
        elif kind == "8":
            value = struct.unpack("<Q", header[pos:pos + 8])[0]
            pos += 8
        elif kind == "P":
            n = struct.unpack("<H", header[pos:pos + 2])[0]
            pos += 2
            value = header[pos:pos + n].decode("utf-8", "surrogateescape")
            pos += n
        elif kind == "T":
            sec, nsec = struct.unpack("<QI", header[pos:pos + 12])
            pos += 12
            value = (sec, nsec)
        elif kind == "B":
            value = struct.unpack("<I", header[pos:pos + 4])[0]
            pos += 4
            data_length = value
        else:
            raise ValueError("unknown AA field type %r in %r" % (kind, key))
        if pos > len(header):
            raise ValueError("AA field %r overruns header" % key)
        fields[key] = value
    return fields, data_length


def aa_entries(blob):
    """Walk an Apple Archive, yielding (name, type, fields, data)."""
    pos = 0
    while pos < len(blob):
        if blob[pos:pos + 4] != AA_MAGIC:
            raise ValueError("no AA01 magic at offset %d (got %r)"
                             % (pos, blob[pos:pos + 4]))
        if pos + 6 > len(blob):
            raise ValueError("truncated AA01 header at %d" % pos)
        (header_size,) = struct.unpack("<H", blob[pos + 4:pos + 6])
        if header_size < 6 or pos + header_size > len(blob):
            raise ValueError("bad AA header size %d at %d"
                             % (header_size, pos))
        header = blob[pos:pos + header_size]
        fields, data_length = _aa_fields(header)
        start = pos + header_size
        end = start + data_length
        if end > len(blob):
            raise ValueError("entry %r wants %d data bytes, only %d remain"
                             % (fields.get("PATP"), data_length,
                                len(blob) - start))
        typ = AA_TYPE.get(fields.get("TYP1"), "?")
        yield fields.get("PATP", ""), typ, fields, blob[start:end]
        pos = end
    if pos != len(blob):
        raise ValueError("Apple Archive did not end on an entry boundary")


# ---------------------------------------------------------------------------
# hierarchical mtree text
# ---------------------------------------------------------------------------
def unescape_name(name):
    """Undo mtree \\ooo escapes in an entry name (spaces are \\040).

    The `# path` comments are printed unescaped, so entry names have to be
    unescaped before they can be compared with the comment's basename.
    """
    out = []
    i = 0
    while i < len(name):
        if (name[i] == "\\" and i + 3 < len(name)
                and all(c in "01234567" for c in name[i + 1:i + 4])):
            out.append(chr(int(name[i + 1:i + 4], 8)))
            i += 4
        else:
            out.append(name[i])
            i += 1
    return "".join(out)


def check_entry_line(line, lineno):
    """Reject a line that is not exactly "one name token + key=value fields".

    Names with spaces are written \\040, so the first token is the whole name
    and the second token must already be an attribute.  If it is not, the name
    had an unescaped space and the path would be silently cut short.
    """
    stripped = line.strip()
    if stripped.startswith("#") or stripped.startswith("/") or stripped == "..":
        return
    tokens = stripped.split()
    first_attr = next((i for i, tok in enumerate(tokens) if "=" in tok), None)
    if first_attr != 1:
        raise ValueError("line %d is not name + attributes: %r"
                         % (lineno, line[:120]))


def mtree_records(text):
    """Yield (path, type, inode, xattrsdigest) from the mtree text."""
    cur = "."
    buf = []
    lineno = 0
    for raw in text.split("\n"):
        lineno += 1
        if raw.endswith("\\"):
            buf.append(raw[:-1])
            continue
        line = "".join(buf) + raw
        buf = []
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            # "# ." or "# ./some/dir" is the current-directory marker.
            mark = stripped[1:].strip()
            if mark == "." or mark.startswith("./"):
                cur = mark
            continue
        if stripped.startswith("/set"):
            continue
        if stripped == "..":
            continue
        check_entry_line(line, lineno)
        name, rest = stripped.split(None, 1)
        name = unescape_name(name)
        attrs = dict(re.findall(r"([A-Za-z0-9_]+)=(\S+)", rest))
        typ = attrs.get("type", "file")
        if name == "." and cur == ".":
            path = "/"
        elif typ == "dir" and name == cur.rsplit("/", 1)[-1]:
            path = "/" if cur == "." else cur[1:]
        else:
            path = ("/" + name) if cur == "." else (cur[1:] + "/" + name)
        yield path, typ, attrs.get("inode", "-"), attrs.get("xattrsdigest", "-")


# ---------------------------------------------------------------------------
# driver
# ---------------------------------------------------------------------------
def load_extractor():
    """Import the sibling range-request tool despite its hyphenated name."""
    import importlib.util
    path = Path(__file__).with_name("extract-ios-userland.py")
    spec = importlib.util.spec_from_file_location("extract_ios_userland", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_member(args):
    if args.mtree:
        return Path(args.mtree).read_bytes()
    if not args.url:
        raise SystemExit("give --mtree PATH or --url URL")
    ext = load_extractor()
    src = ext.open_source(args.url)
    try:
        zipf = ext.RemoteZip(src)
        if args.member:
            entry = zipf.find(args.member)
        else:
            candidates = [e for e in zipf.entries
                          if e.name.endswith(".dmg.mtree")]
            if len(candidates) != 1:
                raise SystemExit("found %d .dmg.mtree members; pass --member"
                                 % len(candidates))
            entry = candidates[0]
        if entry is None:
            raise SystemExit("member not found: %s" % args.member)
        log("member %s: %d compressed, %d uncompressed, method %d"
            % (entry.name, entry.csize, entry.usize, entry.method))
        return zipf.read_member(entry)
    finally:
        src.close()


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="decode the iOS 16 .dmg.mtree member into a path inventory")
    ap.add_argument("--mtree", help="local .dmg.mtree member")
    ap.add_argument("--url", help="IPSW URL (fetched with range requests)")
    ap.add_argument("--member", help="IPSW member name (default: the .dmg.mtree)")
    ap.add_argument("--out", default="mtree-paths.txt",
                    help="inventory output (default: mtree-paths.txt)")
    ap.add_argument("--raw-mtree", help="also write the decoded mtree text here")
    ap.add_argument("--stats", action="store_true", help="print decode stats")
    ap.add_argument("--check", action="store_true",
                    help="check the required paths and print nearest matches")
    args = ap.parse_args(argv)

    blob = read_member(args)
    log("member: %d bytes" % len(blob))
    payload = der_octet_string(blob)
    log("DER/IM4P msys payload: %d bytes" % len(payload))
    archive, block_size, blocks = decode_pbze(payload)
    log("pbze: block size %d, %d blocks, %d decoded bytes"
        % (block_size, len(blocks), len(archive)))

    entries = list(aa_entries(archive))
    for name, typ, fields, data in entries:
        log("Apple Archive entry %-12r type=%-4s data=%d"
            % (name, typ, len(data)))
    mtree_entry = next((e for e in entries if e[0] == "mtree.txt"), None)
    if mtree_entry is None:
        raise SystemExit("Apple Archive has no mtree.txt entry")
    text = mtree_entry[3].decode("utf-8", "surrogateescape")
    log("mtree.txt: %d bytes, %d lines" % (len(text), text.count("\n")))
    if args.raw_mtree:
        Path(args.raw_mtree).write_text(text, encoding="utf-8",
                                        errors="surrogateescape")

    records = list(mtree_records(text))
    counts = {}
    for _, typ, _, _ in records:
        counts[typ] = counts.get(typ, 0) + 1

    out = Path(args.out)
    with out.open("w", encoding="utf-8", errors="surrogateescape",
                  newline="\n") as fh:
        fh.write("# iOS 16.7.16 (20H392) root volume path inventory\n")
        fh.write("# from the IPSW member .dmg.mtree: DER/IM4P 'msys' -> pbze\n")
        fh.write("# -> %d LZFSE blocks -> Apple Archive -> mtree.txt\n" %
                 len(blocks))
        fh.write("# the mtree records type/inode/xattrsdigest but no mode or "
                 "size, so those are '-'\n")
        fh.write("# columns: type\tmode\tsize\tinode\txattrsdigest\tpath\n")
        for path, typ, inode, xdigest in records:
            fh.write("%s\t-\t-\t%s\t%s\t%s\n"
                     % (typ, inode, xdigest, path))

    log("inventory: %d paths -> %s" % (len(records), out))
    if args.stats:
        log("  dirs  %d" % counts.get("dir", 0))
        log("  files %d" % counts.get("file", 0))
        log("  links %d" % counts.get("link", 0))
        for typ in sorted(set(counts) - {"dir", "file", "link"}):
            log("  %s %d" % (typ, counts[typ]))
        log("  bytes %d" % out.stat().st_size)

    if args.check:
        index = {path for path, _, _, _ in records}
        children = {}
        for path, _, _, _ in records:
            children.setdefault(path.rsplit("/", 1)[0] or "/", []).append(path)
        for want in REQUIRED:
            if want in index:
                rec = next(r for r in records if r[0] == want)
                log("FOUND   %s  (type=%s inode=%s xattrsdigest=%s)"
                    % (want, rec[1], rec[2], rec[3]))
                continue
            log("ABSENT  %s" % want)
            ancestor = want.rsplit("/", 1)[0] or "/"
            while ancestor not in index and ancestor != "/":
                ancestor = ancestor.rsplit("/", 1)[0] or "/"
            if ancestor in index:
                kids = children.get(ancestor, [])[:8]
                log("        nearest existing dir %s has: %s"
                    % (ancestor, ", ".join(kids)))
            stem = want.rsplit("/", 1)[-1]
            scored = []
            for path, _, _, _ in records:
                other = path.rsplit("/", 1)[-1]
                n = 0
                while n < len(stem) and n < len(other) and stem[n] == other[n]:
                    n += 1
                if n >= 4:
                    scored.append((n, path))
            scored.sort(reverse=True)
            hits = [p for _, p in scored[:6]]
            if hits:
                log("        nearest basenames: %s" % ", ".join(hits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
