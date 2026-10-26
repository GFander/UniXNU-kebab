#!/usr/bin/env python3
"""Extract files from the APFS root volume of an iOS IPSW over HTTP ranges.

The APFS chain (container -> volume -> fs-tree) and the B-tree offset rules
come from probe-ios-apfs.py.  This tool adds what is needed to turn an inode
record into bytes:

  * a path resolver that walks j_drec records from the root directory (oid 2),
  * a reader for j_file_extent_val_t records, including sparse holes and
    multi-extent streams,
  * decmpfs handling for inline and resource-fork compressed files
    (types 3/4/7/8/9/10/11/12),
  * a Mach-O header/load-command summary and sha256 for verification.

Examples:
    python3 scripts/apfs-get.py --url "$URL" \
        --get /usr/lib/dyld --get /sbin/launchd --out ~/ios-userland/extracted

    python3 scripts/apfs-get.py --url "$URL" --get /usr/bin/zprint \
        --out /tmp/out --verbose
"""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
INSIDE = 0x0FFFFFFFFFFFFFFF

# APFS record types
APFS_TYPE_INODE = 3
APFS_TYPE_XATTR = 4
APFS_TYPE_FILE_EXTENT = 8
APFS_TYPE_DIR_RECORD = 9

# inode bsd flags / decmpfs types
UF_COMPRESSED = 0x20
DECMPFS_MAGIC = 0x636D7066
CMP_TYPE_RAW_XATTR = 1
CMP_TYPE_ZLIB_XATTR = 3
CMP_TYPE_ZLIB_RSRC = 4
CMP_TYPE_LZVN_XATTR = 7
CMP_TYPE_LZVN_RSRC = 8
CMP_TYPE_RAW_XATTR2 = 9
CMP_TYPE_RAW_RSRC = 10
CMP_TYPE_LZFSE_XATTR = 11
CMP_TYPE_LZFSE_RSRC = 12
CMP_TYPE_LZBITMAP_XATTR = 13
CMP_TYPE_LZBITMAP_RSRC = 14

CHUNK = 65536


def log(msg):
    print(msg, file=sys.stderr)


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_probe():
    return load_module(HERE / "probe-ios-apfs.py", "probe_ios_apfs")


