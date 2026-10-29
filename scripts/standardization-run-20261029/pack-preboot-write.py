"""Create and verify an explicitly named, fixed-file Preboot write-test image."""
from pathlib import Path
import ast,copy,hashlib,json,plistlib,struct,subprocess,sys,xml.etree.ElementTree as ET
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');h=r.parent.parent;v=sys.argv[1]
assert len(v)==4 and v.isdigit()
source=(r/('unixnu-'+v+'-ro.toml')).read_text()
assert source.count('ufsrw=0')==source.count('kebab_apfs_probe=1')==1
config=source.replace('ufsrw=0','ufsrw=1').replace('kebab_apfs_probe=1','kebab_apfs_probe=2').replace('kebab_driver_test=1 ','')
tokens=ast.literal_eval(next(line.split('=',1)[1].strip() for line in config.splitlines() if line.startswith('args ='))).split()
assert all(t in tokens for t in ['ufsrw=1','rootrw=0','kebab_apfs_probe=2','ufsmedia_root=2'])
assert all(x not in config for x in ['kebab_driver_test=','kebab_software_aes=','kebab_aes_negative_kat=','kebab_data_mount='])
cfg=r/('unixnu-'+v+'-preboot-write.toml');cfg.write_text(config,newline='\n')
def run(*args):subprocess.run([sys.executable,*map(str,args)],cwd=r,check=True)
temporary=r/('payload-'+v+'-preboot-write.tmp.img');payload=r/('payload-'+v+'-preboot-write.img');boot=r/('boot-'+v+'-preboot-write.img')
run(h/'pack-payload.py',r/('payload-'+v+'-ro.img'),r/('prelink-'+v+'.bin'),temporary,cfg)
run(r/'refresh_owner_manifest.py',temporary,payload,'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7')
run(h/'pack-boot.py',h/'signed.img',payload,boot)
sys.path.insert(0,str(r.parent));from verify_image_lib import Fat32
def files(raw):
    fs=Fat32(raw);return {name:data for name,data,_,_ in fs.walk(fs.root)}
actual=boot.read_bytes();p=payload.read_bytes();base=(h/'signed.img').read_bytes()
assert len(actual)==100663296 and actual[:0x800000]==base[:0x800000]
assert actual[0x800000:0x800000+len(p)]==p and actual[0x800000+len(p):]==base[0x800000+len(p):]
f=files(p);old=files((r/('payload-'+v+'-ro.img')).read_bytes());assert set(f)==set(old)
config_key='EFI/UniXNU/unixnu.toml';adt_key='EFI/UniXNU/Payloads/kernel.macho.adt'
assert f[config_key]==cfg.read_bytes()
for name in f:
    if name not in [config_key,adt_key]:assert f[name]==old[name],name
assert f['EFI/UniXNU/Payloads/kernel.macho']==(r/('prelink-'+v+'.bin')).read_bytes()
tree=ast.parse((r/'refresh_owner_manifest.py').read_text())
codec=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('parse','encode')],type_ignores=[])
exec(compile(codec,'<ADT codec>','exec'))
node,end=parse(f[adt_key]);assert end==len(f[adt_key]) and encode(node)==f[adt_key]
def unmeasured(node):
    node=copy.deepcopy(node);chosen=next(c for c in node[1] if dict(c[0]).get('name')==b'chosen\0')
    chosen[0][:]=[(k,v) for k,v in chosen[0] if k not in ('owner-boot-manifest','boot-manifest-hash')]
    return encode(node)
oldnode,oldend=parse(old[adt_key]);assert unmeasured(node)==unmeasured(oldnode)
chosen=next(c for c in node[1] if dict(c[0]).get('name')==b'chosen\0');props=dict(chosen[0])
matcher=ET.fromstring(props['root-matching'].rstrip(b'\0'))
assert plistlib.loads(b'<plist version="1.0">'+ET.tostring(matcher)+b'</plist>')['IOPropertyMatch']['UUID']=='DADD93F8-DBBF-4B17-A1DF-0F11E64172F7'
manifest=json.loads(props['owner-boot-manifest']);assert hashlib.sha384(props['owner-boot-manifest']).digest()==props['boot-manifest-hash']
for key,data in [('kernel',f['EFI/UniXNU/Payloads/kernel.macho']),('boot-config',f[config_key]),('firmware-prefix',base[:0x800000]),
                 ('adt-without-owner-manifest',unmeasured(node)),('static-trust-cache-bundle',f['EFI/UniXNU/Payloads/kernel.macho.trustcache'])]:
    assert manifest['components'][key]==hashlib.sha256(data).hexdigest(),key
record={'version':v,'boot':str(boot),'boot_sha256':hashlib.sha256(actual).hexdigest(),'system_readonly':True,
 'ufs_writable':True,'probe_mode':2,'fixed_file':'/private/preboot/.hios-apfs-proof-v1','exclusive_create':True,
 'firmware_unchanged':True,'other_fat_files_unchanged':True,'adt_inputs_unchanged':True,'owner_measurements_valid':True,
 'userdata_redeployment':False,'device_write_not_yet_executed':True}
(h/'standardization-run-20261029'/('write-image-'+v+'.json')).write_text(json.dumps(record,indent=2)+'\n')
print('PASS: explicit Preboot write-test image, System RO, fixed-file gate, FAT/ADT/owner/firmware measurements')
