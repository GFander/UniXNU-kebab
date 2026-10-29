"""Capture read-only device checks, compare bus baseline, then verify panic recovery."""
from pathlib import Path
import argparse
import json
import re
import subprocess
import sys
import time

p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('--compare')
p.add_argument('--extended',action='store_true')
p.add_argument('--normal-reset',action='store_true')
p.add_argument('--preboot-proof',action='store_true')
p.add_argument('--panic-fwlog',action='store_true');a=p.parse_args()
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');console=r.parent/'console-cap.py'
def send(command,name,seconds=3):
    path=r/(a.version+'-'+name+'.log')
    run=subprocess.run([sys.executable,str(console),'--send',command,'--log',str(path),
        '--ack-seconds',str(seconds)],text=True,capture_output=True)
    print(command,'captured',path.stat().st_size if path.exists() else 0,'bytes',flush=True)
    if run.returncode:
        print(run.stdout+run.stderr);raise SystemExit(run.returncode)
    return path.read_text(errors='replace')
def payload(s):
    rows=[]
    for line in s.splitlines():
        if 'KEBAB ' not in line:continue
        line=line[line.index('KEBAB '):]
        line=re.sub(r'0xfffffe[0-9a-f]+','<kernel-va>',line)
        rows.append(line)
    return rows
observations={}
for cmd,name in [('SPMI?','spmi'),('PON?','pon'),('TCS?','tcs')]+([
    ('PWR?','power'),('TOUCH?','touch')] if a.extended else []):
    value=send(cmd,name);assert payload(value),(cmd,'empty result')
    observations[name]=payload(value)
    if a.compare:
        baseline=(r/(a.compare+'-'+name+'.log')).read_text(errors='replace')
        assert payload(value)==payload(baseline),(name,'changed hardware diagnostic',payload(value),payload(baseline))
for path,label,size,sha in [('/sbin/launchd','launchd',541600,'12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c'),
    ('/usr/lib/dyld','dyld',922912,'5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757')]:
    got=send('INST CHECK '+path,'check-'+label)
    assert f'INST CHECK {path} {size} {sha}' in got
boot=(r/(a.version+'-ro-boot.log')).read_text(errors='replace')
assert 'BSD root: disk1s1, major 1, minor 2' in boot
assert 'Darwin Ignition Sequence Version 1.0.0' in boot and 'panic(cpu' not in boot
sample=re.search(r'first sample pon=(0x\w+) gpio6=(0x\w+) gpio7=(0x\w+) ident=(0x\w+)',boot)
if a.compare:
    old=(r/(a.compare+'-ro-boot.log')).read_text(errors='replace')
    before=re.search(r'first sample pon=(0x\w+) gpio6=(0x\w+) gpio7=(0x\w+) ident=(0x\w+)',old)
    assert sample and before and sample.groups()==before.groups()
    print('Key raw sample unchanged:',sample.groups(),flush=True)
if a.preboot_proof:
    proof=json.loads((r.parents[1]/'standardization-run-20261029/apfs-write-proof/proof.json').read_text())
    assert 'readonly=1 System-readonly=1 identity=PASS' in boot
    assert f'reboot-read bytes={proof["size"]} sha256={proof["sha256"]} PASS' in boot
if a.normal_reset:
    reset=send('FASTBOOT','normal-fastboot',3)
    expected=['VFS_SYNC + SYNCHRONIZE CACHE ok','PEHaltRestart(kPERestartCPU)']
else:
    panic=send('PANIC!','panic',5)
    assert re.search(r'kebab usb console test(?: \(normal SPMI lock held\))? @kebab_[a-z_]+\.c:',panic)
    if a.panic_fwlog:
        log=send('FWLOG?','panic-fwlog',5)
        assert 'KEBAB FWLOG: ---- begin ----' in log and 'KEBAB FWLOG: ---- end ----' in log
        assert 'kebab usb console test' in log and 'dump already active' not in log
    reset=send('FASTBOOT','panic-fastboot',3)
    expected=['panic PSCI SYSTEM_RESET','skipping filesystem sync']
assert all(x in reset for x in expected+['restart reason verified: read back 0x04',
    'IMEM read back 0x77665500 (MATCH)'])
end=time.monotonic()+20
while time.monotonic()<end:
    devices=subprocess.check_output(['D:/TOOL/platform-tools/fastboot.exe','devices'],text=True)
    if re.search(r'c073a488\s+fastboot',devices):break
    time.sleep(.5)
else:raise SystemExit('reset issued, but fastboot device did not reappear')
(r/(a.version+'-final-fastboot.txt')).write_text(devices)
evidence=dict(version=a.version,compare=a.compare,observations=observations,
    key_raw=list(sample.groups()) if sample else None,root_readonly=True,
    file_hashes_match=True,panic_fastboot_verified=not a.normal_reset,
    normal_sync_fastboot_verified=a.normal_reset,preboot_reboot_sha_verified=a.preboot_proof,device=devices.strip())
evidence['panic_fwlog_verified']=a.panic_fwlog and not a.normal_reset
(r/(a.version+'-regression.json')).write_text(json.dumps(evidence,indent=2)+'\n')
print('PASS: real root, hashes, diagnostics, key sample and '+('normal sync/FASTBOOT' if a.normal_reset else 'panic FASTBOOT'),flush=True)