# --------------------------------------------------------------------------
# file-system tree cursor
# --------------------------------------------------------------------------
class FSTree:
    """In-order cursor over the APFS fs-tree, plus drec path resolution."""

    def __init__(self, fs):
        self.fs = fs
        self._drec_cache = {}

    # -- node helpers -----------------------------------------------------
    def _node(self, paddr):
        b = self.fs.block(paddr)
        flags, level, ents = self.fs.entries(b, fixed=False)
        return flags, level, ents

    def _child(self, val):
        if len(val) < 8:
            raise RuntimeError("short internal-node value")
        return struct.unpack("<Q", val[:8])[0]

    def seek(self, obj, typ):
        """Land on the first leaf that can contain (obj, typ).

        We deliberately descend with a strict comparison: when a separator is
        exactly (obj, typ), the previous child may still hold records of that
        object, because a leaf boundary can split one object's records.  The
        caller scans forward and stops on the first key past (obj, typ).
        """
        node = self.fs.volume["root_tree_oid"]
        path = []
        while True:
            res = self.fs.omap_lookup(self.fs.omap_tree_paddr, node)
            if not res:
                raise RuntimeError("fs-tree node oid %d is not in the omap" % node)
            paddr = res[1][2]
            _flags, level, ents = self._node(paddr)
            if level == 0:
                return path, paddr, ents
            chosen = None
            for i, (key, val) in enumerate(ents):
                o, t = self.fs.jkey(key)
                if (o, t) < (obj, typ):
                    chosen = (i, self._child(val))
            if chosen is None:
                chosen = (0, self._child(ents[0][1]))
            path.append((paddr, ents, chosen[0]))
            node = chosen[1]

    def _leftmost(self, node):
        path = []
        while True:
            res = self.fs.omap_lookup(self.fs.omap_tree_paddr, node)
            if not res:
                raise RuntimeError("fs-tree node oid %d is not in the omap" % node)
            paddr = res[1][2]
            _flags, level, ents = self._node(paddr)
            if level == 0:
                return path, paddr, ents
            path.append((paddr, ents, 0))
            node = self._child(ents[0][1])

    def _next_leaf(self, path):
        while path:
            paddr, ents, idx = path[-1]
            if idx + 1 < len(ents):
                child = self._child(ents[idx + 1][1])
                newpath = path[:-1] + [(paddr, ents, idx + 1)]
                dpath, leaf, le = self._leftmost(child)
                return newpath + dpath, leaf, le
            path = path[:-1]
        return None, None, None

    def iter_records(self, obj, typ):
        """Yield (key, value) for every (obj, typ) record, in tree order."""
        path, leaf, ents = self.seek(obj, typ)
        while ents is not None:
            for key, val in ents:
                o, t = self.fs.jkey(key)
                if o > obj or (o == obj and t > typ):
                    return
                if o == obj and t == typ:
                    yield key, val
            path, leaf, ents = self._next_leaf(path)

    # -- directory records ------------------------------------------------
    @staticmethod
    def drec_name(key):
        if len(key) < 12:
            return None
        nlen = struct.unpack("<I", key[8:12])[0] & 0x3FF
        if nlen == 0 or 12 + nlen > len(key):
            return None
        return key[12:12 + nlen].split(b"\0")[0]

    def drec_lookup(self, dir_oid, name):
        key = (dir_oid, name)
        if key in self._drec_cache:
            return self._drec_cache[key]
        want = name.encode("utf-8")
        for k, v in self.iter_records(dir_oid, APFS_TYPE_DIR_RECORD):
            if self.drec_name(k) == want:
                if len(v) < 8:
                    raise RuntimeError("short drec value for %r" % name)
                file_id = struct.unpack("<Q", v[:8])[0]
                self._drec_cache[key] = file_id
                return file_id
        self._drec_cache[key] = None
        return None

    def resolve(self, path):
        """Resolve an absolute path to an inode/object id."""
        if not path.startswith("/"):
            raise ValueError("path must be absolute: %r" % path)
        oid = 2  # APFS root directory
        for part in path.split("/"):
            if not part or part == ".":
                continue
            if part == "..":
                raise ValueError("'..' is not supported: %r" % path)
            child = self.drec_lookup(oid, part)
            if child is None:
                raise FileNotFoundError("%s: no such file or directory" % path)
            oid = child
        return oid


# --------------------------------------------------------------------------
# inode / xattr parsing
# --------------------------------------------------------------------------
def parse_inode(val):
    if len(val) < 92:
        raise RuntimeError("short inode value (%d bytes)" % len(val))
    parent, private = struct.unpack("<QQ", val[0:16])
    iflags, nlink = struct.unpack("<Qi", val[48:60])
    prot, wgen, bsd, owner, group, mode, pad, size = struct.unpack(
        "<IIIIIHHQ", val[60:92])
    return {
        "parent": parent,
        "private": private,
        "internal_flags": iflags,
        "nlink": nlink,
        "protection": prot,
        "write_generation": wgen,
        "bsd_flags": bsd,
        "owner": owner,
        "group": group,
        "mode": mode,
        "size": size,
    }


def parse_xfields(val):
    """Best-effort xfield walk.  iOS stores INO_EXT_TYPE_NAME as type 4."""
    if len(val) < 96:
        return []
    count, used = struct.unpack("<HH", val[92:96])
    out = []
    pos = 96
    for _ in range(count):
        if pos + 4 > len(val):
            break
        xt, xf, xs = struct.unpack("<BBH", val[pos:pos + 4])
        data = val[pos + 4:pos + 4 + xs]
        out.append((xt, xf, data))
        pos += 4 + xs
        if xs % 8:
            pos += 8 - (xs % 8)
    return out


