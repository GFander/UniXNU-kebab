"""Qualify real SMEM_PROJECT135 and actual TCSR ownership on a normal boot.
This does not qualify board-specific rail votes or a Bluetooth controller.
"""
from pathlib import Path
import hashlib,json,re,subprocess,sys
r=Path(__file__).resolve().parent;version,source=sys.argv[1:]
assert all(len(x)==4 and x.isdigit() for x in (version,source))
raw=(r/(version+'-media-boot.log')).read_bytes();text=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(version+'-capture-provenance.json')).read_text())['sha256']==digest
assert 'BTBOARD completion timeout' not in text and not re.search(r'panic\(cpu',text)
subprocess.run([sys.executable,str(r/'analyse-defaults-lifecycle.py'),version],check=True,stdout=subprocess.DEVNULL)
normal=json.loads((r/(version+'-defaults-qualified.json')).read_text());assert not normal['panic']
observations=[];last=[]
pattern=r'KEBAB BTBOARD: SMEM=80900000\+200000 item=135 version=([0-9a-f]{8}) error=0\n([\s\S]*?)KEBAB BTBOARD: end error=0 fixed board metadata only; no SMEM/rail/GPIO writes'
for match in re.finditer(pattern,text):
 info=re.search(r'KEBAB BTBOARD: OCDT=(\d+) project=(\d+) dtsi=(\d+) bytes=(\d+) cached=([01])',match[2]);assert info
 lock=re.search(r'KEBAB BTBOARD: TCSR3 attempts=(\d+) acquired=(\d+) released=(\d+) owner=(\d+)/(\d+) reads=(\d+) last=([0-9a-f]+) parse=(-?\d+) cleanup=(-?\d+) quarantined=(\d+)',match[2]);assert lock
 attempts,acquired,released,before,after,reads,offset,parse,cleanup,quarantined=lock.groups()
 assert acquired==released=='1' and before!='1' and after!='1' and parse==cleanup==quarantined=='0'
 assert 1<=int(attempts)<=1000 and 3<=int(reads)<=16384
 ocdt,project,dtsi,record_bytes,cached=map(int,info.groups())
 assert ocdt in (1,2) and project>0 and record_bytes>=12 and int(match[1],16)>>16 in (11,12)
 observations.append(dict(smem_version=match[1],ocdt_version=ocdt,project=project,dtsi=dtsi,record_bytes=record_bytes,cached=bool(cached)))
 last.append(match.end())
assert len(observations)>=3 and all(x==observations[0] for x in observations)
at=text.index('****** DONE MKB_INIT ****');late_errors=re.findall(r'KEBAB BTBOARD: end error=(\d+)',text[at:]);assert all(int(e)==0 for e in late_errors)
snapshots=[]
for m in re.finditer(r'KEBAB BTHW: fixed SE6/NORTH[\s\S]*?KEBAB BTHW: no GPIO/clock/power/controller writes',text):
 gpios=re.findall(r'KEBAB BTHW: GPIO(\d+) ctl=([0-9a-f]{8}) io=([0-9a-f]{8})',m[0]);clocks=re.findall(r'KEBAB BTHW: GCC offset=([0-9a-f]{5}) value=([0-9a-f]{8})',m[0]);assert len(gpios)==6 and len(clocks)==9
 snapshots.append((m.start(),gpios,clocks))
assert len(snapshots)>=2 and snapshots[0][0]<last[0] and snapshots[-1][0]>last[-1]
assert snapshots[0][1:]==snapshots[-1][1:]
result=dict(version=version,source_version=source,actual_board_metadata_qualified=True,
 actual_identity=observations[0],successful_queries=len(observations),
 actual_hardware_lock_acquired_and_released=True,gpio_and_gcc_exactly_preserved=True,
 original_root_mkb_c_d_defaults_watchdog_preserved=True,actual_log_sha256=digest,
 native_sha256=normal['native_sha256'],board_specific_rail_binding_qualified=False,
 bluetooth_functional=False,full_springboard=False)
(r/(version+'-bt-board-qualified.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
