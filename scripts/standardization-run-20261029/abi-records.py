"""Pin facts and qualification gaps per Apple-facing class, without inventing ABI."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

version=sys.argv[1];r=Path(__file__).parent;images=r.parent/'p1-run/prelink-fix'
manifest=json.loads((r/('manifest-'+version+'.json')).read_text());snapshot=r/('sources-'+version)
out=r/'abi';out.mkdir(exist_ok=True)
donor='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
common=dict(target='iPhone10,3 20H392 / XNU8796.141.3 ARM64 16KiB',original_kernelcache_sha256=donor,
    evidence_version=version,native_sha256=manifest['native_sha256'])
def write(name,record):
    (out/(name+'.json')).write_text(json.dumps(dict(common,io_class=name,**record),indent=2)+'\n')
def record(path):
    text=(snapshot/path).read_text()
    return dict(source_path=path,source_sha256=manifest['files'][path]['sha256']),text
fb_record,fb=record('iokit/Kernel/arm/KebabFramebuffer.cpp')
surf_record,surface=record('iokit/Kernel/arm/KebabSurface.cpp')
def methods(code):
    numbers={name:int(value,0) for name,value in re.findall(r'\b(kKebab\w+)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*[,}]',code)}
    return [dict(name=name,selector=numbers[name],native_scalar_input_min=int(incount),
                 native_scalar_output_min=int(outcount))
            for name,incount,outcount in re.findall(r'\{\s*(kKebab\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*}',code)]
fb_methods=methods(fb);surf_methods=methods(surface)
assert any(m['selector']==74 for m in fb_methods) and len(fb_methods)==24
assert len(surf_methods)==9
graphics_limits=['Native compatibility implementation; no original graphics kext object/vtable is imported.',
    'Native factory/query/bad-output/close tests are qualified; user-process IOServiceOpen and donor frameworks are not.',
    'Scalar counts describe this implementation minima; partial structure conventions are preserved, not proven donor layouts.',
    'No backboardd, hardware vsync, DPU scanout, 120Hz or GPU qualification.']
for name in ['IOMobileFramebuffer','AppleCLCD','IOMobileFramebufferUserClient']:
    write(name,dict(fb_record,implementation='native IOKit compatibility service/client',
        original_private_layout_recovered=False,donor_userland_abi_qualified=False,
        class_size_crosses_foreign_binary_boundary=False,native_methods=fb_methods,
        unresolved=[dict(selector=74,name='SupportedFrameInfo',status='project compatibility layout; donor layout not recovered')],
        verification=['0191 lifecycle/parameter contracts','0203 native factory/query/close PASS','0204 actual AppleCLCD registry match'],
        limits=graphics_limits))
for name in ['IOCoreSurfaceRoot','IOSurfaceRootUserClient']:
    write(name,dict(surf_record,implementation='native IOKit compatibility service/client',
        original_private_layout_recovered=False,donor_userland_abi_qualified=False,
        class_size_crosses_foreign_binary_boundary=False,native_methods=surf_methods,
        verification=['0191 Surface owner/client reference and close contracts','0203 native factory/query/close PASS','0204 actual root registry match'],
        pending=['donor IOSurface.framework call layouts','cross-process port/map behavior from real user processes','backboardd end-to-end'],
        limits=graphics_limits[:3]))
storage=json.loads((r.parent/('driver-standardization/abi/storage-'+version+'.json')).read_text())
write('IOBlockStorageDevice',dict(implementation='native subclass of original donor base through pinned bindings',
    audit=storage,verification=['0203 real descriptor NXSB/completion/reentry/short/bounds/RO PASS',
        version+' APFS root and complete launchd/dyld hashes unchanged'],
    donor_userland_abi_qualified=False,full_original_class_size_proven=False))
pmgr_record,_=record('iokit/Kernel/arm/KebabPlatformExpert.cpp')
run=subprocess.run([sys.executable,str(images/'audit-pmgr-contract.py'),str(images/('prelink-'+version+'.bin'))],
    text=True,capture_output=True,check=True)
assert 'PASS' in run.stdout
write('ApplePMGRNub',dict(pmgr_record,implementation='original donor metaclass object; development provider-less compatibility',
    donor_class_size=0x740,private_provider_offset=0x88,private_provider_value=None,
    static_audit='unchanged AMFI/PMGR executable hashes, NULL-provider accessor instructions, size immediate and native init binding PASS',
    original_pmgr_text_sha256='74ac53a277da51806f323e8c7d765d4ce9baa443adf4e513be2cff52a245db0e',
    aon_read_on_null='0xffffffff (unsupported)',aon_write_on_null='no operation',
    real_power_management_qualified=False,limits=['No Qualcomm PMIC MMIO by original Apple accessors',
        'Original hardware start is not called; object retained for platform lifetime','Not a persistent AON or IOPM implementation']))
print('7 per-class ABI records pinned; graphics qualification gaps explicit; current donor PMGR audit PASS')
