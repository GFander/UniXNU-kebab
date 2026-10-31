#!/usr/bin/env python3
"""Check this donor's unchanged AMFI/PMGR ABI; no hardware access."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from native_prelink import macho, backed_offset, exports

path = Path(sys.argv[1])
data = path.read_bytes()
image = macho(data)
report = json.loads(path.with_suffix('.json').read_text())

def read(va, size):
    at = backed_offset(image['segments'], va, size, len(data))
    return data[at:at+size]

def words(va, values):
    assert struct.unpack('<' + 'I' * len(values), read(va, 4 * len(values))) == values, hex(va)

for name, digest in {
    'com.apple.driver.ApplePMGR': '74ac53a277da51806f323e8c7d765d4ce9baa443adf4e513be2cff52a245db0e',
    'com.apple.driver.AppleMobileFileIntegrity': '6cbea28deab02a2b848dfac28f11c3c00745b55e7b2a896bac3cf9a600a4c7b1',
}.items():
    extent, = [e for e in report['extents'] if e['module'] == name and e['segment'] == '__TEXT_EXEC']
    assert hashlib.sha256(read(extent['va'], extent['size'])).hexdigest() == digest
    assert report['original_executable_sha256'][name] == digest

words(0xfffffe00088bd734, (0xf9404400, 0xb4000080, 0xf9400008, 0xf944e101,
                          0xd61f0020, 0x12800000, 0xd65f03c0))
words(0xfffffe00088bbdbc, (0xf9404400, 0xb4000080, 0xf9400008, 0xf944e502,
                          0xd61f0040, 0xd65f03c0))
words(0xfffffe0008936790, (0x940065a9, 0x321e0015, 0xaa1403e0, 0xaa1503e1, 0x940065a8))
# Original metaclass passes sizeof(ApplePMGRNub) = 0x740 to OSMetaClass.
words(0xfffffe00088bb0fc, (0x5280e803,))
assert read(0xfffffe0008033acf, 13) == b'ApplePMGRNub\0'
# The ordinary init slot must resolve to the real native IOService init.
init, = struct.unpack('<Q', read(0xfffffe0009edd058 + 0xa8, 8))
assert '__ZN9IOService4initEP12OSDictionary' in exports(data, image).get(init, [])
print('donor AMFI/PMGR executable hashes, NULL-provider read/write, size and native init: PASS')
