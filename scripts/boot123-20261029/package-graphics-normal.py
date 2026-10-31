"""Package a normal graphics candidate without changing storage/key policy."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time,tomllib
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('previous');p.add_argument('--hid-roots',action='store_true');p.add_argument('--input-mask',type=int,choices=range(4));a=p.parse_args()
v,previous=a.version,a.previous;assert len(v)==4 and v.isdigit() and int(v)>int(previous)
native=b/('kernel-'+v+'.kebab');assert native.is_file()
assert not (b/('prelink-'+v+'.bin')).exists(),'Do not overwrite an existing prelink candidate'
text=tomllib.loads((b/('unixnu-'+previous+'-ro.toml')).read_text())['entry'][0]['args']
text,n=re.subn(r'kebab_qsee_utc=\d+','kebab_qsee_utc='+str(int(time.time())),text);assert n==1
assert 'kebab_owner_fast=1' in text and 'ufsrw=1' in text and 'rootrw=0' in text and 'kebab_apfs_tool=12' in text
assert len(text.encode())<608
args=r/(v+'-template-args.txt');args.write_text(text+'\n',newline='\n')
def run(*args):subprocess.run([sys.executable,*map(str,args)],cwd=b,check=True)
run(r/'qualify-original-watchdog-abi.py',v)
roots=['com.apple.filesystems.apfs','com.apple.driver.AppleMobileFileIntegrity','com.apple.AppleFSCompression.AppleFSCompressionTypeZlib','com.apple.driver.AppleARMWatchdogTimer','com.apple.security.sandbox']
if a.hid_roots:roots+=['com.apple.iokit.IOHIDFamily','com.apple.iokit.IOHIDEventDriver']
root_args=[arg for name in roots for arg in ('--root',name)]
run(b/'emit_prelink.py','--kernel',b/('k'+v+'.noctf'),'--prelinked',b/'kernelcache.fileset.macho',*root_args,'--slide=-0x1effcdc8000','--early-security-init','com.apple.driver.AppleMobileFileIntegrity=0xfffffff005703c54','--output',b/('prelink-'+v+'-unadapted.bin'))
run(r/'adapt-watchdog-sandbox-policy.py',b/('prelink-'+v+'-unadapted.bin'),b/('prelink-'+v+'.bin'))
meta=json.loads((b/('prelink-'+v+'-unadapted.json')).read_text());oldmeta=json.loads((b/('prelink-'+previous+'.json')).read_text())
assert set(meta['components'])==set(oldmeta['components'])|({'com.apple.iokit.IOHIDFamily'} if a.hid_roots else set())
if a.hid_roots:assert 'com.apple.iokit.IOHIDEventDriver' in meta['codeless_components']
run(b.parent/'check_loader.py',b/('prelink-'+v+'.bin'))
run(r.parent/'driver-standardization/audit-storage-abi.py','--prelink',b/('prelink-'+v+'.bin'),'--bindings',r.parent/'driver-standardization/abi/KebabPrelinkBindings.cpp','--header',r.parent/'driver-standardization/abi/IOBlockStorageDevice.h','--module-path',b,'--output',r.parent/('driver-standardization/abi/storage-'+v+'.json'))
config=(b/('unixnu-'+previous+'-ro.toml')).read_text()
config,n=re.subn(r'^args = .*$',lambda m:'args = '+json.dumps(text),config,count=1,flags=re.M);assert n==1
(b/('unixnu-'+v+'-ro.toml')).write_text(config,newline='\n')
run(r.parent/'pack-payload.py',b/('payload-'+previous+'-ro.img'),b/('prelink-'+v+'.bin'),b/('payload-'+v+'-ro.tmp.img'),b/('unixnu-'+v+'-ro.toml'))
temporary=b/('payload-'+v+'-ro.tmp.img')
if a.input_mask is not None:
 target=b/('payload-'+v+'-ro.hid.tmp.img')
 run(r/'adt-hid-input.py',temporary,target,'--mask',a.input_mask);temporary=target
run(b/'refresh_owner_manifest.py',temporary,b/('payload-'+v+'-ro.img'),'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7','--firmware-base',r/'signed-darwin22.img','--stable-os-namespace')
run(r.parent/'pack-boot.py',r/'signed-darwin22.img',b/('payload-'+v+'-ro.img'),b/('boot-'+v+'-ro.img'))
run(b/'verify-0187-install.py','--version',v,'--boot-only','--firmware-base',r/'signed-darwin22.img','--data-files-proof','template-userland')
run(r/'audit-aks-provider-abi.py',v)
print('Normal template preserved; native SHA256',hashlib.sha256(native.read_bytes()).hexdigest(),flush=True)
