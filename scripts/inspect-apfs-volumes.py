#!/usr/bin/env python3
"""Inventory checksum-valid volume superblocks in regular host APFS images."""
from pathlib import Path
import json
import struct
import sys
import uuid

root = Path(sys.argv[1])
report = {'evidence': 'host superblock inventory; not a filesystem mount', 'images': {}}


def checksum(block):
    first = second = 0
    for value, in struct.iter_unpack('<I', block[8:]):
        first = (first + value) % 0xffffffff
        second = (second + first) % 0xffffffff
    low = 0xffffffff - (first + second) % 0xffffffff
    high = 0xffffffff - (first + low) % 0xffffffff
    return low | high << 32


for label in ('OS', 'SystemOS', 'AppOS'):
    path = root / (label + '.apfs.raw')
    assert path.is_file()
    volumes = {}
    with path.open('rb') as f:
        nx = f.read(4096)
        assert nx[32:36] == b'NXSB'
        fs_oids = set(struct.unpack_from('<100Q', nx, 0xb8)) - {0}
        f.seek(0)
        base = 0
        while block := f.read(16 * 1024 * 1024):
            at = block.find(b'APSB')
            while at >= 0:
                start = at - 32
                if start >= 0 and (base + start) % 4096 == 0:
                    sb = block[start:start+4096]
                    if len(sb) == 4096 and struct.unpack_from('<Q', sb)[0] == checksum(sb):
                        oid, xid = struct.unpack_from('<QQ', sb, 8)
                        volume_uuid = str(uuid.UUID(bytes=sb[0xf0:0x100]))
                        name = sb[0x2c0:0x3c0].split(b'\0', 1)[0].decode('utf-8', 'replace')
                        role = struct.unpack_from('<H', sb, 0x3c4)[0]
                        item = dict(volume_uuid=volume_uuid, name=name, role=hex(role),
                                    oid=hex(oid), xid=xid, physical_block=(base+start)//4096,
                                    listed_in_nx_fs_oids=oid in fs_oids)
                        if volume_uuid not in volumes or xid > volumes[volume_uuid]['xid']:
                            volumes[volume_uuid] = item
                at = block.find(b'APSB', at + 4)
            base += len(block)
    assert volumes, 'No checksum-valid volume superblock in ' + label
    report['images'][label] = dict(nx_fs_oids=[hex(x) for x in sorted(fs_oids)],
                                  volumes=list(volumes.values()))
    print(label, json.dumps(report['images'][label], ensure_ascii=False), flush=True)
(root / 'volumes.json').write_text(json.dumps(report, indent=2) + '\n')
