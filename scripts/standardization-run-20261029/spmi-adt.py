"""Add the SM8250 arbiter resource profile without changing existing ADT nodes."""
from pathlib import Path
import argparse
import importlib.util
import struct
import sys

RESOURCES=(0x0c440000,0x1100,0x0e600000,0x100000,0x0c40a000,0x26000,0x0c600000,0x2000000)
def spmi_node():
    return ([('name',b'kebab-spmi\0'),
        ('compatible',b'kebab,sm8250-spmi\0qcom,spmi-pmic-arb\0'),
        ('reg',struct.pack('<8Q',*RESOURCES)),('qcom,ee',struct.pack('<I',0)),
        ('kebab,pon',struct.pack('<2I',0,8)),
        ('kebab,pmic-keys',struct.pack('<4I',0,8,0xc5,0xc6)),
        ('kebab,rpmh-rsc',struct.pack('<2Q',0x18220000,0x10000)),
        ('kebab,rpmh-cmd-db',struct.pack('<2Q',0x80860000,0x20000))],[])

def parse(data,at=0,depth=0):
    if depth>64 or at+8>len(data):raise ValueError('invalid ADT node')
    count,children=struct.unpack_from('<II',data,at);at+=8
    if count>4096 or children>4096:raise ValueError('unbounded ADT counts')
    props=[];nodes=[]
    for _ in range(count):
        if at+36>len(data):raise ValueError('truncated ADT property')
        key,size=struct.unpack_from('<32sI',data,at);at+=36
        if at+((size+3)&~3)>len(data):raise ValueError('truncated ADT value')
        props.append((key.rstrip(b'\0').decode('ascii'),data[at:at+size]));at+=(size+3)&~3
    for _ in range(children):
        child,at=parse(data,at,depth+1);nodes.append(child)
    return (props,nodes),at

def encode(tree):
    props,children=tree;out=struct.pack('<II',len(props),len(children))
    for name,value in props:
        key=name.encode('ascii')
        if len(key)>31:raise ValueError('long ADT property key')
        out+=struct.pack('<32sI',key,len(value))+value+bytes(-len(value)%4)
    return out+b''.join(encode(c) for c in children)

def add(data):
    tree,end=parse(data)
    if end!=len(data) or encode(tree)!=data:raise ValueError('ADT roundtrip differs')
    existing=[c for c in tree[1] if dict(c[0]).get('name')==b'kebab-spmi\0']
    if existing:
        if len(existing)!=1 or existing[0]!=spmi_node():raise ValueError('conflicting arbiter profile')
        return data
    tree[1].append(spmi_node())
    return encode(tree)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('input',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    h=Path('D:/LOS/HiOS');sys.path.insert(0,str(h/'p1-run'))
    from verify_image_lib import Fat32
    spec=importlib.util.spec_from_file_location('pack_payload',h/'pack-payload.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    raw=a.input.read_bytes();fs=Fat32(raw)
    files={n:d for n,d,_,_ in fs.walk(fs.root)};path='EFI/UniXNU/Payloads/kernel.macho.adt'
    expected=add(files[path]);out=module.Fat32(bytearray(raw));module.replace_file(out,tuple(path.split('/')),expected)
    verify=Fat32(out.data);updated={n:d for n,d,_,_ in verify.walk(verify.root)}
    assert updated[path]==expected and all(updated[n]==v for n,v in files.items() if n!=path)
    a.output.write_bytes(out.data)
    print('SPMI ADT: resource/schema checks passed; every other FAT file unchanged')
if __name__=='__main__':main()
