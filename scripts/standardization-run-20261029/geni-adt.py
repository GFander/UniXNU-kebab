"""Add the measured SE13/TLMM resource scope, preserving all existing ADT data."""
from pathlib import Path
import importlib.util
import struct
import sys

spec=importlib.util.spec_from_file_location('spmi_adt',Path(__file__).with_name('spmi-adt.py'))
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
RESOURCES=(0xa94000,0x4000,0xac0000,0x2000,0xf500000,0x40000)
def node():
    return ([('name',b'kebab-geni13\0'),
        ('compatible',b'kebab,sm8250-geni13\0qcom,i2c-geni\0'),
        ('reg',struct.pack('<6Q',*RESOURCES)),
        ('kebab,touch-address',struct.pack('<I',0x4b))],[])
def add(data):
    tree,end=common.parse(data)
    if end!=len(data) or common.encode(tree)!=data:raise ValueError('ADT roundtrip differs')
    existing=[c for c in tree[1] if dict(c[0]).get('name')==b'kebab-geni13\0']
    if existing:
        if len(existing)!=1 or existing[0]!=node():raise ValueError('conflicting GENI resource profile')
        return data
    tree[1].append(node())
    return common.encode(tree)
if __name__=='__main__':
    # Reuse the common FAT/other-file invariance verifier with this node adder.
    common.add=add
    common.main()
