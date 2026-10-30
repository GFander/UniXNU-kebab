"""Accept observed original MKB_INIT plus actual hardware-backed cold storage."""
from pathlib import Path
import json,hashlib,re,sys,tomllib,subprocess
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
v=sys.argv[1];assert v in ('0354','0355','0363','0364','0367','0368','0369','0370','0371','0372','0373','0375','0376','0377','0378','0379','0380','0382','0383','0384','0386','0387','0388','0389','0390','0391','0393','0394','0395','0396','0397','0399','0400','0401','0402','0403','0405')
raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(v+'-capture-provenance.json')).read_text())['sha256']==digest
mode='exclusive initialize' if v=='0354' else 'cold authenticate'
assert f'KEBAB SYSTEM BAG: {mode} original plist/real sealed bank PASS (0), bytes=4182 classD;' in s
assert '****** DONE MKB_INIT ****' in s and 'FATAL KEYBAG ERROR' not in s
assert re.search(r'\(usermanagerd\)[\s\S]{0,1024}<Notice>: Doing boot task',s)
assert 'KEBAB AKS UC: selector=14 scalar=0/1 struct-in=0 result=0' in s
assert 'KEBAB AKS UC: selector=65 scalar=2/0 struct-in=84 result=0' in s
assert 'authenticated system bag -> original Data factory C/D mapping result=0' in s
assert 'Successfully obtained the cookie for the volume with length:34' in s
assert 'selector=68 scalar=1/0' not in s and 'APFSVolumePayloadSet succeeded' not in s
assert 'No system keybag loaded.' not in s and 'SKIP INIT' not in s
assert 'Preboot nonce/Root/device-bank preserved=1' in s
flags=dict(x.split('=',1) for x in tomllib.loads((b/('unixnu-'+v+'-ro.toml')).read_text())['entry'][0]['args'].split() if '=' in x)
assert flags['ufsrw']=='1' and flags['rootrw']=='0' and flags['kebab_apfs_tool']=='12' and flags['kebab_owner_fast']=='1'
assert '-apfs_shared_datavolume' not in flags and '-apfs_shared_datavolume' not in tomllib.loads((b/('unixnu-'+v+'-ro.toml')).read_text())['entry'][0]['args']
old=json.loads((r/'0343-real-data-files-refresh.json').read_text());cipher={}
for cls in (3,4):
 plain=bytes((i*17+(i>>7)+cls*29)&255 for i in range(65559))
 assert f'cold-read class={cls} bytes=65559 original APFS protected IO sha256={hashlib.sha256(plain).hexdigest()} PASS' in s
 for off in (0,61440):
  hits=re.findall(rf'class={cls} logical={off} ciphertext-sha256=([0-9a-f]{{64}}) differs from plaintext',s);assert len(hits)==1;cipher[f'{cls}:{off}']=hits[0]
assert cipher==old['raw_ciphertext_stripes']
out={'version':v,'actual_log_sha256':digest,'stage_accepted':True,'original_keybagd_mkb_init_complete':True,
 'real_backend_systembag_disk_envelope':True,'disk_envelope_class_d_bytes':4182,'hardware_root_and_durable_bank_preserved':True,
 'original_apfs_persistent_cookie_read_and_authenticated_map':True,'cookie_regenerated_or_rewritten':False,
 'no_keybag_skip_or_empty_offline_success':True,'original_usermanagerd_task_started':True,
 'system_preboot_readonly':True,'old_c_d_proof_and_raw_ciphertext_unchanged':True,
 'native_sha256':hashlib.sha256((b/('kernel-'+v+'.kebab')).read_bytes()).hexdigest(),
 'full_applekeystore_compatibility':False,'apple_sep_or_passcode_ab_claim':False,'qualified_policy':'factory no-passcode C/D',
 'unimplemented_selectors_observed':sorted(set(int(x) for x in re.findall(r'selector=(\d+) scalar=[^\n]*result=e00002c7',s))),
 'later_panic':re.findall(r'panic\(cpu[^\n]+',s)[:1],
 'gpu_display_input_started':False,'springboard_qualified':False,'final_fsck_pending':True}
if v=='0355':
 previous=json.loads((r/'0354-systembag-qualified.json').read_text());assert previous['stage_accepted']
 assert previous['actual_log_sha256']==hashlib.sha256((r/'0354-media-boot.log').read_bytes()).hexdigest()
 out['complete_reset_cold_systembag_reuse_qualified']=True
(r/(v+'-systembag-qualified.json')).write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
