from pathlib import Path
import argparse
import re
import subprocess
import sys

p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('--count',type=int,default=24);a=p.parse_args()
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');failures=[]
for i in range(a.count):
    path=r/(a.version+'-stress-pon-'+str(i+1)+'.log')
    result=subprocess.run([sys.executable,str(r.parent/'console-cap.py'),'--send','PON?',
        '--log',str(path),'--ack-seconds','0.5'],capture_output=True,text=True)
    text=path.read_text(errors='replace') if path.exists() else ''
    passed=result.returncode==0 and all(s in text for s in [
        'PON subtype 0x0805 reads 0x04 / 0x04 -> STABLE',
        'SOFT_RB_SPARE 0x088f reads 0x00 / 0x00 -> STABLE','SPMI-3 PASS'])
    if not passed:failures.append(i+1)
    print(f'{i+1}/{a.count}: '+('PASS' if passed else 'FAIL'),flush=True)
assert not failures,('PON samples disagreed with expected hardware registers',failures)
print('PASS: no cross-register contamination with live 16ms key polling',flush=True)
