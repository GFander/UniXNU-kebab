"""Qualify observation completeness, never daemon/hardware functionality."""
from pathlib import Path
from datetime import datetime
import hashlib, json, re

r = Path(__file__).resolve().parent
def load(n): return json.loads((r/n).read_text())
def sha(n): return hashlib.sha256((r/n).read_bytes()).hexdigest()
v = '0387'
provenance = load(v+'-capture-provenance.json')
assert sha(v+'-media-boot.log') == provenance['sha256']
defaults = load(v+'-defaults-qualified.json')
assert defaults['actual_log_sha256'] == provenance['sha256']
assert defaults['defaults_matching_complete'] and defaults['systembag_and_hardware_c_d_preserved']
assert defaults['watchdog_enabled'] and defaults['original_iokit_monitoring_preserved']
assert not defaults['panic']
replay = load('all-source-replay-0387.json')
assert (replay['common_patches'], replay['host_patches'], replay['all_source_files'], replay['all_source_equal']) == (269,4,5239,True)
snapshots = []
for suffix in ('a','b'):
    name = v+'-processes-'+suffix+'.log'
    text = (r/name).read_text()
    start = datetime.fromisoformat(re.search(r'^\[([^]]+)\] sending',text)[1])
    assert 'snapshot begin (cap512)' in text
    rows = re.findall(r'KEBAB PROC: pid=(\d+) parent=(\d+) state=(\d+) flags=(0x[0-9a-f]+) name=(\S+)',text)
    count = int(re.search(r'snapshot end count=(\d+)\b',text)[1])
    assert count == len(rows) < 512 and len({x[0] for x in rows}) == count
    snapshots.append(dict(file=name, sha256=sha(name), start=start.isoformat(), count=count,
                          processes={name:dict(pid=int(pid),parent=int(parent),state=int(state)) for pid,parent,state,flags,name in rows}))
elapsed = (datetime.fromisoformat(snapshots[1]['start'])-datetime.fromisoformat(snapshots[0]['start'])).total_seconds()
assert elapsed >= 60
names = ('ReportCrash','coreduetd','securityd','tccd','navd','lockdownd','backboardd','locationd','watchdogd','mediaserverd','configd')
stable = {}
for name in names:
    first,second = (s['processes'][name] for s in snapshots)
    assert first['pid'] == second['pid'] and first['state'] == second['state'] == 2
    stable[name] = first
exits = load(v+'-runtime-crashes.json')
assert exits['sha256'] == provenance['sha256'] and not exits['panic']
for name in ('bluetoothd','fairplayd.H2'):
    assert any(f'name={name} ' in s and 'signal=0 status=0 ' in s for s in exits['normal_first_exits'])
symbols = load(v+'-runtime-symbolicated.json')
assert any(s['name']=='bluetoothd' and s['depth']==0 and s.get('symbol')=='__exit' and s.get('offset')=='0x8' for s in symbols)
assert any(s['name']=='bluetoothd' and s['depth']==2 and s.get('symbol')=='_errx' for s in symbols)
for s in snapshots: del s['processes']
q = dict(version=v, observation_scope_accepted=True, full_goal_complete=False,
         source=dict(common=269,host=4,files=5239,exact=True), native_sha256=defaults['native_sha256'],
         boot_log_sha256=provenance['sha256'], snapshots=snapshots, snapshot_separation_seconds=elapsed,
         same_pids_observed=stable, ordinary_zero_status_exits=['bluetoothd','fairplayd.H2'],
         symbolication='original local symbols plus actual export trie; subtract four only for saved return addresses',
         preserved=['original watchdog actual heartbeats','hardware roots','encrypted Data C/D','original MKB_INIT','defaults lifecycle'],
         remaining=['real Bluetooth power/firmware/UART/HCI/iOS integration/scan/connect','AKS device-state and genuine A/B file-key services','other actual daemon failures','latest guide review, GPU/display/input and usable SpringBoard'],
         daemon_functionality_qualified=False, bluetooth_functional=False, raw_logs_published=False,
         last_readonly_data_fsck='0385 (unchanged substrate; no new filesystem check claimed)', github_uploaded=False)
(r/'DAEMON-LIFECYCLE-ACCEPTED-0387.json').write_text(json.dumps(q,indent=2)+'\n')
print(json.dumps({k:q[k] for k in ('version','observation_scope_accepted','snapshot_separation_seconds','daemon_functionality_qualified','bluetooth_functional')},indent=2))
