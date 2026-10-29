from pathlib import Path
import json
import re
import subprocess
import sys
r=Path('D:/LOS/HiOS/p1-run/prelink-fix')
current,baseline=sys.argv[1:3]
def rows(path):
    return [re.sub(r'0xfffffe[0-9a-f]+','<kernel-va>',x[x.index('KEBAB '):])
            for x in path.read_text(errors='replace').splitlines() if 'KEBAB ' in x]
for part in ['clock','se','ping']:
    assert rows(r/f'{current}-touch-{part}.log')==rows(r/f'{baseline}-touch-{part}.log'),part
log=(r/f'{current}-normal-fastboot.log').read_text(errors='replace')
assert all(s in log for s in ['VFS_SYNC + SYNCHRONIZE CACHE ok', 'restart reason verified: read back 0x04',
    'IMEM read back 0x77665500 (MATCH)', 'PEHaltRestart(kPERestartCPU)'])
device=subprocess.check_output(['D:/TOOL/platform-tools/fastboot.exe','devices'],text=True)
assert re.search(r'c073a488\s+fastboot',device)
(r/f'{current}-transport-regression.json').write_text(json.dumps(dict(version=current,baseline=baseline,
    clock_se_ping_logs_unchanged=True,normal_fastboot_verified=True,device=device.strip(),
    touch_sampling_verified=False,limitation='Unpowered baseline ping times out and recovers; no touch report sample.'),indent=2)+'\n')
print('Real clock/SE/ping unchanged and normal FASTBOOT returned actual bootloader PASS')
