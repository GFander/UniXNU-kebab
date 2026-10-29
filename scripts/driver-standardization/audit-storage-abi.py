#!/usr/bin/env python3
"""Pin the target storage bindings and verify the compiled bridge dispatch slots.

This is static ABI evidence. Device I/O and provider lifetime are tested
separately; private graphics/HID ABIs are not qualified by this check.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import sys

parser=argparse.ArgumentParser()
parser.add_argument('--prelink',type=Path,required=True)
parser.add_argument('--bindings',type=Path,required=True)
parser.add_argument('--header',type=Path,required=True)
parser.add_argument('--module-path',type=Path,required=True,
    help='Directory containing this build\'s native_prelink.py')
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args()
sys.path.insert(0,str(a.module_path))
from native_prelink import macho,backed_offset,exports

data=a.prelink.read_bytes();image=macho(data);symbols=exports(data,image)
report=json.loads(a.prelink.with_suffix('.json').read_text())
assert report['original_sha256']=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
assert report['candidate_sha256']==hashlib.sha256(data).hexdigest()
def read(va,n):
    at=backed_offset(image['segments'],va,n,len(data))
    return data[at:at+n]
module='com.apple.iokit.IOStorageFamily'
extent,=[e for e in report['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC']
digest=hashlib.sha256(read(extent['va'],extent['size'])).hexdigest()
assert digest==report['original_executable_sha256'][module]=='4c9e7e8f12306986dc124e9e2cd4f9126fa7dc61e89155d973767b88a0d5ce6f'

bindings=[]
for name,address in re.findall(r'\.set\s+(\w+),\s*(0x[0-9a-f]+)',a.bindings.read_text()):
    va=int(address,16)
    segments=[e for e in report['extents'] if e['module']==module and e['va']<=va<e['va']+e['size']]
    assert len(segments)==1,(name,address,'outside pinned component')
    assert name in symbols.get(va,[]),(name,'not exported at fixed target')
    bindings.append(dict(symbol=name,address=address,segment=segments[0]['segment']))
assert len(bindings)==10
vtable,=[va for va,names in symbols.items() if '__ZTV23KebabBlockStorageDevice' in names]
original_vtable=0xfffffe0009eabf50
methods=['doEjectMedia','doFormatMedia','doGetFormatCapacities','doSynchronizeCache',
    'getVendorString','getProductString','getRevisionString','getAdditionalDeviceInfoString',
    'reportBlockSize','reportEjectability','reportMaxValidBlock','reportMediaState',
    'reportRemovability','reportWriteProtection','getWriteCacheState','setWriteCacheState',
    'doAsyncReadWrite','requestIdle','doUnmap','doSetPriority','doSynchronize','doGetProvisionStatus']
inherited={'getWriteCacheState','setWriteCacheState','doSetPriority','doGetProvisionStatus'}
rows=[]
for slot,method in enumerate(methods,168):
    target,=struct.unpack('<Q',read(vtable+16+slot*8,8))
    donor,=struct.unpack('<Q',read(original_vtable+16+slot*8,8))
    owner='IOBlockStorageDevice' if method in inherited else 'KebabBlockStorageDevice'
    marker=str(len(owner))+owner+str(len(method))+method
    names=symbols.get(target,[])
    assert any(marker in n for n in names),(slot,method,hex(target),names)
    if method in inherited:assert target==donor
    rows.append(dict(slot=slot,method=method,owner=owner,target=hex(target),
        donor_base_target=hex(donor),compiled_symbols=names))
out=dict(target='iPhone10,3 20H392 / XNU8796.141.3 ARM64 16KiB',
    original_sha256=report['original_sha256'],slide=report['slide'],
    candidate_sha256=report['candidate_sha256'],storage_text_sha256=digest,
    header_sha256=hashlib.sha256(a.header.read_bytes()).hexdigest(),
    bindings_sha256=hashlib.sha256(a.bindings.read_bytes()).hexdigest(),
    bindings=bindings,bridge_vtable=hex(vtable),methods=rows,
    limits=['Static binding/slot identity only; class sizes and private userland ABI not inferred.',
        'Provider remains the boot-lifetime platform root; C backend owns the UFS controller.',
        'No DMA, SMMU, IRQ or SMP behavior was changed by standardization.'])
a.output.write_text(json.dumps(out,indent=2)+'\n')
print('PASS: fixed 20H392 storage text, 10 bindings and 22 compiled bridge slots')
