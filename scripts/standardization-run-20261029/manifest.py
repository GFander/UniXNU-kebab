"""Snapshot actual source, native identities and observed registry evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys

ap=argparse.ArgumentParser();ap.add_argument('version');ap.add_argument('--replay',action='store_true');a=ap.parse_args()
r=Path(__file__).parent;images=r.parent/'p1-run/prelink-fix'
groups={
 'platform':['iokit/Kernel/arm/KebabPlatformExpert.cpp'],
 'cpu':['iokit/Kernel/arm/KebabCPU.cpp'],
 'ufs':['bsd/dev/kebab_ufs.c','iokit/Kernel/arm/KebabBlockStorageDevice.cpp'],
 'usb':['bsd/dev/kebab_usb.c','bsd/dev/kebab_reboot.c'],
 'spmi_pmic':['bsd/dev/kebab_spmi.c','bsd/dev/kebab_spmi_bus.c','bsd/dev/kebab_spmi_pon.c','bsd/dev/kebab_spmi_keys.c','bsd/dev/kebab_spmi_power.c','iokit/Kernel/arm/KebabSPMI.cpp'],
 'rpmh_vrm':['bsd/dev/kebab_rpmh.c','bsd/dev/kebab_rpmh_vrm.c'],
 'touch':['bsd/dev/kebab_geni.c','bsd/dev/kebab_touch.c','bsd/dev/kebab_touch_tcm.c','iokit/Kernel/arm/KebabGeni.cpp'],
 'keys':['bsd/dev/kebab_keys.c','bsd/dev/kebab_keys_core.c'],
 'display':['iokit/Kernel/arm/KebabFramebuffer.cpp']}
compat={'surface':['iokit/Kernel/arm/KebabSurface.cpp']}
components={'software_aes':['iokit/Kernel/arm/NativeAES.cpp'],
 'apfs_probe':['iokit/Kernel/arm/KebabVolumeLookup.cpp','bsd/kern/kebab_apfs_probe.c']}
paths=sorted({p for files in [*groups.values(),*compat.values(),*components.values()] for p in files})
support=['iokit/KernelConfigTables.cpp','iokit/Kernel/arm/KebabDriverManifest.h',
 'iokit/Kernel/arm/KebabDriverDiagnostics.cpp','iokit/Kernel/arm/KebabDriverSelfTest.cpp',
 'bsd/dev/kebab_geni.h','bsd/dev/kebab_spmi_bus.h','bsd/dev/kebab_spmi_bus_private.h','bsd/dev/kebab_spmi_clients.h',
 'bsd/dev/kebab_pmic_regs.h','bsd/kern/kebab_bootdiag.c','osfmk/kern/kebab_bootdiag.c',
 'bsd/conf/files','osfmk/conf/files','iokit/conf/files.arm64','bsd/kern/bsd_init.c',
 'bsd/kern/kern_exec.c','bsd/kern/kern_newsysctl.c','iokit/Kernel/IOMemoryDescriptor.cpp',
 'iokit/Kernel/arm/OriginalAESABI.h','iokit/Kernel/arm/OriginalAESBindings.cpp','iokit/Kernel/arm/KebabPrelinkBindings.cpp',
 'bsd/kern/kebab_scm_probe.c','bsd/dev/kebab_reboot.h','bsd/dev/kebab_fwlog.c',
 'bsd/kern/kebab_apfs_probe.h','bsd/dev/kebab_install.c']
all_paths=paths+support
def read(host,root):
    result=subprocess.run(['ssh',host,'python3 source-reader.py '+root],input=json.dumps(all_paths),
        text=True,encoding='utf-8',capture_output=True,check=True)
    return json.loads(result.stdout)
ubuntu=read('ubuntualbert','/home/albert/s4/x')
mac=read('albertlee@192.168.30.165','/Users/albertlee/kebab-build/dxb/xnu')
assert {p:v['sha256'] for p,v in ubuntu.items()}=={p:v['sha256'] for p,v in mac.items()},'actual source trees differ'
if a.replay:
    replay=read('albertlee@192.168.30.165','/Users/albertlee/kebab-build/verify-0181')
    assert {p:v['sha256'] for p,v in ubuntu.items()}=={p:v['sha256'] for p,v in replay.items()},'independent replay differs'
snapshot=r/('sources-'+a.version)
for path,value in ubuntu.items():
    p=snapshot/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(value['source'],newline='\n')
classes={}
for path in paths:
    for name,base in re.findall(r'OSDefineMetaClassAndStructors\(\s*(\w+)\s*,\s*(\w+)\s*\)',ubuntu[path]['source']):
        assert name not in classes;classes[name]=dict(base=base,path=path)
sys.path.insert(0,str(images));from native_prelink import macho,exports,backed_offset
native=(images/('k'+a.version+'.noctf')).read_bytes();image=macho(native);symbols=exports(native,image)
all_symbols={n for names in symbols.values() for n in names}
assert all(any(name in symbol for symbol in all_symbols) for name in classes),'a source class is not linked'
table_va,=[va for va,names in symbols.items() if '_gIOKernelConfigTables' in names]
import struct
at=backed_offset(image['segments'],table_va,8,len(native));text_va,=struct.unpack_from('<Q',native,at)
at=backed_offset(image['segments'],text_va,1,len(native));personality=native[at:native.index(b'\0',at)].decode('ascii')
registry=(images/(a.version+'-drivers.log')).read_text(errors='replace')
observed={m[0]:dict(io_class=m[1],matched=int(m[2]),iterator_valid=m[3]=='1') for m in
    re.findall(r'KEBAB DRV: (\w+) class=(\w+) matched=(\d+)\+? valid=(\d)',registry)}
for role in ['platform','cpu','ufs','spmi','pon','keys','rpmh','geni','touch','display','surface']:
    assert observed[role]['matched']>0 and observed[role]['iterator_valid'],role
result=dict(version=a.version,target='SM8250 / XNU8796.141.3 / 16KiB / 20H392',hardware_platform_groups=9,
    groups=groups,separate_compatibility=compat,separate_components=components,implementation_files=len(paths),
    physical_lines=sum(ubuntu[p]['lines'] for p in paths),classes=classes,class_count=len(classes),
    native_sha256=hashlib.sha256((images/('kernel-'+a.version+'.kebab')).read_bytes()).hexdigest(),
    boot_sha256=hashlib.sha256((images/('boot-'+a.version+'-ro.img')).read_bytes()).hexdigest(),
    source_trees_equal=True,independent_patch_replay_equal=a.replay,
    equality_scope='Only explicitly listed implementation/support files; whole-checkout equality is not asserted.',
    files={p:{k:v for k,v in record.items() if k!='source'} for p,record in ubuntu.items()},
    native_personality=personality,observed_registry=observed,
    limits=['USB remains a resident C core','normal bus services do not unload boot-owned C mappings/polling',
        'No HID/backboardd/Metal/DPU-vsync or full launchd qualification','No SMMU/SMP/encrypted-Data-write qualification',
        'SoftwareAES raw-key CBC does not supply original Image4 selector 2212',
        'APFS Preboot proof is recorded separately and does not establish Data protection'])
(r/('manifest-'+a.version+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print('Manifest:',len(groups),'domains;',len(paths),'main implementation files;',len(classes),'classes;',len(observed),'actual registry roles; tree equality',a.replay)
