"""Require actual internal SE6 byte exchange and restoration; never RF success."""
from pathlib import Path
import argparse,hashlib,json,re
ap=argparse.ArgumentParser();ap.add_argument('version');ap.add_argument('--source');a=ap.parse_args()
r=Path(__file__).resolve().parent;v=a.version;source=a.source or v
raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(v+'-capture-provenance.json')).read_text())['sha256']==digest
normal=json.loads((r/(v+'-defaults-qualified.json')).read_text());assert normal['actual_log_sha256']==digest
assert not re.search(r'panic\(cpu|WATCHDOG CRITICAL:',s)
calls=re.findall(r'KEBAB BTUART: phase=(\d+) frames=(\d+) tx=(\d+) rx=(\d+) irq=([0-9a-f]+)/([0-9a-f]+) changed=(\d+) restored=(\d+) cleanup=(\d+) quarantined=(\d+)',s)
assert len(calls)>=3
for phase,frames,tx,rx,m,ss,changed,restored,cleanup,dead in calls:
 assert tuple(map(int,(phase,frames,tx,rx,changed,restored,cleanup,dead)))==(5,9,1225,1225,1,1,0,0)
ends=re.findall(r'KEBAB BTUART: end error=(\d+)([^\n]*)',s)
early=[x for x in ends if x==('35',' BSD startup incomplete; no register access')]
assert [x for x in ends if x not in early]==[('0',' internal-SE6-only; no controller/HCI/RF qualification')]*len(calls)
if early:assert s.index('BSD startup incomplete; no register access')<s.index('KEBAB BTCLK: error=')
ports=re.findall(r'KEBAB BTUART: port default-state=(\d+) actual-default-writes=(\d+)',s)
assert ports==[('1','1')]*len(calls)
assert s.count('LUCID mode=d810003d L=0000001f user=00000103 opmode=00000000 alpha=00004000 vote=00000001')==len(calls)
# Every clock-prep report is before each internal run; a final explicit BTHW
# report after the last run verifies the final restore independently too.
snapshots=[]
for part in s.split('KEBAB BTHW: fixed SE6/NORTH read-only snapshot ')[1:]:
 end=part.index('KEBAB BTHW: no GPIO/clock/power/controller writes');part=part[:end]
 assert part.startswith('error=0 valid=fff/1ff/fff')
 gpio={int(i):(int(ctl,16),int(io,16)) for i,ctl,io in re.findall(r'GPIO(\d+) ctl=([0-9a-f]{8}) io=([0-9a-f]{8})',part)}
 regs={int(off,16):int(value,16) for off,value in re.findall(r'GCC offset=([0-9a-f]+) value=([0-9a-f]+)',part)}
 se={int(off,16):int(value,16) for off,value in re.findall(r'SE6 offset=([0-9a-f]+) value=([0-9a-f]+)',part)}
 assert len(gpio)==6 and all((value[0]>>2)&15==0 for value in gpio.values())
 assert gpio[21][1]&2==0 and gpio[124][1]&2==0
 assert {off:regs[off] for off in (0x17734,0x17738,0x1773c,0x17740)}=={0x17734:0x100000,0x17738:0,0x1773c:0,0x17740:0}
 assert {off:se[off] for off in (0x40,0x48,0x4c,0x800,0x804,0x610,0x640)}=={0x40:0,0x48:0x11,0x4c:0,0x800:0,0x804:0,0x610:0,0x640:0}
 snapshots.append(gpio)
assert len(snapshots)>=len(calls)+1 and all(x==snapshots[0] for x in snapshots)
assert s.rindex('KEBAB BTHW: fixed SE6/NORTH')>s.rindex('KEBAB BTUART: end')
replay=json.loads((r/('all-source-replay-'+source+'.json')).read_text());assert replay['all_source_equal']
q=dict(version=v,native_sha256=normal['native_sha256'],actual_log_sha256=digest,internal_uart_qualified=True,early_bsd_refused_before_mmio=bool(early),probe_calls=len(calls),frames_per_call=9,bytes_each_direction_per_call=1225,partial_fifo_words=True,full_config_restored=True,final_readonly_snapshot=True,pins_and_bt_reset_unchanged=True,shared_pll_readonly=True,hardware_initialization_once_per_boot=True,actual_force_default_writes=1,source_version=source,common_patches=replay['common_patches'],source_files=replay['all_source_files'],controller_power_sequence_implemented=False,hci_controller_or_firmware_download_qualified=False,bluetooth_scan_connect_data_qualified=False,bluetooth_functional=False,hardware_roots_and_encrypted_data_preserved=True)
(r/(v+'-bt-uart-qualified.json')).write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
