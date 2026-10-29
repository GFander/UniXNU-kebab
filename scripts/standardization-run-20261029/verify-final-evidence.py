"""Cross-check final source, image, configuration and independent device evidence."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).parent;images=r.parent/'p1-run/prelink-fix';v='0225'
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
m=read(r/'manifest-0225.json');mac=read(r/'series-repair/all-mac-source-0224.json')['files']
for n,record in m['files'].items():
    assert sha(r/'sources-0225'/n)==record['sha256']==mac[n],n
assert m['implementation_files']==25 and m['class_count']==19 and m['hardware_platform_groups']==9
assert sha(images/'kernel-0225.kebab')==sha(images/'kernel-0224.kebab')==m['native_sha256']
assert sha(images/'boot-0225-ro.img')==m['boot_sha256']
config=(images/'unixnu-0225-ro.toml').read_text()
for token in ['ufsrw=0','rootrw=0','kebab_apfs_probe=3']:assert token in config
for token in ['kebab_spmi_lockpanic','kebab_driver_test','kebab_scm_probe','kebab_software_aes']:assert token not in config
normal=read(images/'0225-regression.json');panic=read(images/'0224-regression.json');fwlog=read(images/'0224-fwlog-device.json')
assert normal['normal_sync_fastboot_verified'] and normal['preboot_reboot_sha_verified'] and normal['file_hashes_match']
assert panic['panic_fastboot_verified'] and panic['panic_fwlog_verified'] and panic['preboot_reboot_sha_verified']
assert 'normal SPMI lock held' in (images/'0224-panic.log').read_text()
assert fwlog['native_dump_requested']==65536 and fwlog['transport_payload_bytes_LF_normalized']==65536 and not fwlog['own_header_in_payload']
assert fwlog['normal_command_after_dump_verified']
for p in (r/'abi').glob('*.json'):assert read(p)['evidence_version']==v
replay=read(r/'series-repair/all-source-replay-0224.json')
assert replay['common_patches']==176 and replay['host_patches']==4 and replay['all_source_files']==5192 and replay['all_source_equal']
boot=(images/'0225-ro-boot.log').read_text();assert 'panic(cpu' not in boot
assert 'reboot-read bytes=65559 sha256=3f2f47b49455f2d5cce7d745829440ccf0e8f1beda6c5120641c25213f78c957 PASS' in boot
soak=read(images/'0225-soak.json');stable_log=read(images/'0225-fwlog-device.json')
assert soak['complete'] and len(soak['samples'])==4 and soak['samples'][-1]['elapsed_seconds']>=1080
assert len({tuple(s['pid1_identity_and_times']) for s in soak['samples']})==1
assert stable_log['transport_payload_bytes_LF_normalized']==65536 and not stable_log['own_header_in_payload']
assert stable_log['normal_command_after_dump_verified']
assert 'c073a488' in (images/'0225-soak-final-fastboot.txt').read_text()
assert 'current-slot: b' in (images/'0225-final-slot.txt').read_text()
assert 'VFS_SYNC + SYNCHRONIZE CACHE ok' in (images/'0225-soak-normal-fastboot.log').read_text()
result={'stable_version':v,'native_build_version':'0224','source_files_in_manifest':len(m['files']),
 'source_manifest_matches_actual_Mac_and_snapshot':True,'native_same_as_qualified_0224':True,
 'selftest_fault_and_experimental_bootargs_absent':True,'System_and_Preboot_readonly':True,
 'Preboot_reboot_SHA_verified':True,'System_file_SHA_verified':True,
 'normal_sync_fastboot':True,'held_normal_SPMI_lock_panic_FWLOG_fastboot':True,
 'stable_config_18minute_four_checkpoint_soak':True,'stable_config_64KiB_FWLOG':True,'final_state':'fastboot; active slot b',
 'full_64KiB_FWLOG_without_self_header':True,'all_source_replay':{'common':176,'host':4,'files':5192},
 'native_sha256':m['native_sha256'],'boot_sha256':m['boot_sha256'],
 'qualification_limits':['cpus=1','Data content protection and nonce selector2212 unimplemented','no full launchd/Cryptex/DSC/UI qualification']}
(r/'evidence-0225.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: final image/config,',len(m['files']),'source entries, ABI records, independent normal/panic/FWLOG and 5192-file replay evidence agree')
