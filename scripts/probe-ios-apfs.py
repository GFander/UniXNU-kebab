#!/usr/bin/env python3
"""Read APFS inode records out of an IPSW's root DMG, over range requests.

This implements the B-tree layout the earlier probe got wrong:

    key   = 56 + table_space.off + table_space.len + koff
    value = block_size - (40 if ROOT else 0) - voff

Only the *root* node carries the 40-byte `btree_info_t` footer; non-root nodes
store values right up to the end of the block.  Reading `block_size - voff` on
a root node reads the footer, and reading `block_size - 40 - voff` on a
non-root node is 40 bytes off.

With those two rules the chain is:

    container superblock
      -> container omap -> nx_fs_oid[0] -> volume superblock (APSB)
      -> volume omap    -> apfs_root_tree_oid -> fs-tree root
      -> fs-tree        -> j_inode_val_t (mode, uid/gid, uncompressed size)

Examples:
    python3 scripts/probe-ios-apfs.py --url "$URL" \
        --inode 1152921500312216349 --inode 1152921500312216239

    # short form for the two P1 entry points
    python3 scripts/probe-ios-apfs.py --url "$URL" --known
"""
import argparse
import importlib.util
import struct
import sys
from pathlib import Path

DYLD = 1152921500312216349
LAUNCHD = 1152921500312216239
INSIDE = 0x0fffffffffffffff


def log(msg):
    print(msg, file=sys.stderr)


