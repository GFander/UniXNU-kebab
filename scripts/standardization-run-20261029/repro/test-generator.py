from pathlib import Path
import importlib.util
import os
import sys
import tempfile

r=Path(__file__).parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
old=load('old_generator',r/'before/make-kebab-adt.py')
new=load('new_generator',r/'after/make-kebab-adt.py')
spmi=load('spmi',r.parent/'spmi-adt.py');geni=load('geni',r.parent/'geni-adt.py')
saved=os.urandom;os.urandom=lambda size: bytes((i%256 for i in range(size)))
saved_argv=sys.argv
try:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for arguments in ([],['--development-boot','--iphone-identity','--trust-cache',
            '--root-match-volume-uuid','DADD93F8-DBBF-4B17-A1DF-0F11E64172F7']):
            results=[]
            for module,name in [(old,'old'),(new,'new')]:
                path=root/(name+'.adt');sys.argv=['make-kebab-adt.py','--output',str(path),'--dram-size','0x100000000',*arguments]
                module.main();results.append(path.read_bytes())
            expected=geni.add(spmi.add(results[0]))
            assert results[1]==expected,'formal generator changed a previous node or resource profile'
            tree,end=spmi.parse(results[1]);assert end==len(results[1])
            if arguments:
                chosen=next(c for c in tree[1] if dict(c[0]).get('name')==b'chosen\0')
                assert dict(chosen[0])['boot-nonce']==bytes(range(32))
                manifest=next(c for c in chosen[1] if dict(c[0]).get('name')==b'manifest-properties\0')
                assert 'BNCH' in dict(manifest[0])
finally:
    os.urandom=saved;sys.argv=saved_argv
print('Formal ADT generator preserves every previous node, root identity and nonce/BNCH; new profiles equal flashed overlays PASS')
