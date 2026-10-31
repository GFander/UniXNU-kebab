#!/usr/bin/env python3
"""Check the opt-in UEFI development identity emitted by the ADT generator."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def parse(data, at=0):
    count, nchildren = struct.unpack_from('<II', data, at)
    at += 8
    props = {}
    for _ in range(count):
        name, size = struct.unpack_from('<32sI', data, at)
        at += 36
        props[name.rstrip(b'\0').decode()] = data[at:at + size]
        at += (size + 3) & ~3
    children = {}
    for _ in range(nchildren):
        node, at = parse(data, at)
        children[node[0].get('name', b'').rstrip(b'\0').decode()] = node
    return (props, children), at


generator = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name('make-kebab-adt.py')
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kebab-adt-test-', dir=cache) as directory:
    owner = Path(directory) / 'owner.json'
    owner.write_bytes(b'{"domain":"UniXNU.Test.OwnerBootSet.v1","components":{}}')
    for enabled, measured in ((False, False), (True, False), (True, True)):
        out = Path(directory) / f'boot-{enabled}-{measured}.adt'
        command = [sys.executable, str(generator), '--dram-size', '0x300000000',
                   '--iphone-identity', '--unique-chip-id', '1', '--chip-id', '8250',
                   '--output', str(out)]
        subprocess.run(command + (['--development-boot'] if enabled else []) +
                       (['--owner-boot-manifest', str(owner)] if measured else []), check=True,
                       stdout=subprocess.DEVNULL)
        data = out.read_bytes()
        tree, end = parse(data)
        assert end == len(data)
        chosen = tree[1]['chosen']
        manifest = chosen[1]['manifest-properties'][0]
        if enabled:
            assert chosen[0]['mix-n-match-prevention-status'] == struct.pack('<I', 0)
            nonce = chosen[0]['boot-nonce']
            assert len(nonce) == 32
            assert manifest['BNCH'] == hashlib.sha384(nonce).digest()
        else:
            assert 'mix-n-match-prevention-status' not in chosen[0]
            assert 'boot-nonce' not in chosen[0] and 'BNCH' not in manifest
        if measured:
            assert chosen[0]['owner-boot-manifest'] == owner.read_bytes()
            assert chosen[0]['owner-boot-manifest-domain'] == b'UniXNU.Test.OwnerBootSet.v1\0'
            assert chosen[0]['boot-manifest-hash'] == hashlib.sha384(owner.read_bytes()).digest()
        else:
            assert 'boot-manifest-hash' not in chosen[0]
    invalid = subprocess.run(command + ['--owner-boot-manifest', str(owner)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert invalid.returncode != 0
print('default identity unchanged; development policy and nonce hash: PASS')
