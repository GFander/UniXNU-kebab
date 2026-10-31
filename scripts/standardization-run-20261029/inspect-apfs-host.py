"""Read-only inspection of the retained, already-deployed 20H392 APFS image."""
from pathlib import Path
import hashlib,json,plistlib,shutil,subprocess
r=Path('/Users/albertlee/kebab-build/apfs-20H392').resolve();image=r/'HiOS.userdata.work.raw'
out=r/'standardization-b1-inputs';out.mkdir(exist_ok=True)
mp=out/'system-mount';mp.mkdir(exist_ok=True);assert not list(mp.iterdir())
assert image.is_file() and not image.is_symlink() and image.stat().st_size==241008750592
attached=plistlib.loads(subprocess.check_output(['hdiutil','attach','-nomount','-readonly','-imagekey','diskimage-class=CRawDiskImage','-plist',str(image)]))
devices={x['dev-entry'] for x in attached['system-entities'] if 'dev-entry' in x}
inventory=plistlib.loads(subprocess.check_output(['hdiutil','info','-plist']))
owners=[x for x in inventory['images'] if Path(x.get('image-path','')).resolve()==image]
assert len(owners)==1 and devices<={x['dev-entry'] for x in owners[0]['system-entities'] if 'dev-entry' in x}
base=next(x['dev-entry'] for x in attached['system-entities'] if 'dev-entry' in x)
try:
    data=plistlib.loads(subprocess.check_output(['diskutil','apfs','list','-plist']))
    container,=[c for c in data['Containers'] if c.get('APFSContainerUUID')=='0E25F331-6279-458A-9CFE-F39598DA17AA']
    volume,=[x for x in container['Volumes'] if x['APFSVolumeUUID']=='DADD93F8-DBBF-4B17-A1DF-0F11E64172F7']
    device='/dev/'+volume['DeviceIdentifier'];assert device in devices
    subprocess.run(['diskutil','mount','readOnly','-mountPoint',str(mp),device],check=True)
    assert hashlib.sha256((mp/'sbin/launchd').read_bytes()).hexdigest()=='12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c'
    files={}
    for name in ['sbin/mount_apfs','sbin/mount','System/Library/Filesystems/apfs.fs/mount_apfs']:
        source=mp/name
        if source.is_file():
            target=out/(name.replace('/','_'));shutil.copyfile(source,target)
            files[name]={'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    var=mp/'private/var';entries=[]
    for name in ['private/var','private/preboot','private/var/tmp','System/Volumes/Data','usr/standalone/firmware']:
        x=mp/name;entries.append({'path':name,'exists':x.exists(),'symlink':x.is_symlink(),'mode':oct(x.lstat().st_mode) if x.exists() or x.is_symlink() else None})
    report={'read_only':True,'container':container,'copied':files,'paths':entries,'initial_var':sorted(x.name for x in var.iterdir()) if var.is_dir() else []}
    (out/'inspection.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'copied':files,'paths':entries,'initial_var':report['initial_var']},indent=2))
finally:
    # Device identity was checked against this exact attached retained image.
    subprocess.run(['hdiutil','detach',base],check=True)
