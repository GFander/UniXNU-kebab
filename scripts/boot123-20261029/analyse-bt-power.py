"""Require real fixed-resource reports on a healthy original normal boot.
Read-only discovery does not qualify regulator votes, controller or RF.
"""
from pathlib import Path
import hashlib, json, re, subprocess, sys
r=Path(__file__).resolve().parent;v,source=sys.argv[1:]
assert all(len(x)==4 and x.isdigit() for x in (v,source))
raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(v+'-capture-provenance.json')).read_text())['sha256']==digest
assert 'BTPWR completion timeout' not in s and not re.search(r'panic\(cpu',s)
subprocess.run([sys.executable,str(r/'analyse-defaults-lifecycle.py'),v],check=True,stdout=subprocess.DEVNULL)
normal=json.loads((r/(v+'-defaults-qualified.json')).read_text());assert not normal['panic']
reports=[];ends=[]
pattern=r'KEBAB BTPWR: cmd-db=80860000\+20000 version=1 standalone=([01]) error=0\n([\s\S]*?)KEBAB BTPWR: end error=0 read-only; board binding and rail ownership not established'
for m in re.finditer(pattern,s):
 rows=re.findall(r'KEBAB BTPWR: resource=(smp[a-z]\d+) address=([0-9a-f]{8}) slave-version=([0-9a-f]{4}) aux-bytes=(\d+) matches=(\d+)',m[2])
 assert [x[0] for x in rows]==['smpa5','smpa6','smpc8'],rows
 addresses=[int(x[1],16) for x in rows]
 assert all(int(x[4])>=1 for x in rows)
 assert len(set(addresses))==3 and all(x&0xffff0000==0x40000 and not(x&255) for x in addresses)
 reports.append(dict(standalone=int(m[1]),resources=[dict(name=n,address=a,slave_version=sv,auxiliary_bytes=int(size),equivalent_matches=int(matches)) for n,a,sv,size,matches in rows]));ends.append(m.end())
assert len(reports)>=2 and all(q==reports[0] for q in reports),reports
# Snapshot groups include actual GPIO and GCC values even when serial clocks
# are correctly disabled and the SE6 register read is deferred.
snapshots=[]
for m in re.finditer(r'KEBAB BTHW: fixed SE6/NORTH[\s\S]*?KEBAB BTHW: no GPIO/clock/power/controller writes',s):
 gpios=re.findall(r'KEBAB BTHW: GPIO(\d+) ctl=([0-9a-f]{8}) io=([0-9a-f]{8})',m[0])
 clocks=re.findall(r'KEBAB BTHW: GCC offset=([0-9a-f]{5}) value=([0-9a-f]{8})',m[0])
 assert len(gpios)==6 and len(clocks)==9
 snapshots.append((m.start(),gpios,clocks))
assert len(snapshots)>=2 and snapshots[0][0]<ends[0] and snapshots[-1][0]>ends[-1]
assert snapshots[0][1:]==snapshots[-1][1:],'GPIO/GCC state changed across discovery'
result=dict(version=v,source_version=source,resource_discovery_qualified=True,
 scope='actual fixed read-only Bluetooth supply-name/accelerator discovery only',
 actual_log_sha256=digest,native_sha256=normal['native_sha256'],successful_reports=len(reports),
 actual_resources=reports[0],gpio_and_gcc_exactly_preserved=True,
 original_hardware_root_c_d_mkb_defaults_watchdog_preserved=True,
 controller_power_or_owned_rail_votes_qualified=False,controller_hci_qualified=False,
 bluetooth_functional=False,full_springboard=False)
(r/(v+'-bt-power-qualified.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
