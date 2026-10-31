from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import struct
import sys
import uuid

root = Path(__file__).parent
sys.path.insert(0, str(root.parent))
from verify_image_lib import Fat32

def parse(data, at=0):
    count, nchildren = struct.unpack_from('<II', data, at)
    at += 8
    props, children = [], []
    for _ in range(count):
        key, size = struct.unpack_from('<32sI', data, at)
        at += 36
        props.append((key.rstrip(b'\0').decode(), data[at:at+size]))
        at += (size+3) & ~3
    for _ in range(nchildren):
        child, at = parse(data, at)
        children.append(child)
    return (props, children), at

def encode(node):
    props, children = node
    out = struct.pack('<II', len(props), len(children))
    for key, value in props:
        out += struct.pack('<32sI', key.encode(), len(value)) + value + bytes(-len(value)%4)
    return out + b''.join(encode(child) for child in children)

source, destination = map(Path, sys.argv[1:3])
payload = source.read_bytes()
fs = Fat32(payload)
files = {n: d for n, d, _, _ in fs.walk(fs.root)}
adt_path = 'EFI/UniXNU/Payloads/kernel.macho.adt'
tree, end = parse(files[adt_path])
assert end == len(files[adt_path]) and encode(tree) == files[adt_path]
chosen = next(c for c in tree[1] if dict(c[0]).get('name') == b'chosen\0')
if len(sys.argv) > 3:
    volume_uuid = str(uuid.UUID(sys.argv[3])).upper()
    root_match = ('<dict><key>IOProviderClass</key><string>IOMedia</string>'
                  '<key>IOPropertyMatch</key><dict><key>UUID</key><string>' +
                  volume_uuid + '</string></dict></dict>\0').encode()
    chosen[0][:] = [(key, value) for key, value in chosen[0] if key != 'root-matching']
    chosen[0].append(('root-matching', root_match))
properties = dict(chosen[0])
manifest = json.loads(properties['owner-boot-manifest'])
manifest['components']['kernel'] = hashlib.sha256(files['EFI/UniXNU/Payloads/kernel.macho']).hexdigest()
manifest['components']['boot-config'] = hashlib.sha256(files['EFI/UniXNU/unixnu.toml']).hexdigest()
trust_sidecar = 'EFI/UniXNU/Payloads/kernel.macho.trustcache'
if trust_sidecar in files:
    manifest['components']['static-trust-cache-bundle'] = hashlib.sha256(files[trust_sidecar]).hexdigest()
# Include the actual current ADT while avoiding a self-referential manifest.
measured_tree = copy.deepcopy(tree)
measured_chosen = next(c for c in measured_tree[1] if dict(c[0]).get('name') == b'chosen\0')
measured_chosen[0][:] = [(key, value) for key, value in measured_chosen[0]
                        if key not in ('owner-boot-manifest', 'boot-manifest-hash')]
manifest['components']['adt-without-owner-manifest'] = hashlib.sha256(encode(measured_tree)).hexdigest()
blob = json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()
digest = hashlib.sha384(blob).digest()
chosen[0][:] = [(key, blob if key == 'owner-boot-manifest' else
                digest if key == 'boot-manifest-hash' else value) for key, value in chosen[0]]
new_adt = encode(tree)
spec = importlib.util.spec_from_file_location('pack_payload', root.parents[1] / 'pack-payload.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
out = module.Fat32(bytearray(payload))
module.replace_file(out, tuple(adt_path.split('/')), new_adt)
destination.write_bytes(out.data)
check = Fat32(out.data)
updated = {n: d for n, d, _, _ in check.walk(check.root)}
assert updated[adt_path] == new_adt
assert all(updated[n] == data for n, data in files.items() if n != adt_path)
destination.with_suffix('.owner.json').write_bytes(blob)
print('owner manifest refreshed for actual kernel/config; readback PASS')
