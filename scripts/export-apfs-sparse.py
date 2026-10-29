#!/usr/bin/env python3
"""Export a detached sparse raw file as exact Android sparse RAW/zero-FILL.

Uses SEEK_DATA/SEEK_HOLE to avoid reading hundreds of GB of host-file holes.
Unlike DONT_CARE, FILL explicitly writes zeros. Verification compares every
allocated byte and proves every skipped host range is a filesystem hole.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import struct
import uuid

BLOCK = 4096
MAGIC, RAW, FILL = 0xED26FF3A, 0xCAC1, 0xCAC2
MAX_RAW = 16 << 20


def extents(fd, size):
    pos = 0
    while pos < size:
        try:
            start = os.lseek(fd, pos, os.SEEK_DATA)
        except OSError as e:
            if e.errno == errno.ENXIO:
                return
            raise
        end = min(os.lseek(fd, start, os.SEEK_HOLE), size)
        if not (pos <= start < end <= size and start % BLOCK == end % BLOCK == 0):
            raise ValueError('Unaligned or invalid allocated extent')
        yield start, end
        pos = end


def checksum(b):
    words = struct.unpack('<1022I', b[8:])
    a = c = 0
    for w in words:
        a = (a + w) % 0xffffffff
        c = (c + a) % 0xffffffff
    low = (0xffffffff - (a + c) % 0xffffffff) % 0xffffffff
    high = (0xffffffff - (a + low) % 0xffffffff) % 0xffffffff
    return (high << 32) | low


def inspect_blocks(data, offset, found):
    # Only checksum candidate superblocks, not every APFS block.
    for i in range(0, len(data), BLOCK):
        b = data[i:i + BLOCK]
        magic = b[32:36]
        if magic not in (b'NXSB', b'APSB') or checksum(b) != struct.unpack_from('<Q', b)[0]:
            continue
        oid, xid = struct.unpack_from('<QQ', b, 8)
        if magic == b'NXSB':
            key = 'container:' + str(uuid.UUID(bytes=b[72:88]))
            item = dict(kind='NXSB', block_size=struct.unpack_from('<I', b, 36)[0],
                        block_count=struct.unpack_from('<Q', b, 40)[0],
                        features=list(struct.unpack_from('<QQQ', b, 48)))
        else:
            key = 'volume:' + str(uuid.UUID(bytes=b[0xf0:0x100]))
            item = dict(kind='APSB', features=list(struct.unpack_from('<QQQ', b, 0x28)),
                        role=struct.unpack_from('<H', b, 0x3c4)[0],
                        name=b[0x2c0:0x3c0].split(b'\0', 1)[0].decode('utf-8', 'replace'))
        item.update(offset=offset+i, oid=oid, xid=xid)
        if key not in found or xid > found[key]['xid']:
            found[key] = item


def verify(source, target, ranges):
    with source.open('rb') as src, target.open('rb') as image:
        header = image.read(28)
        magic, major, minor, fhs, chs, block, blocks, chunks, crc = struct.unpack('<IHHHHIIII', header)
        assert (magic, major, minor, fhs, chs, block, crc) == (MAGIC, 1, 0, 28, 12, BLOCK, 0)
        assert blocks * BLOCK == source.stat().st_size
        position = 0
        for _ in range(chunks):
            kind, reserved, count, length = struct.unpack('<HHII', image.read(12))
            assert reserved == 0 and count > 0
            nbytes = count * BLOCK
            if kind == RAW:
                assert length == nbytes + 12 and nbytes <= MAX_RAW
                src.seek(position)
                left = nbytes
                while left:
                    data = image.read(min(left, 1 << 20))
                    assert data and data == src.read(len(data))
                    left -= len(data)
            elif kind == FILL:
                assert length == 16 and image.read(4) == bytes(4)
                # Holes read as zeros by the host filesystem contract. Any
                # allocated overlap must actually contain only zeros.
                for begin, end in ranges:
                    lo, hi = max(begin, position), min(end, position+nbytes)
                    if lo < hi:
                        src.seek(lo)
                        left = hi-lo
                        while left:
                            data = src.read(min(left, 1 << 20))
                            assert data and not any(data)
                            left -= len(data)
            else:
                raise ValueError(f'Unexpected chunk type {kind:#x}')
            position += nbytes
            assert position <= blocks * BLOCK
        assert position == blocks * BLOCK and not image.read(1)


def export(source, target):
    assert source.resolve() != target.resolve() and not target.exists()
    before = source.stat()
    assert before.st_size > 0 and before.st_size % BLOCK == 0
    with source.open('rb') as src:
        ranges = list(extents(src.fileno(), before.st_size))
        allocated = sum(end-start for start, end in ranges)
        assert allocated < 12 << 30, 'Unexpected allocation; inspect before exporting'
        chunks = raw_bytes = fill_bytes = 0
        position = 0
        found = {}
        with target.open('xb') as out:
            out.write(bytes(28))

            def fill(nbytes):
                nonlocal chunks, fill_bytes
                if nbytes:
                    out.write(struct.pack('<HHII', FILL, 0, nbytes//BLOCK, 16) + bytes(4))
                    chunks += 1
                    fill_bytes += nbytes

            for start, end in ranges:
                fill(start-position)
                src.seek(start)
                position = start
                while position < end:
                    data = src.read(min(MAX_RAW, end-position))
                    assert len(data) == min(MAX_RAW, end-position)
                    inspect_blocks(data, position, found)
                    if data == bytes(len(data)):
                        fill(len(data))
                    else:
                        out.write(struct.pack('<HHII', RAW, 0, len(data)//BLOCK, len(data)+12))
                        out.write(data)
                        chunks += 1
                        raw_bytes += len(data)
                    position += len(data)
                print(f'exported extent {start}..{end}', flush=True)
            fill(before.st_size-position)
            out.seek(0)
            out.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLOCK,
                                  before.st_size//BLOCK, chunks, 0))
    assert source.stat().st_mtime_ns == before.st_mtime_ns
    verify(source, target, ranges)
    digest = hashlib.sha256()
    with target.open('rb') as f:
        for b in iter(lambda: f.read(8 << 20), b''):
            digest.update(b)
    report = dict(source=str(source), target=str(target), logical_bytes=before.st_size,
                  allocated_ranges=ranges, raw_bytes=raw_bytes, zero_fill_bytes=fill_bytes,
                  chunks=chunks, sparse_bytes=target.stat().st_size, sparse_sha256=digest.hexdigest(),
                  exact_allocated_comparison=True, holes_explicit_zero_fill=True,
                  superblocks=found, device_flash=False)
    target.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    args = parser.parse_args()
    export(args.source, args.target)
