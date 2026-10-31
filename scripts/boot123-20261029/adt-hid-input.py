"""Change only /defaults/kebab,hid-input in a candidate payload, not boot args."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, struct, sys
r=Path(__file__).resolve().parent
sys.path.insert(0,str(r.parent/'p1-run'))
from verify_image_lib import Fat32

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def change(adt,mask):
    assert mask in (0,1,2,3), 'Unknown HID input mask'
    m=load('owned_memory',r.parent/'cryptex-runtime-20261029/owned-memory.py')
    tree,end=m.parse(adt);assert end==len(adt)
    defaults,=[c for c in tree[1] if dict(c[0]).get('name')==b'defaults\0']
    prop='kebab,hid-input';original=list(defaults[0])
    matches=[(i,value) for i,(key,value) in enumerate(original) if key==prop]
    assert len(matches)<=1
    if matches:
        assert len(matches[0][1])==4 and struct.unpack('<I',matches[0][1])[0] in (1,2,3)
        del defaults[0][matches[0][0]]
    if mask:defaults[0].append((prop,struct.pack('<I',mask)))
    new=m.encode(tree)
    # Prove every existing property and child, including hardware key settings,
    # is byte-identical after removing just this optional property again.
    defaults[0][:]=original;assert m.encode(tree)==adt
    return new

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    p.add_argument('--mask',type=int,choices=range(4),required=True);a=p.parse_args()
    assert a.source.resolve()!=a.destination.resolve()
    assert not a.destination.exists(), 'Do not overwrite a packaged candidate'
    payload=a.source.read_bytes();fs=Fat32(payload)
    files={n:d for n,d,_,_ in fs.walk(fs.root)};key='EFI/UniXNU/Payloads/kernel.macho.adt'
    new=change(files[key],a.mask)
    pack=load('pack_payload',r.parent/'pack-payload.py')
    out=pack.Fat32(bytearray(payload));pack.replace_file(out,tuple(key.split('/')),new)
    final=Fat32(out.data);actual={n:d for n,d,_,_ in final.walk(final.root)}
    assert actual.keys()==files.keys() and actual[key]==new
    assert all(actual[n]==d for n,d in files.items() if n!=key)
    a.destination.write_bytes(out.data)
    receipt=dict(mask=a.mask,only_adt_property='/defaults/kebab,hid-input',
                 all_other_fat_files_identical=True,boot_arguments_changed=False,
                 before_adt_sha256=hashlib.sha256(files[key]).hexdigest(),
                 after_adt_sha256=hashlib.sha256(new).hexdigest())
    a.destination.with_suffix('.hid.json').write_text(json.dumps(receipt,indent=2)+'\n',newline='\n')
    print('ADT physical HID mask',a.mask,'all other payload files identical')

if __name__=='__main__':main()
