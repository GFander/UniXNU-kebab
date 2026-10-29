#!/usr/bin/env python3
"""Inspect a disposable host image; every device must belong to this image."""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess

root = Path('/Users/albertlee/kebab-build/apfs-20H392').resolve()
source = root / 'OS.source.raw'
work = root / 'HiOS.userdata.work.raw'
assert source.is_file() and not source.is_symlink() and not work.exists()
digest = hashlib.sha256()
with source.open('rb') as f:
    while b := f.read(4 * 1024 * 1024):
        digest.update(b)
assert digest.hexdigest() == '0c638c90c6dbc7ab037dc932967f95ed97d6aee380d00e808d02e9c7260588b8'
subprocess.run(['cp', '-c', str(source), str(work)], check=True)
assert work.resolve().parent == root and work.is_file() and not work.is_symlink()
with work.open('r+b') as f:
    f.truncate(241008750592)
result = subprocess.run(['hdiutil', 'attach', '-nomount', '-readwrite', '-imagekey',
                         'diskimage-class=CRawDiskImage', '-plist', str(work)],
                        check=True, capture_output=True)
attachment = plistlib.loads(result.stdout)
(root / 'attachment.plist').write_bytes(result.stdout)
devices = [e['dev-entry'] for e in attachment['system-entities'] if 'dev-entry' in e]
print('attached only our working image:', devices, flush=True)
inventory = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
owner = [i for i in inventory['images'] if Path(i.get('image-path', '')).resolve() == work]
assert len(owner) == 1
assert set(devices) <= {e['dev-entry'] for e in owner[0]['system-entities'] if 'dev-entry' in e}
data = plistlib.loads(subprocess.check_output(['diskutil', 'apfs', 'list', '-plist']))
containers = [c for c in data['Containers'] if c.get('APFSContainerUUID', '').lower()
              == '0e25f331-6279-458a-9cfe-f39598da17aa']
assert len(containers) == 1
container = containers[0]
physical = container['PhysicalStores'] if 'PhysicalStores' in container else container['APFSPhysicalStores']
assert all('/dev/' + x['DeviceIdentifier'] in devices for x in physical)
print(json.dumps(container, indent=2), flush=True)
reference = '/dev/' + container['ContainerReference']
assert reference in devices
limits = subprocess.run(['diskutil', 'apfs', 'resizeContainer', reference, 'limits', '-plist'],
                        capture_output=True, text=True)
(root / 'resize-limits.txt').write_text(limits.stdout + limits.stderr)
print('resize limits exit:', limits.returncode, limits.stdout[-3000:], limits.stderr, flush=True)
report = dict(work_image=str(work), devices=devices, container=container,
              limits_exit=limits.returncode, device_flash=False)
(root / 'host-probe.json').write_text(json.dumps(report, indent=2) + '\n')
