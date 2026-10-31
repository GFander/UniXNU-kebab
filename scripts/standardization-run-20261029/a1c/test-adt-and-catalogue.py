from pathlib import Path
import importlib.util
import re
import struct
import subprocess
import sys
import tempfile

r=Path(__file__).parent
spec=importlib.util.spec_from_file_location('spmi_adt',r.parent/'spmi-adt.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
tree=([('name',b'device-tree\0'),('model',b'iPhone10,3\0')],[
    ([('name',b'chosen\0'),('random-seed',bytes(range(64))),('TrustCache',bytes(16))],[]),
    ([('name',b'cpus\0')],[])])
old=module.encode(tree);new=module.add(old);decoded,end=module.parse(new)
assert end==len(new) and decoded[0]==tree[0] and decoded[1][:-1]==tree[1]
assert decoded[1][-1]==module.spmi_node() and module.add(new)==new
for length in (0,7,len(new)-1,len(new)-4):
    try:module.add(new[:length])
    except (ValueError,struct.error):pass
    else:raise AssertionError('truncated ADT accepted')
bad=module.spmi_node();bad[0][2]=('reg',bytes(64))
try:module.add(module.encode((tree[0],tree[1]+[bad])))
except ValueError:pass
else:raise AssertionError('conflicting resource profile accepted')

header=(r/'after/bsd/dev/kebab_spmi_bus_private.h').read_text()
defines=dict(re.findall(r'^#define (KEBAB_SPMI_\w+)\s+(0x[0-9a-f]+|[0-9]+)u',header,re.M))
keys=['CORE_PA','CORE_SIZE','OBSRV_PA','OBSRV_SIZE','CNFG_PA','CNFG_SIZE','CHNLS_PA','CHNLS_SIZE']
assert module.RESOURCES==tuple(int(defines['KEBAB_SPMI_'+k],0) for k in keys)

catalogue=(r/'after/iokit/KernelConfigTables.cpp').read_text()
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text('#include <cstdio>\n'+catalogue+'\nint main(){puts(gIOKernelConfigTables);}\n')
    for board in ('KEBAB','ARM64_BOARD_CONFIG_NEGRONI',None):
        subprocess.run(['c++','-Wall','-Wextra','-Werror',*(['-D'+board] if board else []),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        table=subprocess.check_output([str(p/'test')],text=True)
        for name,provider,match in [('KebabSPMIBus','IOPlatformDevice','kebab,sm8250-spmi'),
            ('KebabPon','KebabSPMIDevice','pmic-pon'),('KebabPmicKeys','KebabSPMIDevice','pmic-keys'),('KebabVrm','KebabSPMIDevice','rpmh-vrm-policy')]:
            entries=[e for e in re.findall(r'\{([^{}]*)\}',table) if re.search(r"'IOClass'\s*=\s*"+name+';',e)]
            assert len(entries)==(1 if board=='KEBAB' else 0)
            if entries:
                assert re.search(r"'IOProviderClass'\s*=\s*"+provider+';',entries[0])
                assert re.search(r"'IONameMatch'\s*=\s*'"+match+"';",entries[0])
                if name=='KebabSPMIBus':assert "'IOResourceMatch' = IOBSD;" in entries[0]
print('PASS: ADT exact resource provenance, old-node preservation, conflict/truncation rejection; native board-scoped personalities')
