#!/usr/bin/env python3
"""Prepare the unchanged 20H392 OS trust cache and its standard ADT handoff."""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import sys

root = Path(__file__).resolve().parent
inputs = root.parents[1] / 'apfs-inputs-20H392-ready'
spec = importlib.util.spec_from_file_location('tc', root.parents[1]/'work/cs/extract-ios-trustcache.py')
tc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tc)
manifest = plistlib.loads((inputs/'BuildManifest.plist').read_bytes())
assert manifest['ProductBuildVersion'] == '20H392'
identity, = [i for i in manifest['BuildIdentities'] if i['Info']['DeviceClass'] == 'd22ap'
              and i['Info']['RestoreBehavior'] == 'Erase']
entry = identity['Manifest']['StaticTrustCache']
source = inputs / Path(entry['Info']['Path']).name
im4p = source.read_bytes()
expected = entry['Digest']
digest = {20:hashlib.sha1, 32:hashlib.sha256, 48:hashlib.sha384}[len(expected)](im4p).digest()
assert digest == expected
module = tc.im4p_payload(im4p)
cache = tc.TrustCache(module)
assert cache.version == 2 and cache.count == 2760 and cache.sorted
assert cache.uuid.hex() == '9f368a73d90b40ce943c827e13e72eee'
assert len(module) == 24+24*cache.count
assert all(e[1] in (1,2,3,4) for e in cache.entries)
bundle = struct.pack('<II',1,8)+module
(root/'original-static-trust.bundle').write_bytes(bundle)
(root/'original-static-trust.json').write_text(json.dumps(dict(
    source=str(source), build='20H392', version=cache.version, entries=cache.count,
    uuid=cache.uuid.hex(), im4p_sha256=hashlib.sha256(im4p).hexdigest(),
    module_sha256=hashlib.sha256(module).hexdigest(), bundle_sha256=hashlib.sha256(bundle).hexdigest(),
    original_module_modified=False, own_code_entries_added=False),indent=2)+'\n')

sys.path.insert(0,str(root.parent))
from verify_image_lib import Fat32
codec = ast.parse((root/'refresh_owner_manifest.py').read_text())
exec(compile(ast.Module(body=[n for n in codec.body if isinstance(n,ast.FunctionDef)
                              and n.name in ('parse','encode')], type_ignores=[]),'codec','exec'))
payload = Path(sys.argv[1])
data = payload.read_bytes()
fs = Fat32(data)
files = {n:b for n,b,_,_ in fs.walk(fs.root)}
adt_path = 'EFI/UniXNU/Payloads/kernel.macho.adt'
tree,end = parse(files[adt_path])
assert end == len(files[adt_path])
chosen, = [c for c in tree[1] if dict(c[0]).get('name') == b'chosen\0']
memory, = [c for c in chosen[1] if dict(c[0]).get('name') == b'memory-map\0']
prior = dict(memory[0]).get('TrustCache',bytes(16))
assert prior == bytes(16), 'Refuse to replace a live physical handoff'
memory[0][:] = [(k,v) for k,v in memory[0] if k != 'TrustCache']
memory[0].append(('TrustCache',bytes(16)))
spec = importlib.util.spec_from_file_location('pack',root.parents[1]/'pack-payload.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
out = pack.Fat32(bytearray(data))
pack.replace_file(out,tuple(adt_path.split('/')),encode(tree))
payload.write_bytes(out.data)
print('Original static module verified against BuildManifest; zeroed loader handoff prepared')
