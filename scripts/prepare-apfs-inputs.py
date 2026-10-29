#!/usr/bin/env python3
"""Prepare regular host files from this IPSW. Never opens a device or flashes."""
import argparse
import bz2
import hashlib
import json
import lzma
from pathlib import Path
import plistlib
import struct
import uuid
import zipfile
import zlib
import lzfse

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('ipsw', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
assert a.ipsw.is_file()
a.output.mkdir(parents=True, exist_ok=True)


def nx_info(path):
    with path.open('rb') as f:
        block = f.read(4096)
    assert block[32:36] == b'NXSB', path
    block_size, count = struct.unpack_from('<IQ', block, 36)
    assert block_size == 4096 and count * block_size == path.stat().st_size
    first = second = 0
    for value, in struct.iter_unpack('<I', block[8:]):
        first = (first + value) % 0xffffffff
        second = (second + first) % 0xffffffff
    low = 0xffffffff - (first + second) % 0xffffffff
    high = 0xffffffff - (first + low) % 0xffffffff
    assert struct.unpack_from('<Q', block)[0] == low | high << 32, 'NXSB checksum'
    return dict(block_size=block_size, block_count=count,
                container_uuid=str(uuid.UUID(bytes=block[72:88])),
                nxsb_checksum='PASS', volume_uuid=None,
                features=[hex(x) for x in struct.unpack_from('<3Q', block, 48)])


def finish(path, expected, digest):
    assert path.stat().st_size == expected
    metadata = nx_info(path)
    final = path.with_suffix('')
    assert not final.exists(), final
    path.rename(final)
    print(final.name, expected, digest.hexdigest(), metadata, flush=True)
    return dict(file=final.name, bytes=expected, sha256=digest.hexdigest(), **metadata)


report = dict(product_version=None, build=None, device_class='d22ap',
              evidence='host extraction and NXSB checks only; not a device mount', images={})
with zipfile.ZipFile(a.ipsw) as z, a.ipsw.open('rb') as raw:
    manifest_data = z.read('BuildManifest.plist')
    manifest = plistlib.loads(manifest_data)
    assert manifest['ProductVersion'] == '16.7.16' and manifest['ProductBuildVersion'] == '20H392'
    report['product_version'], report['build'] = '16.7.16', '20H392'
    identity, = [x for x in manifest['BuildIdentities'] if x['Info']['DeviceClass'] == 'd22ap'
                 and x['Info']['RestoreBehavior'] == 'Erase']
    with (a.output / 'BuildManifest.plist').open('xb') as f:
        f.write(manifest_data)

    # OS is a ZIP_STORED UDIF with a 512-byte-sector GPT wrapper. Decode only
    # its Apple_APFS partition, so the output begins at NXSB and is 4K aligned.
    os_name = identity['Manifest']['OS']['Info']['Path']
    info = z.getinfo(os_name)
    assert info.compress_type == zipfile.ZIP_STORED
    raw.seek(info.header_offset)
    header = raw.read(30)
    assert header[:4] == b'PK\x03\x04'
    name_len, extra_len = struct.unpack_from('<HH', header, 26)
    base = info.header_offset + 30 + name_len + extra_len

    def dmg_read(offset, size):
        assert offset >= 0 and size >= 0 and offset + size <= info.file_size
        raw.seek(base + offset)
        value = raw.read(size)
        assert len(value) == size
        return value

    footer = dmg_read(info.file_size - 512, 512)
    assert footer[:4] == b'koly'
    data_fork, data_length = struct.unpack_from('>QQ', footer, 24)
    xml_offset, xml_length = struct.unpack_from('>QQ', footer, 216)
    metadata = plistlib.loads(dmg_read(xml_offset, xml_length))
    entry, = [x for x in metadata['resource-fork']['blkx'] if x['Name'] == 'Apple_APFS']
    table = entry['Data']
    assert table[:4] == b'mish'
    start_sector, sectors, data_offset = struct.unpack_from('>3Q', table, 8)
    chunks, = struct.unpack_from('>I', table, 200)
    assert len(table) == 204 + chunks * 40
    expected = sectors * 512
    output = a.output / 'OS.apfs.raw.partial'
    digest = hashlib.sha256()
    position = 0
    with output.open('xb') as f:
        for i in range(chunks):
            kind, _, sector, length, compressed, compressed_length = struct.unpack_from('>II4Q', table, 204 + i * 40)
            if kind in (0x7ffffffe, 0xffffffff):
                continue
            assert sector * 512 == position and (sector + length) <= sectors
            size = length * 512
            if kind in (0, 2):
                # A sparse extent can span gigabytes; keep memory bounded.
                zeros = bytes(1024 * 1024)
                remaining = size
                while remaining:
                    part = zeros[:min(remaining, len(zeros))]
                    f.write(part)
                    digest.update(part)
                    remaining -= len(part)
                position += size
                continue
            else:
                # The source has literal runs up to 70 MiB; compressed
                # chunks remain capped separately before decoding.
                assert size <= (128 if kind == 1 else 16) * 1024 * 1024
                assert compressed + compressed_length <= data_length - data_offset
                blob = dmg_read(data_fork + data_offset + compressed, compressed_length)
                if kind == 1:
                    decoded = blob
                elif kind == 0x80000005:
                    decoded = zlib.decompress(blob)
                elif kind == 0x80000006:
                    decoded = bz2.decompress(blob)
                elif kind == 0x80000007:
                    decoded = lzfse.decompress(blob)
                elif kind == 0x80000008:
                    decoded = lzma.decompress(blob)
                else:
                    raise ValueError('Unsupported UDIF chunk ' + hex(kind))
            assert len(decoded) == size
            f.write(decoded)
            digest.update(decoded)
            position += size
            if i % 400 == 0:
                print('OS decode', i, '/', chunks, position, '/', expected, flush=True)
    assert position == expected
    report['images']['OS'] = finish(output, expected, digest)
    report['images']['OS']['udif_partition_start_512'] = start_sector
    report['images']['OS']['source'] = os_name

    for key, label in (('Cryptex1,SystemOS', 'SystemOS'), ('Cryptex1,AppOS', 'AppOS')):
        name = identity['Manifest'][key]['Info']['Path']
        info = z.getinfo(name)
        output = a.output / (label + '.apfs.raw.partial')
        digest = hashlib.sha256()
        with z.open(name) as source, output.open('xb') as dest:
            while block := source.read(4 * 1024 * 1024):
                dest.write(block)
                digest.update(block)
        report['images'][label] = finish(output, info.file_size, digest)
        report['images'][label]['source'] = name

    for key in ('StaticTrustCache', 'Cryptex1,SystemTrustCache', 'Cryptex1,AppTrustCache',
                'Cryptex1,SystemVolume', 'Cryptex1,AppVolume'):
        name = identity['Manifest'][key]['Info']['Path']
        content = z.read(name)
        with (a.output / Path(name).name).open('xb') as f:
            f.write(content)
    with (a.output / 'inputs.json').open('x') as f:
        json.dump(report, f, indent=2)
        f.write('\n')
print('Host APFS input preparation complete. No device writes.', flush=True)
