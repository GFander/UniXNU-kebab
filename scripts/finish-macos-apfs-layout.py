#!/usr/bin/env python3
"""Build and inspect shared APFS volumes only inside our attached work image."""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess

root = Path('/Users/albertlee/kebab-build/apfs-20H392').resolve()
work = root / 'HiOS.userdata.work.raw'
expected_uuid = '0e25f331-6279-458a-9cfe-f39598da17aa'


def inventory():
    attached = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
    owner, = [x for x in attached['images'] if Path(x.get('image-path', '')).resolve() == work]
    devices = {e['dev-entry'] for e in owner['system-entities'] if 'dev-entry' in e}
    all_containers = plistlib.loads(subprocess.check_output(['diskutil', 'apfs', 'list', '-plist']))
    container, = [x for x in all_containers['Containers'] if x.get('APFSContainerUUID', '').lower() == expected_uuid]
    assert '/dev/' + container['ContainerReference'] in devices
    assert all('/dev/' + p['DeviceIdentifier'] in devices for p in container['PhysicalStores'])
    assert container['CapacityCeiling'] == 241008750592
    return container, devices


container, devices = inventory()
reference = '/dev/' + container['ContainerReference']
assert len(container['Volumes']) == 1
assert container['Volumes'][0]['APFSVolumeUUID'].lower() == 'dadd93f8-dbbf-4b17-a1df-0f11e64172f7'
for name, role in [('Data', 'D'), ('Preboot', 'B')]:
    inventory()  # The device reference must still belong to this exact file.
    result = subprocess.run(['diskutil', 'apfs', 'addVolume', reference, 'APFSX', name,
                             '-role', role, '-nomount'], capture_output=True, text=True)
    (root / ('add-' + name + '.txt')).write_text(result.stdout + result.stderr)
    print(result.stdout, result.stderr, flush=True)
    result.check_returncode()
container, devices = inventory()
system, = [v for v in container['Volumes'] if v['APFSVolumeUUID'].lower() == 'dadd93f8-dbbf-4b17-a1df-0f11e64172f7']
volume = '/dev/' + system['DeviceIdentifier']
assert volume in devices
subprocess.run(['diskutil', 'mount', 'readOnly', volume], check=True)
info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', volume]))
mount = Path(info['MountPoint'])
assert str(mount).startswith('/Volumes/')
files = {}
for name in ('sbin/launchd', 'usr/lib/dyld', 'System/Library/xpc/launchd.plist'):
    path = mount / name
    assert path.is_file()
    digest = hashlib.sha256()
    with path.open('rb') as f:
        while b := f.read(1024 * 1024):
            digest.update(b)
    files['/' + name] = dict(bytes=path.stat().st_size, sha256=digest.hexdigest())
print('read-only original System files:', json.dumps(files), flush=True)
inventory()
checked = subprocess.run(['diskutil', 'verifyVolume', reference], capture_output=True, text=True)
(root / 'verify-container.txt').write_text(checked.stdout + checked.stderr)
print(checked.stdout, checked.stderr, flush=True)
checked.check_returncode()
container, devices = inventory()
report = dict(work_image=str(work), container=container, files=files,
              host_verification=True, ios16_mount_verified=False, device_flash=False,
              cryptex_deployment_complete=False)
(root / 'host-layout.json').write_text(json.dumps(report, indent=2) + '\n')
physical = '/dev/' + container['PhysicalStores'][0]['DeviceIdentifier']
assert physical in devices
subprocess.run(['hdiutil', 'detach', physical], check=True)
print('Only the working image changed; it is now detached.', flush=True)
