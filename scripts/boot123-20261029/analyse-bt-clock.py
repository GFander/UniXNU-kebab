"""Qualify the real fixed SE6 clock prerequisite, never Bluetooth/HCI functionality."""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('--source');a=p.parse_args()
r=Path(__file__).resolve().parent;v=a.version;source=a.source or v
raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(v+'-capture-provenance.json')).read_text())['sha256']==digest
assert not re.search(r'panic\(cpu|KEBAB PANIC: USB command|WATCHDOG CRITICAL:',s)
flash=json.loads((r/(v+'-flash.json')).read_text())
assert hashlib.sha256((r.parent/'p1-run/prelink-fix'/('kernel-'+v+'.kebab')).read_bytes()).hexdigest()==flash['kernel_sha256']
assert json.loads((r/('all-source-replay-'+source+'.json')).read_text())['all_source_equal']
regression=json.loads((r/(v+'-defaults-qualified.json')).read_text())
assert regression['actual_log_sha256']==digest and regression['native_sha256']==flash['kernel_sha256']
pattern=r'KEBAB BTCLK: error=(\d+) before=([0-9a-f]{8}) after=([0-9a-f]{8}) added=([0-9a-f]{8}) owned=([0-9a-f]{8}) polls=(\d+) attempted=(\d+) restored=(\d+) quarantined=(\d+)'
calls=[];required=(1<<16)|(1<<6)|(1<<7)
for values in re.findall(pattern,s):
 error,before,after,added,owned,polls,attempted,restored,dead=values
 q=dict(error=int(error),before=int(before,16),after=int(after,16),added=int(added,16),owned=int(owned,16),polls=int(polls),attempted=int(attempted),restored=int(restored),quarantined=int(dead))
 assert not q['error'] and not q['quarantined'] and 1<=q['polls']<=200
 assert q['added']==(required&~q['before']) and q['after']==(q['before']|required)
 assert not ((q['before']^q['after'])&~required) and (q['owned']&q['added'])==q['added']
 calls.append(q)
assert len(calls)>=2 and calls[0]['added']==1<<16 and calls[0]['attempted']==1
assert all(q['added']==0 and not q['attempted'] and q['owned']==calls[0]['owned'] for q in calls[1:])
snapshots=[]
for block in s.split('KEBAB BTHW: fixed SE6/NORTH read-only snapshot ')[1:]:
 end=block.index('KEBAB BTHW: no GPIO/clock/power/controller writes');block=block[:end]
 header=re.match(r'error=(\d+) valid=([0-9a-f]+)/([0-9a-f]+)/([0-9a-f]+)',block);assert header
 gpios={int(pin):(int(ctl,16),int(io,16)) for pin,ctl,io in re.findall(r'KEBAB BTHW: GPIO(\d+) ctl=([0-9a-f]{8}) io=([0-9a-f]{8})',block)}
 clocks={int(off,16):int(val,16) for off,val in re.findall(r'KEBAB BTHW: GCC offset=([0-9a-f]+) value=([0-9a-f]{8})',block)}
 serial={int(off,16):int(val,16) for off,val in re.findall(r'KEBAB BTHW: SE6 offset=([0-9a-f]+) value=([0-9a-f]{8})',block)}
 assert set(gpios)=={16,17,18,19,21,124}
 snapshots.append(dict(error=int(header[1]),valid=[int(x,16) for x in header.groups()[1:]],gpio=gpios,clock=clocks,serial=serial))
assert len(snapshots)>=3 and snapshots[0]['valid']==[0xfff,0x1ff,0] and snapshots[0]['error']!=0
initial=snapshots[0]
for q in snapshots[1:]:
 assert q['error']==0 and q['valid']==[0xfff,0x1ff,0xfff]
 assert q['clock'][0x52008]==calls[0]['after'] and not q['clock'][0x1772c]&(1<<31)
 assert q['clock'][0x17008]&(1<<1), 'real slave AHB HWCG mode must remain intact'
 assert q['serial'][0x68]==0x203 and q['serial'][0x6c]==0x203
 assert q['serial'][0x40]==0 and q['serial'][0x800]==0 and q['serial'][0x804]==0
 assert q['serial'][0xe24]==0x20102864 and q['serial'][0xe28]==0x20204800
 assert all(q['gpio'][pin][0]==initial['gpio'][pin][0] for pin in initial['gpio'])
 assert q['gpio'][21]==initial['gpio'][21] and q['gpio'][124]==initial['gpio'][124]
assert s.count('TX FIFO depth=16 words width=32 bits; RX depth=32 words width=32 bits')>=2
q=dict(version=v,source_version=source,native_sha256=flash['kernel_sha256'],actual_log_sha256=digest,
       clock_prerequisite_qualified=True,calls=calls,only_new_bit16_acquired=True,
       hardware_gated_slave_ahb_preserved=True,unchanged_gpio_and_controller_reset=True,
       actual_geni_protocol=2,geni_protocol_name='UART',geni_microcode_revision=3,
       tx_fifo_depth_words=16,rx_fifo_depth_words=32,fifo_word_bits=32,
       systembag_and_hardware_c_d_preserved=regression['systembag_and_hardware_c_d_preserved'],
       original_watchdog_enabled=regression['watchdog_enabled'],explicit_console_invocation=True,
       automatic_driver_start=False,controller_hci_communication=False,bluetooth_functional=False,
       touch_transport_or_input_requalified=False,full_springboard=False)
(r/(v+'-bt-clock-qualified.json')).write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
