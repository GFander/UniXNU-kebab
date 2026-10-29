from pathlib import Path
import importlib.util
import re
import struct
import subprocess
import tempfile

r=Path(__file__).parent
spec=importlib.util.spec_from_file_location('geni_adt',r.parent/'geni-adt.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
c=m.common
tree=([('name',b'device-tree\0'),('model',b'iPhone10,3\0')],[
    ([('name',b'chosen\0'),('random-seed',bytes(range(64)))],[]),m.common.spmi_node()])
old=c.encode(tree);new=m.add(old);decoded,end=c.parse(new)
assert end==len(new) and decoded[0]==tree[0] and decoded[1][:-1]==tree[1]
assert decoded[1][-1]==m.node() and m.add(new)==new
for length in (0,7,len(new)-1,len(new)-4):
    try:m.add(new[:length])
    except (ValueError,struct.error):pass
    else:raise AssertionError('truncation accepted')
bad=m.node();bad[0][2]=('reg',bytes(48))
try:m.add(c.encode((tree[0],tree[1]+[bad])))
except ValueError:pass
else:raise AssertionError('resource conflict accepted')
source=(r/'after/bsd/dev/kebab_geni.c').read_text()
constants=dict(re.findall(r'^#define (KT_\w+|TLMM_TILE_SOUTH)\s+(0x[0-9a-f]+|[0-9]+)u',source,re.M))
get=lambda name:int(constants[name],0)
assert m.RESOURCES==(get('KT_SE13_PA_DEFAULT'),get('KT_SE13_SIZE'),get('KT_WRAP_PA_DEFAULT'),
    get('KT_WRAP_SIZE'),get('KT_TLMM_PA')+get('TLMM_TILE_SOUTH'),get('KT_TLMM_SIZE'))
assert int(dict(m.node()[0])['kebab,touch-address'].hex(),16)==0x4b000000
catalogue=(r/'after/iokit/KernelConfigTables.cpp').read_text()
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory);(p/'test.cpp').write_text('#include <cstdio>\n'+catalogue+'\nint main(){puts(gIOKernelConfigTables);}\n')
    for board in ('KEBAB','ARM64_BOARD_CONFIG_NEGRONI',None):
        subprocess.run(['clang++','-Wall','-Wextra','-Werror',*(['-D'+board] if board else []),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        table=subprocess.check_output([str(p/'test')],text=True)
        for name,provider,match in [('KebabGeniBus','IOPlatformDevice','kebab,sm8250-geni13'),
                                  ('KebabTouch','KebabI2CDevice','synaptics,s3908')]:
            entries=[e for e in re.findall(r'\{([^{}]*)\}',table) if re.search(r"'IOClass'\s*=\s*"+name+';',e)]
            assert len(entries)==(1 if board=='KEBAB' else 0)
            if entries:
                assert re.search(r"'IOProviderClass'\s*=\s*"+provider+';',entries[0])
                assert re.search(r"'IONameMatch'\s*=\s*'"+match+"';",entries[0])
                if name=='KebabGeniBus':assert "'IOResourceMatch' = IOBSD;" in entries[0]
print('GENI ADT exact resource provenance, child address, old-node invariance, rejection and 3 board personality scopes PASS')
