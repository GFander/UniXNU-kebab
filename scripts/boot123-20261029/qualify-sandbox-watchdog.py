"""Accept the actual owner-policy/native watchdog contract, not all iOS services."""
from pathlib import Path
import json,hashlib,re,subprocess,sys
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
load=lambda n:json.loads((r/n).read_text());sha=lambda x:hashlib.sha256(x).hexdigest()
native=sha((b/'kernel-0383.kebab').read_bytes());assert native=='6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051'
source=load('all-source-replay-0383.json');assert source['all_source_equal'] and source['common_patches']==268 and source['host_patches']==4 and source['all_source_files']==5239
assert load('test-sandbox-platform-policy-result.json')['passed'] and load('test-sandbox-platform-policy-result.json')['cases']==407
test=load('test-platform-watchdog-result.json');assert test['passed'] and test['mutation_refusals']==3
assert test['source_sha256']==load('all-mac-source-0383.json')['files']['iokit/Kernel/arm/kebab_watchdog_impl.h']
observed={}
for v in ('0383','0384','0386'):
 raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=sha(raw)
 assert load(v+'-capture-provenance.json')['sha256']==digest
 assert sha((b/('kernel-'+v+'.kebab')).read_bytes())==native
 subprocess.run([sys.executable,str(r/'verify-owner-watchdog-policy.py'),v],check=True)
 sg=load(v+'-systembag-qualified.json');assert sg['actual_log_sha256']==digest and sg['stage_accepted']
 assert sg['old_c_d_proof_and_raw_ciphertext_unchanged'] and sg['hardware_root_and_durable_bank_preserved']
 matches=re.findall(r'KEBAB MACF SCOPE: pid=(\d+) task=pid \d+, watchdogd real=KebabPlatformWatchdog type=1 policy=0 unrelated=IOResources policy=1; original checks, no override',s)
 assert len(matches)==1 and s.count('KEBAB WATCHDOG: original user-client accepted type=1')==1
 assert 'KEBAB MACF OPEN:' not in s and 'IOUC IOWatchdogUserClient failed' not in s
 assert 'profile not found' not in s and 'WATCHDOG CRITICAL' not in s and 'panic(cpu' not in s
 assert 'KEBAB CRYPTEX LIBRARIES: original3 SHA/RO union + original lower dyld/root identity PASS (0)' in s
 beats=list(map(float,re.findall(r'\[\s*([\d.]+)\] KEBAB WATCHDOG METHOD: selector=1 scalar=0/0 struct=0/0 result=0x0',s)))
 assert len(beats)>=5 and beats[-1]-beats[0]>=30
 if v!='0386':
  d=load(v+'-defaults-qualified.json');assert d['actual_log_sha256']==digest and d['defaults_matching_complete']
  assert d['original_locked_observations'][-1][0]>=180
 exits=re.findall(r'KEBAB RUNTIME EXIT: name=(\S+)',s)
 assert all(n=='SpringBoard' for n in exits)
 observed[v]=dict(log_sha256=digest,real_watchdog_pid=int(matches[0]),heartbeat_times=beats,
  original_macf_target_allow=True,original_macf_unrelated_service_denied=True,
  hardware_roots_systembag_c_d_preserved=True,observed_first_signal_failures=exits,
  prelink_sha256=json.loads((b/('prelink-'+v+'.json')).read_text())['candidate_sha256'])
fsck=load('0385-full-data-fsck.json');assert fsck['accepted'] and not fsck['unexpected_warnings'] and fsck['original_signed_fsck_exit']==0
assert fsck['actual_log_sha256']==sha((r/'0385-media-boot.log').read_bytes())
q=dict(accepted=True,scope='Original Sandbox with explicit owner platform class mapping and unchanged original watchdog monitor',
 native_sha256=native,native_bytes=(b/'kernel-0383.kebab').stat().st_size,source=dict(common=268,host=4,files=5239,exact=True),
 policy=dict(readonly_data_bytes_changed=21,original_class='AppleARMWatchdogTimer',native_class='KebabPlatformWatchdog',
  original_executable_bytes_unchanged=True,apple_authenticated_policy=False,process_name_authorization=False,
  original_macf_return_override=False,unrelated_service_deny_physically_observed=True),
 watchdog_hardware_backend=False,original_watchdog_client_policy_and_heartbeats_preserved=True,
 normal_boots=observed,final_strict_data_fsck=fsck,all_daemon_functions_qualified=False,all_goal_complete=False,
 gpu_display_input_started=False,visible_usable_springboard=False,
 remaining=['Real A/B file-key service and original AKS selectors17/35/102/103','Observe normal/nonzero exits and full active process list: current PROC? truncates64','Remaining daemon functional verification before updated GPU/display/input guide review and implementation'],
 phone=dict(serial='c073a488',slot='b',boot_b='0386',mode='normal userspace; maintenance profile removed'),
 github_upload_required=True,github_upload_completed=False)
(r/'SANDBOX-WATCHDOG-ACCEPTED-0386.json').write_text(json.dumps(q,indent=2)+'\n')
print('ACCEPTED actual Sandbox/platform watchdog contract in0383/0384/0386; hardware C/D and strict Datafsck preserved. Full goal remains ACTIVE.')