def load_extractor():
    path = Path(__file__).with_name("extract-ios-userland.py")
    spec = importlib.util.spec_from_file_location("extract_ios_userland", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def open_reader(ext, args):
    src = ext.open_source(args.url or args.ipsw)
    zipf = ext.RemoteZip(src)
    entry = zipf.find(args.dmg) if args.dmg else zipf.largest_dmg()
    if entry is None:
        raise SystemExit("no .dmg member found")
    start = zipf.data_offset(entry)
    koly, endian = ext.parse_koly(
        src.read_at(start + entry.usize - 512, 512), entry.usize)
    plist = ext.plistlib.loads(src.read_at(
        start + koly["data_fork_offset"] + koly["xml_offset"],
        koly["xml_length"]))
    tables = [(i["Name"], ext.parse_blkx(i["Data"], endian))
              for i in plist["resource-fork"]["blkx"]]
    codec = ext.sniff_codec(src.read_at(start + koly["data_fork_offset"], 64))
    base = 34 * 512
    for name, table in tables:
        if name == "Apple_APFS":
            base = table["first_sector"] * 512
            break
    return ext.DmgReader(zipf, entry, koly, tables, codec), base


class APFS:
    def __init__(self, reader, base):
        self.r = reader
        self.base = base
        self.bs = 4096
        self.container = None
        self.volume = None

    def block(self, paddr):
        return self.r.read_at(self.base + paddr * self.bs, self.bs)

    # -- btree node ------------------------------------------------------
    def header(self, b):
        flags, level = struct.unpack("<HH", b[32:36])
        nkeys = struct.unpack("<I", b[36:40])[0]
        toff, tlen = struct.unpack("<HH", b[40:44])
        return flags, level, nkeys, toff, tlen

    def entries(self, b, fixed=False, klen=16, vlen_leaf=16, vlen_inner=8):
        flags, level, nkeys, toff, tlen = self.header(b)
        toc = 56 + toff
        keybase = toc + tlen
        end = self.bs - (40 if flags & 1 else 0)
        out = []
        for i in range(nkeys):
            if fixed:
                koff, voff = struct.unpack(
                    "<HH", b[toc + i * 4:toc + i * 4 + 4])
                kl = klen
                vl = vlen_leaf if level == 0 else vlen_inner
            else:
                koff, kl, voff, vl = struct.unpack(
                    "<HHHH", b[toc + i * 8:toc + i * 8 + 8])
            key = b[keybase + koff:keybase + koff + kl]
            val = b[end - voff:end - voff + vl]
            out.append((key, val))
        return flags, level, out

    # -- object maps -----------------------------------------------------
    def omap_lookup(self, tree_paddr, oid, max_xid=0xFFFFFFFFFFFFFFFF):
        node = tree_paddr
        while True:
            flags, level, ents = self.entries(self.block(node), fixed=True)
            if level == 0:
                best = None
                for key, val in ents:
                    o, x = struct.unpack("<QQ", key)
                    if o == oid and x <= max_xid:
                        if best is None or x > best[0]:
                            best = (x, struct.unpack("<IIQ", val))
                return best
            chosen = None
            for key, val in ents:
                o, x = struct.unpack("<QQ", key)
                if (o, x) <= (oid, max_xid):
                    chosen = struct.unpack("<Q", val)[0]
            if chosen is None:
                return None
            node = chosen

    def omap_tree(self, omap_paddr):
        omap = self.block(omap_paddr)
        typ = struct.unpack("<I", omap[24:28])[0]
        if typ & 0xFFFF != 0x0B:
            raise SystemExit("block %d is not an omap (type 0x%x)"
                             % (omap_paddr, typ))
        return struct.unpack("<Q", omap[48:56])[0]

    def open_container(self):
        nx = self.block(0)
        if nx[32:36] != b"NXSB":
            raise SystemExit("partition does not start with NXSB")
        self.bs, blocks = struct.unpack("<IQ", nx[36:48])
        omap_oid = struct.unpack("<Q", nx[160:168])[0]
        max_fs = struct.unpack("<I", nx[180:184])[0]
        fs_oids = [struct.unpack("<Q", nx[184 + i * 8:192 + i * 8])[0]
                   for i in range(min(max_fs, 8))]
        self.container = {"block_size": self.bs, "blocks": blocks,
                          "omap_oid": omap_oid, "fs_oids": fs_oids}
        tree = self.omap_tree(omap_oid)
        res = self.omap_lookup(tree, fs_oids[0])
        if not res:
            raise SystemExit("container omap has no entry for %d" % fs_oids[0])
        _xid, (_flags, _size, paddr) = res
        self.volume_paddr = paddr

    def open_volume(self):
        vol = self.block(self.volume_paddr)
        if vol[32:36] != b"APSB":
            raise SystemExit("block %d is not APSB" % self.volume_paddr)
        u64 = lambda off: struct.unpack("<Q", vol[off:off + 8])[0]
        u32 = lambda off: struct.unpack("<I", vol[off:off + 4])[0]
        self.volume = {
            "paddr": self.volume_paddr,
            "name": vol[704:960].split(b"\0")[0].decode("utf-8", "replace"),
            "files": u64(184), "dirs": u64(192), "symlinks": u64(200),
            "uuid": vol[240:256].hex(),
            "omap_block": u64(128), "root_tree_oid": u64(136),
        }
        self.omap_tree_paddr = self.omap_tree(self.volume["omap_block"])
        res = self.omap_lookup(self.omap_tree_paddr,
                               self.volume["root_tree_oid"])
        if not res:
            raise SystemExit("volume omap has no entry for root_tree_oid %d"
                             % self.volume["root_tree_oid"])
        self.fs_root_paddr = res[1][2]

    # -- file-system tree ------------------------------------------------
    @staticmethod
    def jkey(key):
        v = struct.unpack("<Q", key[:8])[0]
        return v & INSIDE, v >> 60

    def fs_lookup(self, oid, typ=3):
        node = self.volume["root_tree_oid"]
        path = []
        target = (oid, typ)
        while True:
            res = self.omap_lookup(self.omap_tree_paddr, node)
            if not res:
                return None, path
            paddr = res[1][2]
            b = self.block(paddr)
            flags, level, ents = self.entries(b, fixed=False)
            path.append((node, paddr, level, len(ents)))
            chosen = None
            for key, val in ents:
                if self.jkey(key) <= target:
                    chosen = (key, val)
            if level == 0:
                for key, val in ents:
                    if self.jkey(key) == target:
                        return val, path
                return None, path
            if chosen is None:
                return None, path
            node = struct.unpack("<Q", chosen[1])[0]


def decode_inode(val):
    if len(val) < 92:
        return None
    parent, private = struct.unpack("<QQ", val[0:16])
    ctime, mtime, chg, atime = struct.unpack("<QQQQ", val[16:48])
    iflags, nlink = struct.unpack("<Qi", val[48:60])
    prot, wgen, bsd, owner, group, mode, pad, size = struct.unpack(
        "<IIIIIHHQ", val[60:92])
    name = None
    if len(val) > 92:
        num, used = struct.unpack("<HH", val[92:96])
        pos = 96
        for _ in range(num):
            xt, xf, xs = struct.unpack("<BBH", val[pos:pos + 4])
            pos += 4
            data = val[pos:pos + xs]
            pos += xs
            if xt == 4:
                name = data.split(b"\0")[0].decode("utf-8", "replace")
    return {"parent": parent, "private": private, "mode": mode,
            "owner": owner, "group": group, "nlink": nlink,
            "size": size, "name": name, "flags": iflags,
            "mtime": mtime, "change": chg}


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="read APFS inode records from an IPSW root DMG")
    ap.add_argument("--url", help="IPSW URL")
    ap.add_argument("--ipsw", help="local IPSW")
    ap.add_argument("--dmg", help="DMG member name (default: largest)")
    ap.add_argument("--inode", type=lambda s: int(s, 0), action="append",
                    default=[], help="inode/object id to read (repeatable)")
    ap.add_argument("--known", action="store_true",
                    help="read /usr/lib/dyld and /sbin/launchd")
    args = ap.parse_args(argv)
    if not args.url and not args.ipsw:
        ap.error("give --url or --ipsw")
    if args.known:
        args.inode += [DYLD, LAUNCHD]
    if not args.inode:
        ap.error("give --inode or --known")

    ext = load_extractor()
    reader, base = open_reader(ext, args)
    fs = APFS(reader, base)
    fs.open_container()
    c = fs.container
    print("container: block_size=%d blocks=%d omap=%d fs_oids=%s"
          % (c["block_size"], c["blocks"], c["omap_oid"], c["fs_oids"]))
    fs.open_volume()
    v = fs.volume
    print("volume: %r files=%d dirs=%d symlinks=%d uuid=%s"
          % (v["name"], v["files"], v["dirs"], v["symlinks"], v["uuid"]))
    print("volume omap block=%d tree=%d root_tree_oid=%d -> paddr=%d"
          % (v["omap_block"], fs.omap_tree_paddr, v["root_tree_oid"],
             fs.fs_root_paddr))

    rc = 0
    for oid in args.inode:
        rec, path = fs.fs_lookup(oid)
        print("\ninode %d" % oid)
        for node, paddr, level, nkeys in path:
            print("  fs-tree oid=%-6d level=%d paddr=%-7d nkeys=%d"
                  % (node, level, paddr, nkeys))
        if rec is None:
            print("  NOT FOUND")
            rc = 1
            continue
        ino = decode_inode(rec)
        print("  parent=%d private=%d mode=0%o uid=%d gid=%d nlink=%d "
              "size=%d name=%r"
              % (ino["parent"], ino["private"], ino["mode"], ino["owner"],
                 ino["group"], ino["nlink"], ino["size"], ino["name"]))
    return rc


if __name__ == "__main__":
    sys.exit(main())
