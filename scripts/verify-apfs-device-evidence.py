#!/usr/bin/env python3
"""Correlate independent host files with actual device logs; no device writes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--inputs', required=True, type=Path)
parser.add_argument('--session', required=True, type=Path)
args = parser.parse_args()
host = json.loads((args.inputs/'host-layout.json').read_text())
observed = {}
for path,name in [('/sbin/launchd','launchd'),('/usr/lib/dyld','dyld')]:
    log = (args.session/f'0190-check-{name}.log').read_text(encoding='utf-8')
    match = re.search(r'INST CHECK '+re.escape(path)+r' (\d+) ([0-9a-f]{64})\b',log)
    assert match, f'Missing device result for {path}'
    value = dict(bytes=int(match[1]),sha256=match[2])
    assert value == host['files'][path], f'Device content differs for {path}'
    observed[path] = value
boot = (args.session/'0190-ro-boot.log').read_text(encoding='utf-8')
assert 'BSD root: disk1s1, major 1, minor 2' in boot
assert 'vol-uuid: DADD93F8-DBBF-4B17-A1DF-0F11E64172F7' in boot
assert 'bsd_init: calling VFS_ROOT' in boot
assert 'Darwin Ignition Sequence Version' in boot and 'libignition: 1:' in boot
assert 'panic(cpu' not in boot
status = (args.session/'0190-inst-status.log').read_text(encoding='utf-8')
assert 'active 0 dry 0 root ro' in status and 'entries 0/0' in status
early = (args.session/'0189-ro-fwlog.log').read_text(encoding='utf-8')
assert 'loaded external trust cache module: 0' in early
flash = (args.session/'0187-flash-userdata.log').read_text(encoding='utf-8')
assert len(re.findall(r"Sending sparse 'userdata'.*OKAY",flash)) == 22
assert len(re.findall(r"Writing 'userdata'.*OKAY",flash)) == 22
assert 'FAILED' not in flash and 'Finished. Total time:' in flash
boot_flash = (args.session/'0190-flash-boot.log').read_text(encoding='utf-8')
assert re.search(r"Sending 'boot_b'.*OKAY",boot_flash)
assert re.search(r"Writing 'boot_b'.*OKAY",boot_flash)
reset = (args.session/'0190-userspace-fastboot.log').read_text(encoding='utf-8')
assert 'IMEM read back 0x77665500 (MATCH)' in reset
report = dict(userdata_deployed=True, apfs_system_root_mount_verified=True,
              known_files=observed, genuine_pid1_early_ignition_observed=True,
              original_static_trust_cache_loaded_in_0189=True,
              kernel_storage_readonly=True, installer_write_entries=0,
              full_filesystem_hash_verified=False, root_seal_authenticated=False,
              data_persistence_verified=False, cryptex_assembled=False,
              complete_launchd_startup_verified=False, springboard_verified=False,
              boot_version='0190',boot_sha256=hashlib.sha256(
                  (args.session/'boot-0190-ro.img').read_bytes()).hexdigest(),
              automatic_fastboot_reset_requested=True,
              last_ignition_output='halt after stage : n/a; exact subsequent wait not yet diagnosed')
(args.inputs/'device-evidence-0190.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
print('Device APFS root, two complete original file hashes, early PID 1 and read-only state: PASS')
