#!/usr/bin/env python3
"""Inventory an immutable source snapshot and inspect the tested native kernel."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--kernel',type=Path,required=True)
parser.add_argument('--prelink-tools',type=Path,required=True)
parser.add_argument('--prelink-report',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args = parser.parse_args()
groups = {
 'platform': ['iokit/Kernel/arm/KebabPlatformExpert.cpp'],
 'cpu': ['iokit/Kernel/arm/KebabCPU.cpp'],
 'ufs': ['bsd/dev/kebab_ufs.c','iokit/Kernel/arm/KebabBlockStorageDevice.cpp'],
 'usb': ['bsd/dev/kebab_usb.c'],
 'spmi_pmic': ['bsd/dev/kebab_spmi.c'],
 'rpmh_vrm': ['bsd/dev/kebab_rpmh_vrm.c'],
 'touch': ['bsd/dev/kebab_touch.c','bsd/dev/kebab_touch_tcm.c'],
 'keys': ['bsd/dev/kebab_keys.c','bsd/dev/kebab_keys_core.c'],
 'display': ['iokit/Kernel/arm/KebabFramebuffer.cpp'],
}
compat = {'surface': ['iokit/Kernel/arm/KebabSurface.cpp']}
records = {}
classes = {}
for group,paths in {**groups,**compat}.items():
    for path in paths:
        b = (args.source_root/path).read_bytes()
        code = b.decode('utf-8')
        records[path] = dict(group=group,physical_lines=len(b.splitlines()),bytes=len(b),
                             sha256=hashlib.sha256(b).hexdigest())
        for name,base in re.findall(r'OSDefineMetaClassAndStructors\(\s*(\w+)\s*,\s*(\w+)\s*\)',code):
            classes[name] = dict(base=base,path=path)
sys.path.insert(0,str(args.prelink_tools))
from native_prelink import macho,exports,backed_offset
kernel = args.kernel.read_bytes()
image = macho(kernel)
symbols = exports(kernel,image)
table_va, = [va for va,names in symbols.items() if '_gIOKernelConfigTables' in names]
table_at = backed_offset(image['segments'],table_va,8,len(kernel))
text_va, = struct.unpack_from('<Q',kernel,table_at)
text_at = backed_offset(image['segments'],text_va,1,len(kernel))
end = kernel.index(b'\0',text_at)
personality = kernel[text_at:end].decode('ascii')
assert 'KebabPlatformExpert' in personality and 'KebabCPU' in personality
prelink = json.loads(args.prelink_report.read_text())
all_names = {name for names in symbols.values() for name in names}
queries = {'platform':'KebabPlatformExpert','cpu':'KebabCPU','ufs':'_kebab_ufs_rootdev',
           'usb':'_kebab_usb_start','spmi_pmic':'_kebab_spmi_init','rpmh_vrm':'_kebab_rpmh_',
           'touch':'_kebab_touch_start','keys':'_kebab_keys_start','display':'AppleCLCD',
           'surface':'IOCoreSurfaceRoot'}
linked = {group:sorted(n for n in all_names if query in n) for group,query in queries.items()}
assert all(linked.values()), 'An inventoried subsystem has no native kernel symbol evidence'
report = dict(baseline='native 0187; boot0190 uses prelink0188 unchanged',
              hardware_platform_groups=len(groups), groups=groups,
              separate_compatibility_services=compat,
              pmgr_compatibility_entry='embedded in KebabPlatformExpert.cpp; original donor class, not own driver',
              unique_implementation_files=len(records),
              physical_lines=sum(v['physical_lines'] for v in records.values()),
              class_count=len(classes), classes=classes,files=records,
              kernel_sha256=hashlib.sha256(kernel).hexdigest(),
              linked_symbol_examples={k:v[:3] for k,v in linked.items()},
              donor_sha256=prelink['original_sha256'],
              compiled_personality_table=personality,
              surface_root_matching_in_native_table='IOCoreSurfaceRoot' in personality,
              selected_donor_hid_components=[x for x in prelink['components'] if 'HID' in x],
              excluded=['headers','crypto library ports','Apple OSS HFS+','donor kexts',
                        'installer and volume writer','AMFI/code-sign/dyld helpers',
                        'early console, GIC/timer/architecture adaptations','tests and host probes'])
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['hardware_platform_groups','unique_implementation_files',
    'physical_lines','class_count','surface_root_matching_in_native_table','selected_donor_hid_components']},indent=2))
print('compiled personality:',personality)
