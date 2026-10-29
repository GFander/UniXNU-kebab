"""Package a fully-built kernel, retaining the last verified read-only payload."""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess
import sys

p=argparse.ArgumentParser()
p.add_argument('version');p.add_argument('previous')
p.add_argument('--selftest',action='store_true')
p.add_argument('--boot-arg',action='append',default=[])
p.add_argument('--without-boot-arg',action='append',default=[])
p.add_argument('--adt-patch',type=Path)
a=p.parse_args()
assert len(a.version)==4 and a.version.isdigit()
assert len(a.previous)==4 and a.previous.isdigit()
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');h=r.parent.parent
def run(*args):
    subprocess.run([sys.executable,*map(str,args)],cwd=r,check=True)
v=a.version;old=a.previous
native=r/('kernel-'+v+'.kebab')
print('Native SHA256',hashlib.sha256(native.read_bytes()).hexdigest(),flush=True)
run(r.parent/'strip_ctf.py',native,r/('k'+v+'.noctf'))
run(r/'emit_prelink.py','--kernel',r/('k'+v+'.noctf'),
    '--prelinked',r/'kernelcache.fileset.macho',
    '--root','com.apple.filesystems.apfs',
    '--root','com.apple.driver.AppleMobileFileIntegrity',
    '--root','com.apple.AppleFSCompression.AppleFSCompressionTypeZlib',
    '--slide=-0x1effcdc8000',
    '--early-security-init','com.apple.driver.AppleMobileFileIntegrity=0xfffffff005703c54',
    '--output',r/('prelink-'+v+'.bin'))
run(r.parent/'check_loader.py',r/('prelink-'+v+'.bin'))
run(h/'driver-standardization/audit-storage-abi.py','--prelink',r/('prelink-'+v+'.bin'),
    '--bindings',h/'driver-standardization/abi/KebabPrelinkBindings.cpp',
    '--header',h/'driver-standardization/abi/IOBlockStorageDevice.h',
    '--module-path',r,'--output',h/('driver-standardization/abi/storage-'+v+'.json'))
config=(r/('unixnu-'+old+'-ro.toml')).read_text()
config=config.replace('kebab_driver_test=1 ','')
for key in a.without_boot_arg:
    assert key and all(c.isalnum() or c=='_' for c in key)
    config=re.sub(r'(?<![\w])'+re.escape(key)+r'=[^ "\n]+\s*','',config)
line=next(x for x in config.splitlines() if x.startswith('title ='))
config=config.replace(line,'title = "kebab APFS read-only: standardized IOKit contracts'+(' selftest' if a.selftest else '')+'"',1)
if a.selftest:config=config.replace('args = "','args = "kebab_driver_test=1 ',1)
for argument in a.boot_arg:
    assert argument and all(c.isalnum() or c in '_=,x.-' for c in argument)
    config=config.replace('args = "','args = "'+argument+' ',1)
(r/('unixnu-'+v+'-ro.toml')).write_text(config,newline='\n')
run(h/'pack-payload.py',r/('payload-'+old+'-ro.img'),r/('prelink-'+v+'.bin'),
    r/('payload-'+v+'-ro.tmp.img'),r/('unixnu-'+v+'-ro.toml'))
owner_input=r/('payload-'+v+'-ro.tmp.img')
if a.adt_patch:
    patched=r/('payload-'+v+'-ro.adt.img')
    run(a.adt_patch,owner_input,patched)
    owner_input=patched
run(r/'refresh_owner_manifest.py',owner_input,r/('payload-'+v+'-ro.img'),
    'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7')
run(h/'pack-boot.py',h/'signed.img',r/('payload-'+v+'-ro.img'),r/('boot-'+v+'-ro.img'))
run(r/'verify-0187-install.py','--version',v,'--boot-only')