def find_dstream(val):
    """Find a j_dstream_t in an inode tail.

    Some inodes (e.g. the SystemVersion.plist in this image) carry the name
    xfield with an extra word and store the dstream immediately after it, so
    the fixed xfield walk does not reach it.  We only use this heuristic when
    the inode's size field is zero and an extent exists.
    """
    for pos in range(92, len(val) - 39, 4):
        size, alloced, crypto, written, read = struct.unpack(
            "<QQQQQ", val[pos:pos + 40])
        if not (0 < size <= alloced <= (1 << 40)):
            continue
        if alloced % 4096 or crypto:
            continue
        if alloced != ((size + 4095) // 4096) * 4096:
            continue
        if written > alloced or read > alloced:
            continue
        return {"size": size, "alloced_size": alloced, "crypto": crypto,
                "written": written, "read": read, "offset": pos}
    return None


def parse_xattr_val(val):
    if len(val) < 4:
        raise RuntimeError("short xattr value")
    flags, dlen = struct.unpack("<HH", val[:4])
    if flags & 1:  # XATTR_DATA_STREAM
        if len(val) < 52:
            raise RuntimeError("short xattr data-stream value")
        oid = struct.unpack("<Q", val[4:12])[0]
        ds = struct.unpack("<QQQQQ", val[12:52])
        return {"kind": "stream", "oid": oid, "size": ds[0],
                "alloced_size": ds[1], "crypto": ds[2],
                "written": ds[3], "read": ds[4]}
    return {"kind": "inline", "flags": flags, "data": val[4:4 + dlen]}


# --------------------------------------------------------------------------
# decmpfs
# --------------------------------------------------------------------------
def lzfse_module():
    try:
        import lzfse
        return lzfse
    except ImportError:
        return None


def lzvn_decode(data, expected):
    """Decode one raw LZVN chunk.

    The lzfse module exposes LZFSE streams, not LZVN.  A one-block LZFSE
    stream is just the 'bvxn' block header, the LZVN payload, and the
    end-of-stream magic; the C decoder then dispatches to LZVN for us.
    """
    mod = lzfse_module()
    if mod is None:
        raise RuntimeError("LZVN decoding needs the 'lzfse' Python module")
    stream = (b"bvxn" + struct.pack("<II", expected, len(data)) + data +
              b"bvx$")
    out = mod.decompress(stream)
    if len(out) != expected:
        raise RuntimeError("LZVN decoded %d bytes, expected %d"
                           % (len(out), expected))
    return out


def lzfse_decode(data, expected):
    mod = lzfse_module()
    if mod is None:
        raise RuntimeError("LZFSE decoding needs the 'lzfse' Python module")
    out = mod.decompress(data)
    if len(out) != expected:
        raise RuntimeError("LZFSE decoded %d bytes, expected %d"
                           % (len(out), expected))
    return out


def decode_chunk(ctype, blob, expected):
    """Decode one 64 KiB chunk according to its decmpfs type."""
    if ctype == CMP_TYPE_ZLIB_RSRC:
        if blob and blob[0] == 0xFF:
            out = blob[1:]
        else:
            out = zlib.decompress(blob)
    elif ctype == CMP_TYPE_LZVN_RSRC:
        if blob and blob[0] == 0x06:
            out = blob[1:]
        else:
            out = lzvn_decode(blob, expected)
    elif ctype == CMP_TYPE_RAW_RSRC:
        if blob and blob[0] == 0xCC:
            out = blob[1:]
        else:
            out = blob
    elif ctype == CMP_TYPE_LZFSE_RSRC:
        if blob and blob[0] == 0xFF:
            out = blob[1:]
        else:
            out = lzfse_decode(blob, expected)
    else:
        raise RuntimeError("unsupported resource-fork compression type %d" % ctype)
    if len(out) != expected:
        raise RuntimeError("chunk decoded to %d bytes, expected %d"
                           % (len(out), expected))
    return out


def decompress_resource(ctype, rsrc, usize):
    """Decode a decmpfs resource fork (fixed type-4 or absolute type 8/10/12)."""
    if usize == 0:
        return b""
    if ctype == CMP_TYPE_ZLIB_RSRC:
        if len(rsrc) < 0x108:
            raise RuntimeError("short zlib resource fork")
        header_size = struct.unpack(">I", rsrc[0:4])[0]
        if header_size != 0x100:
            raise RuntimeError("unexpected zlib resource-fork header size %d"
                               % header_size)
        count = struct.unpack("<I", rsrc[0x104:0x108])[0]
        want_count = (usize + CHUNK - 1) // CHUNK
        if count != want_count:
            raise RuntimeError("resource fork has %d chunks, expected %d"
                               % (count, want_count))
        out = bytearray()
        for i in range(count):
            off, clen = struct.unpack("<II", rsrc[0x108 + i * 8:
                                                     0x110 + i * 8])
            start = 0x104 + off
            blob = rsrc[start:start + clen]
            if len(blob) != clen:
                raise RuntimeError("short chunk %d (%d of %d bytes)"
                                   % (i, len(blob), clen))
            expected = min(CHUNK, usize - len(out))
            out += decode_chunk(ctype, blob, expected)
        return bytes(out)

    if ctype in (CMP_TYPE_LZVN_RSRC, CMP_TYPE_RAW_RSRC,
                 CMP_TYPE_LZFSE_RSRC):
        count = (usize + CHUNK - 1) // CHUNK
        if len(rsrc) < 4 * (count + 1):
            raise RuntimeError("short absolute-offset chunk table")
        offs = [struct.unpack("<I", rsrc[4 * i:4 * i + 4])[0]
                for i in range(count + 1)]
        if offs != sorted(offs):
            raise RuntimeError("chunk offsets are not ascending: %r" % offs)
        out = bytearray()
        for i in range(count):
            blob = rsrc[offs[i]:offs[i + 1]]
            expected = min(CHUNK, usize - len(out))
            out += decode_chunk(ctype, blob, expected)
        return bytes(out)

    raise RuntimeError("unsupported resource-fork compression type %d" % ctype)


def decompress_inline(ctype, attr, usize):
    if ctype in (CMP_TYPE_RAW_XATTR, CMP_TYPE_RAW_XATTR2):
        out = attr
    elif ctype == CMP_TYPE_ZLIB_XATTR:
        if attr and attr[0] == 0xFF:
            out = attr[1:]
        else:
            out = zlib.decompress(attr)
    elif ctype == CMP_TYPE_LZVN_XATTR:
        if attr and attr[0] == 0x06:
            out = attr[1:]
        else:
            out = lzvn_decode(attr, usize)
    elif ctype == CMP_TYPE_LZFSE_XATTR:
        if attr and attr[0] == 0xFF:
            out = attr[1:]
        else:
            out = lzfse_decode(attr, usize)
    else:
        raise RuntimeError("unsupported inline compression type %d" % ctype)
    if len(out) != usize:
        raise RuntimeError("inline data is %d bytes, expected %d"
                           % (len(out), usize))
    return out


# --------------------------------------------------------------------------
# data streams / extents
# --------------------------------------------------------------------------
def read_xattr(fs, oid, name):
    for key, val in fs.iter_records(oid, APFS_TYPE_XATTR):
        if len(key) < 10:
            continue
        nlen = struct.unpack("<H", key[8:10])[0]
        nm = key[10:10 + nlen].split(b"\0")[0].decode("utf-8", "replace")
        if nm == name:
            return parse_xattr_val(val)
    return None


def read_stream(fs, oid, size, verbose=False):
    """Read an APFS data stream, following all extents and filling holes."""
    extents = []
    for key, val in fs.iter_records(oid, APFS_TYPE_FILE_EXTENT):
        if len(key) < 16 or len(val) < 24:
            raise RuntimeError("short extent record for oid %d" % oid)
        logical = struct.unpack("<Q", key[8:16])[0]
        len_flags, paddr, crypto = struct.unpack("<QQQ", val[:24])
        length = len_flags & 0x00FFFFFFFFFFFFFF
        flags = len_flags >> 56
        if length:
            extents.append({"logical": logical, "length": length,
                            "paddr": paddr, "crypto": crypto,
                            "flags": flags})
    extents.sort(key=lambda e: e["logical"])

    if size is None:
        size = max((e["logical"] + e["length"] for e in extents), default=0)

    out = bytearray()
    for e in extents:
        if e["logical"] > len(out):
            gap = min(e["logical"], size) - len(out)
            if gap > 0:
                out += b"\0" * gap
        if e["logical"] + e["length"] <= len(out):
            continue
        if e["logical"] >= size:
            break
        if e["crypto"]:
            raise RuntimeError("extent at logical %d uses crypto id %d; "
                               "cannot decrypt" % (e["logical"], e["crypto"]))
        start = max(0, len(out) - e["logical"])
        take = min(e["length"] - start, size - len(out))
        if take <= 0:
            continue
        abs_off = fs.fs.base + e["paddr"] * fs.fs.bs + start
        data = fs.fs.r.read_at(abs_off, take)
        if len(data) != take:
            raise RuntimeError("short extent read at logical %d: %d of %d"
                               % (e["logical"], len(data), take))
        out += data
        e["read"] = take
        if verbose:
            log("    extent logical=%d len=%d paddr=%d read=%d"
                % (e["logical"], e["length"], e["paddr"], take))
        if len(out) >= size:
            break
    if len(out) < size:
        out += b"\0" * (size - len(out))
    return bytes(out), extents


# --------------------------------------------------------------------------
# Mach-O summary
# --------------------------------------------------------------------------
CPU_NAMES = {
    0x0100000C: "arm64",
    0x01000007: "x86_64",
    0x0000000C: "arm",
    0x00000007: "i386",
}
FILETYPE_NAMES = {
    1: "object", 2: "executable", 3: "fvmlib", 4: "core", 5: "preload",
    6: "dylib", 7: "dylinker", 8: "bundle", 9: "dylib-stub", 10: "dsym",
    11: "kext-bundle",
}
LC_UUID = 0x1B
LC_BUILD_VERSION = 0x32
LC_VERSION_MIN_MACOSX = 0x24
LC_VERSION_MIN_IPHONEOS = 0x25
LC_VERSION_MIN_TVOS = 0x2F
LC_VERSION_MIN_WATCHOS = 0x30


def fmt_version(v):
    return "%d.%d.%d" % ((v >> 16) & 0xFFFF, (v >> 8) & 0xFF, v & 0xFF)


def fmt_uuid(b):
    a, b0, c = struct.unpack("<IHH", b[:8])
    return "%08x-%04x-%04x-%s-%s" % (a, b0, c, b[8:10].hex(), b[10:16].hex())


def macho_info(data):
    if len(data) < 28:
        return None
    magic_le = struct.unpack("<I", data[:4])[0]
    if magic_le not in (0xFEEDFACF, 0xFEEDFACE, 0xCAFEBABE, 0xBEBAFECA):
        return None
    if magic_le in (0xCAFEBABE, 0xBEBAFECA):
        return {"format": "Mach-O universal/fat", "magic": hex(magic_le),
                "ncmds": None}
    is64 = magic_le == 0xFEEDFACF
    if is64:
        magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, _res = \
            struct.unpack("<IiiIIIII", data[:32])
        off = 32
    else:
        magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags = \
            struct.unpack("<IiiIIII", data[:28])
        off = 28
    info = {
        "format": "Mach-O %s %s" % ("64-bit" if is64 else "32-bit",
                                    CPU_NAMES.get(cputype & 0xFFFFFFFF,
                                                  hex(cputype))),
        "magic": hex(magic),
        "cputype": hex(cputype & 0xFFFFFFFF),
        "cpusubtype": hex(cpusub & 0xFFFFFFFF),
        "filetype": filetype,
        "filetype_name": FILETYPE_NAMES.get(filetype, "unknown"),
        "ncmds": ncmds,
        "sizeofcmds": sizeofcmds,
        "flags": hex(flags),
        "uuid": None,
        "build_version": None,
        "min_version": None,
        "dylinker": None,
    }
    for _ in range(ncmds):
        if off + 8 > len(data):
            break
        cmd, cmdsize = struct.unpack("<II", data[off:off + 8])
        if cmdsize < 8 or off + cmdsize > len(data):
            break
        if cmd == LC_UUID and cmdsize >= 24:
            info["uuid"] = fmt_uuid(data[off + 8:off + 24])
        elif cmd == LC_BUILD_VERSION and cmdsize >= 24:
            plat, minos, sdk, ntools = struct.unpack(
                "<IIII", data[off + 8:off + 24])
            info["build_version"] = {
                "platform": plat, "minos": fmt_version(minos),
                "sdk": fmt_version(sdk), "ntools": ntools,
            }
        elif cmd in (LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS,
                     LC_VERSION_MIN_TVOS, LC_VERSION_MIN_WATCHOS):
            ver, sdk = struct.unpack("<II", data[off + 8:off + 16])
            info["min_version"] = {"cmd": hex(cmd),
                                   "version": fmt_version(ver),
                                   "sdk": fmt_version(sdk)}
        elif cmd == 0x0E and cmdsize > 12:  # LC_LOAD_DYLINKER
            raw = data[off + 12:off + cmdsize].split(b"\0")[0]
            info["dylinker"] = raw.decode("utf-8", "replace")
        off += cmdsize
    return info


def proof_strings(data, path):
    """A couple of strings that identify the binary."""
    out = []
    if path.endswith("/dyld"):
        for m in re.finditer(rb"dyld-[0-9][^\x00\n]{0,48}", data):
            out.append(m.group(0).decode("latin1"))
            if len(out) >= 3:
                break
        if not out:
            for m in re.finditer(rb"@\(#\)[^\x00\n]{0,80}", data):
                out.append(m.group(0).decode("latin1"))
                if len(out) >= 2:
                    break
    elif path.endswith("/launchd"):
        for pat in (rb"/sbin/launchd", rb"launchd[^\x00\n]{0,40}"):
            for m in re.finditer(pat, data):
                out.append(m.group(0).decode("latin1"))
                if len(out) >= 3:
                    return out
    else:
        for m in re.finditer(rb"[ -~]{10,}", data):
            s = m.group(0).decode("latin1")
            if "/" in s or s.startswith("Apple"):
                out.append(s)
                if len(out) >= 2:
                    break
    return out


# --------------------------------------------------------------------------
# extraction
# --------------------------------------------------------------------------
def extract(fs, path, verbose=False):
    oid = fs.resolve(path)
    inode = None
    inode_raw = None
    for _k, v in fs.iter_records(oid, APFS_TYPE_INODE):
        inode = parse_inode(v)
        inode_raw = v
        break
    if inode is None:
        raise RuntimeError("%s: object %d has no inode record" % (path, oid))

    dec = read_xattr(fs, oid, "com.apple.decmpfs")
    compressed = None
    if dec is not None:
        if dec["kind"] != "inline" or len(dec["data"]) < 16:
            raise RuntimeError("%s: malformed decmpfs xattr" % path)
        magic, ctype, usize = struct.unpack("<IIQ", dec["data"][:16])
        if magic != DECMPFS_MAGIC:
            raise RuntimeError("%s: bad decmpfs magic %#x" % (path, magic))
        compressed = {"type": ctype, "size": usize,
                      "attr": dec["data"][16:]}

    if compressed:
        ctype = compressed["type"]
        usize = compressed["size"]
        if ctype in (CMP_TYPE_ZLIB_XATTR, CMP_TYPE_LZVN_XATTR,
                     CMP_TYPE_LZFSE_XATTR, CMP_TYPE_RAW_XATTR,
                     CMP_TYPE_RAW_XATTR2):
            data = decompress_inline(ctype, compressed["attr"], usize)
        elif ctype in (CMP_TYPE_ZLIB_RSRC, CMP_TYPE_LZVN_RSRC,
                       CMP_TYPE_RAW_RSRC, CMP_TYPE_LZFSE_RSRC):
            rv = read_xattr(fs, oid, "com.apple.ResourceFork")
            if rv is None or rv["kind"] != "stream":
                raise RuntimeError("%s: resource fork xattr is missing" % path)
            rsrc, extents = read_stream(fs, rv["oid"], rv["size"], verbose)
            if len(rsrc) != rv["size"]:
                raise RuntimeError("%s: resource fork is %d bytes, expected %d"
                                   % (path, len(rsrc), rv["size"]))
            data = decompress_resource(ctype, rsrc, usize)
            compressed["rsrc"] = rv
            compressed["extents"] = extents
        else:
            raise RuntimeError("%s: unsupported decmpfs type %d"
                               % (path, ctype))
        if len(data) != usize:
            raise RuntimeError("%s: decoded %d bytes, decmpfs says %d"
                               % (path, len(data), usize))
        size = usize
    else:
        size = inode["size"]
        if size == 0:
            ds = find_dstream(inode_raw)
            if ds is not None:
                size = ds["size"]
                if verbose:
                    log("    dstream fallback at inode offset %d: size=%d "
                        "alloced=%d" % (ds["offset"], ds["size"],
                                        ds["alloced_size"]))
        data, extents = read_stream(fs, oid, size, verbose)
        compressed = {"type": None, "size": size, "extents": extents}

    info = macho_info(data)
    result = {
        "path": path,
        "inode": oid,
        "mode": "0%o" % inode["mode"],
        "bsd_flags": "0x%x" % inode["bsd_flags"],
        "compressed": compressed["type"] is not None,
        "compression_type": compressed["type"],
        "declared_size": compressed["size"],
        "inode_size": inode["size"],
        "output_size": len(data),
        "size_ok": len(data) == compressed["size"],
        "sha256": hashlib.sha256(data).hexdigest(),
        "macho": info,
        "strings": proof_strings(data, path),
        "data": data,
    }
    if compressed.get("extents"):
        result["extents"] = [
            {k: e[k] for k in ("logical", "length", "paddr", "read")
             if k in e}
            for e in compressed["extents"]]
    return result


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
def human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return "%.1f %s" % (n, unit) if unit != "B" else "%d B" % n
        n /= 1024.0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="extract files from an iOS APFS root DMG")
    ap.add_argument("--url", help="IPSW URL")
    ap.add_argument("--ipsw", help="local IPSW")
    ap.add_argument("--dmg", help="DMG member (default: largest)")
    ap.add_argument("--get", action="append", default=[],
                    help="absolute path to extract (repeatable)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--json", action="store_true", help="print a JSON report")
    args = ap.parse_args(argv)
    if not args.url and not args.ipsw:
        ap.error("give --url or --ipsw")
    if not args.get:
        ap.error("give at least one --get")

    probe = load_probe()
    ext = probe.load_extractor()
    reader, base = probe.open_reader(ext, args)
    fs = probe.APFS(reader, base)
    fs.open_container()
    fs.open_volume()
    tree = FSTree(fs)
    if args.verbose:
        log("volume %r, root_tree_oid=%d, block_size=%d"
            % (fs.volume["name"], fs.volume["root_tree_oid"], fs.bs))

    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    results = []
    for path in args.get:
        if args.verbose:
            log("== %s" % path)
        r = extract(tree, path, args.verbose)
        dest = outdir / path.lstrip("/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(r["data"])
        r["dest"] = str(dest)
        results.append(r)
        if not args.json:
            print("%s" % path)
            print("  inode=%d mode=%s bsd=%s compressed=%s type=%s"
                  % (r["inode"], r["mode"], r["bsd_flags"],
                     r["compressed"], r["compression_type"]))
            print("  size=%d declared=%d ok=%s sha256=%s"
                  % (r["output_size"], r["declared_size"], r["size_ok"],
                     r["sha256"]))
            if r["macho"]:
                m = r["macho"]
                print("  %s filetype=%s ncmds=%s uuid=%s"
                      % (m["format"], m["filetype_name"], m["ncmds"],
                         m["uuid"]))
                if m["build_version"]:
                    b = m["build_version"]
                    print("  LC_BUILD_VERSION platform=%d minos=%s sdk=%s"
                          % (b["platform"], b["minos"], b["sdk"]))
            if r["strings"]:
                print("  strings=%r" % r["strings"][:3])
            print("  wrote %s" % dest)

    src = reader.zipf.src if hasattr(reader, "zipf") else reader
    stats = {
        "requests": getattr(src, "requests", None),
        "bytes_fetched": getattr(src, "bytes_fetched", None),
        "dmg_chunks": getattr(reader, "chunks_read", None),
        "dmg_chunk_bytes": getattr(reader, "fetched", None),
    }
    if args.json:
        print(json.dumps({"files": [{k: v for k, v in r.items() if k != "data"}
                                    for r in results], "stats": stats},
                         indent=2))
    else:
        print("range requests: %s" % stats["requests"])
        print("bytes fetched: %s (%s)" % (stats["bytes_fetched"],
                                          human(stats["bytes_fetched"] or 0)))
        print("DMG chunks: %s (%s compressed)"
              % (stats["dmg_chunks"], human(stats["dmg_chunk_bytes"] or 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
