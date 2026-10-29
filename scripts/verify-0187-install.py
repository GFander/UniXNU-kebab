#!/usr/bin/env python3
"""Read back the prepared boot payload and sparse userdata; never touch a device."""
import ast
import argparse
import copy
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version', default='0187')
parser.add_argument('--boot-only', action='store_true')
args = parser.parse_args()
assert args.version.isdigit() and len(args.version) == 4
version = args.version
sys.path.insert(0, str(root.parent))
from verify_image_lib import Fat32

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(8 << 20), b''):
            h.update(b)
    return h.hexdigest()

# Reuse only the ADT codec definitions; do not run the modifying CLI.
tree = ast.parse((root / 'refresh_owner_manifest.py').read_text())
codec = ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
                         and n.name in ('parse', 'encode')], type_ignores=[])
exec(compile(codec, '<ADT codec>', 'exec'))

boot_path = root / f'boot-{version}-ro.img'
payload_path = root / f'payload-{version}-ro.img'
boot, payload = boot_path.read_bytes(), payload_path.read_bytes()
base_boot = (root.parents[1] / 'signed.img').read_bytes()
firmware = base_boot[:0x800000]
assert len(boot) == 100663296 and boot[:0x800000] == firmware
assert boot[0x800000:0x800000+len(payload)] == payload
assert boot[0x800000+len(payload):] == base_boot[0x800000+len(payload):]
fs = Fat32(payload)
files = {name: data for name, data, _, _ in fs.walk(fs.root)}
assert files['EFI/UniXNU/Payloads/kernel.macho'] == (root / f'prelink-{version}.bin').read_bytes()
config = files['EFI/UniXNU/unixnu.toml']
assert config == (root / f'unixnu-{version}-ro.toml').read_bytes()
assert all(flag in config.decode().split() for flag in ('ufsmedia_root=2', 'ufsrw=0', 'rootrw=0'))
adt = files['EFI/UniXNU/Payloads/kernel.macho.adt']
node, end = parse(adt)
assert end == len(adt) and encode(node) == adt
chosen = next(c for c in node[1] if dict(c[0]).get('name') == b'chosen\0')
props = dict(chosen[0])
matcher = ET.fromstring(props['root-matching'].rstrip(b'\0'))
dictionary = plistlib.loads(b'<plist version="1.0">'+ET.tostring(matcher)+b'</plist>')
assert dictionary == dict(IOProviderClass='IOMedia', IOPropertyMatch={
    'UUID': 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7'})
manifest = json.loads(props['owner-boot-manifest'])
assert hashlib.sha384(props['owner-boot-manifest']).digest() == props['boot-manifest-hash']
assert manifest['components']['kernel'] == hashlib.sha256(files['EFI/UniXNU/Payloads/kernel.macho']).hexdigest()
assert manifest['components']['boot-config'] == hashlib.sha256(config).hexdigest()
assert manifest['components']['firmware-prefix'] == hashlib.sha256(firmware).hexdigest()
measured = copy.deepcopy(node)
mc = next(c for c in measured[1] if dict(c[0]).get('name') == b'chosen\0')
mc[0][:] = [(k, v) for k, v in mc[0] if k not in ('owner-boot-manifest', 'boot-manifest-hash')]
assert manifest['components']['adt-without-owner-manifest'] == hashlib.sha256(encode(measured)).hexdigest()
sidecar = files.get('EFI/UniXNU/Payloads/kernel.macho.trustcache')
if sidecar is not None:
    assert manifest['components']['static-trust-cache-bundle'] == hashlib.sha256(sidecar).hexdigest()
    assert struct.unpack_from('<III',sidecar) == (1,8,2)
    count = struct.unpack_from('<I',sidecar,28)[0]
    assert count == 2760 and len(sidecar) == 8+24+24*count
    assert sidecar[12:28].hex() == '9f368a73d90b40ce943c827e13e72eee'
    memory, = [c for c in chosen[1] if dict(c[0]).get('name') == b'memory-map\0']
    assert dict(memory[0])['TrustCache'] == bytes(16)
if args.boot_only:
    print(json.dumps(dict(boot_file=str(boot_path), boot_sha256=sha(boot_path),
                          first_boot_storage_readonly=True, root_uuid=dictionary['IOPropertyMatch']['UUID']), indent=2))
    print('Firmware, FAT files, root UUID and owner measurements: PASS')
    sys.exit(0)

image_root = root.parents[1] / 'apfs-inputs-20H392-ready'
sparse_path = image_root / 'HiOS.userdata.sparse.img'
report = json.loads((image_root / 'HiOS.userdata.sparse.json').read_text())
assert sha(sparse_path) == report['sparse_sha256']
assert sparse_path.stat().st_size == report['sparse_bytes']
raw = fill = count = 0
with sparse_path.open('rb') as f:
    assert struct.unpack('<IHHHHIIII', f.read(28)) == (
        0xED26FF3A, 1, 0, 28, 12, 4096, 241008750592//4096, report['chunks'], 0)
    for _ in range(report['chunks']):
        kind, reserved, blocks, size = struct.unpack('<HHII', f.read(12))
        assert reserved == 0 and blocks > 0
        if kind == 0xCAC1:
            assert size == blocks*4096+12 and size <= (16 << 20)+12
            f.seek(blocks*4096, 1)
            raw += blocks*4096
        else:
            assert kind == 0xCAC2 and size == 16 and f.read(4) == bytes(4)
            fill += blocks*4096
        count += blocks
    assert f.tell() == sparse_path.stat().st_size and not f.read(1)
assert count*4096 == raw+fill == report['logical_bytes'] == 241008750592
assert (raw, fill) == (report['raw_bytes'], report['zero_fill_bytes'])
superblocks = report['superblocks']
nx = superblocks['container:0e25f331-6279-458a-9cfe-f39598da17aa']
assert nx['block_size']*nx['block_count'] == report['logical_bytes'] and nx['features'] == [0,0,2]
expected = [('dadd93f8-dbbf-4b17-a1df-0f11e64172f7',1,[18,0,8]),
            ('4c56203c-cfe8-4dff-bf8e-bb48d852c936',64,[2,0,8]),
            ('ef45f98e-9f6c-43db-b7c6-f418ca77429b',16,[2,0,8])]
for volume, role, features in expected:
    assert superblocks['volume:'+volume]['role'] == role
    assert superblocks['volume:'+volume]['features'] == features
out = dict(serial='c073a488', target='userdata', logical_bytes=report['logical_bytes'],
           sparse_file=str(sparse_path), sparse_sha256=report['sparse_sha256'],
           sparse_bytes=report['sparse_bytes'], boot_partition='boot_b',
           boot_file=str(boot_path), boot_sha256=sha(boot_path),
           kernel_sha256=sha(root/'kernel-0187.kebab'),
           root_uuid=expected[0][0].upper(), first_boot_storage_readonly=True,
           outer_gpt_changes=False, host_verification=True,
           device_flash=False, ios16_mount_verified=False)
(image_root/'install-0187.json').write_text(json.dumps(out, indent=2)+'\n')
print(json.dumps(out, indent=2))
print('Firmware, FAT files, root UUID, owner measurements and sparse structure/hash: PASS')
