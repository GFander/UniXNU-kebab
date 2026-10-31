"""Scope: actual original AKS102/103 state flag and preserved boot substrate."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parent
def load(n):return json.loads((r/n).read_text())
test=load('test-aks-notification-state-result.json');abi=load('original-aks-notify-abi-qualified.json')
assert test['passed'] and test['behavioral_mutations_refused']==3 and abi['qualified']
manifest=load('all-mac-source-0388.json');replay=load('all-source-replay-0388.json')
assert test['production_sha256']==manifest['files']['iokit/Kernel/arm/kebab_aks_userclient_impl.h']
assert (replay['common_patches'],replay['host_patches'],replay['all_source_files'],replay['all_source_equal'])==(270,4,5239,True)
boots=[]
for v in ('0388','0389'):
 raw=(r/(v+'-media-boot.log')).read_bytes();text=raw.decode();sha=hashlib.sha256(raw).hexdigest()
 assert sha==load(v+'-capture-provenance.json')['sha256']
 defaults=load(v+'-defaults-qualified.json');assert defaults['actual_log_sha256']==sha and not defaults['panic']
 assert defaults['defaults_matching_complete'] and defaults['watchdog_enabled'] and defaults['original_iokit_monitoring_preserved']
 assert defaults['systembag_and_hardware_c_d_preserved']
 native=hashlib.sha256((r.parent/'p1-run/prelink-fix'/('kernel-'+v+'.kebab')).read_bytes()).hexdigest()
 assert native==defaults['native_sha256']=='25ee2e0c11059b4c8249819d036339cf1fcb287d060ac492a63176a27fdd259d'
 events=re.findall(r'KEBAB AKS NOTIFY: selector=(102|103) provider-state=([01]); registration flag only',text)
 assert events and events[0]==('103','0') and ('102','1') in events
 assert all(state=='1' for sel,state in events[events.index(('102','1')):] )
 for sel,shape in ((102,'0/0'),(103,'0/1')):
  assert f'KEBAB AKS UC: selector={sel} scalar={shape} struct-in=0 result=0;' in text
  assert not re.search(rf'KEBAB AKS UC: selector={sel} [^\n]*result=e00002c7',text)
 assert defaults['watchdog_heartbeat_times'][-1]>=180
 assert 'KEBAB QSEE: key generation route=' not in text and 'FATAL KEYBAG ERROR' not in text
 boots.append(dict(version=v,actual_log_sha256=sha,native_sha256=native,state_events=events,
                   watchdog_last_heartbeat=defaults['watchdog_heartbeat_times'][-1],
                   root_cd_mkb_defaults_regression=True,kernel_panic=False,
                   actual_userland_post_set_read_observed=any(sel=='103' and state=='1' for sel,state in events),
                   class_b_failures_still_visible=text.count('AKS new_key failed for class = 2')))
q=dict(notification_state_scope_accepted=True,full_goal_complete=False,source=dict(common=270,host=4,files=5239,exact=True),
       original_abi=abi,host_tests=test,boots=boots,complete_reboot_resets_flag=True,
       hardware_crypto_changed=False,encrypted_data_reformatted=False,original_watchdog_preserved=True,
       host_cross_client_and_concurrent_state_tested=True,actual_userland_read_before_set_and_set_observed=True,
       real_notification_delivery_or_all_aks_claim=False,device_state_selectors_17_35_implemented=False,
       class_a_b_file_keys_implemented=False,bluetooth_functional=False,all_daemon_errors_fixed=False,
       most_recent_independent_readonly_data_fsck='0385; no new fsck claimed for this one-header state change',github_uploaded=False)
(r/'AKS-NOTIFY-ACCEPTED-0389.json').write_text(json.dumps(q,indent=2)+'\n')
print('PASS actual original102/103 0->1 in0388 and complete-reboot0389; preserved roots/C,D/MKB/defaults/WDT. Full goal remains incomplete.')
