"""Read-only stability samples of the already-qualified configuration; no reset/write."""
from pathlib import Path
import argparse,datetime,json,re,subprocess,sys,time
ap=argparse.ArgumentParser();ap.add_argument('version');ap.add_argument('--minutes',type=float,default=18);a=ap.parse_args()
assert 0<a.minutes<=20
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');console=r.parent/'console-cap.py';start=time.monotonic();samples=[];identity=None
for i in range(4):
    due=start+a.minutes*60*i/3
    while time.monotonic()<due:time.sleep(min(5,due-time.monotonic()))
    sample={'index':i,'at':datetime.datetime.now().astimezone().isoformat(),'elapsed_seconds':round(time.monotonic()-start,2)}
    for command,label in [('PON?','pon'),('PID1?','pid1'),('INST CHECK /sbin/launchd','launchd'),('INST CHECK /usr/lib/dyld','dyld')]:
        path=r/(a.version+'-soak-'+str(i)+'-'+label+'.log')
        result=subprocess.run([sys.executable,str(console),'--send',command,'--log',str(path),'--ack-seconds','2'],text=True,capture_output=True,check=True)
        text=path.read_text(errors='replace');assert 'panic(cpu' not in text and 'refused while panicking' not in text
        if label=='pon':assert 'SPMI-3 PASS' in text
        elif label=='pid1':
            assert 'thread snapshot count=1 thread_total=1' in text
            match=re.search(r'tid=(\d+).*?wait=(0x\w+) continuation=(0x\w+) user=([\d.]+) sys=([\d.]+)',text);assert match
            current=match.groups()
            if identity is None:identity=current
            assert current==identity,('PID1 state changed; inspect rather than assuming a regression',identity,current)
            sample['pid1_identity_and_times']=current
        elif label=='launchd':assert '541600 12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c' in text
        else:assert '922912 5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757' in text
    samples.append(sample)
    (r/(a.version+'-soak.json')).write_text(json.dumps({'version':a.version,'complete':i==3,'samples':samples,'scope':'Four read-only checkpoints; not UI or full-system qualification.'},indent=2)+'\n')
    print('PASS read-only soak checkpoint',i,'elapsed',sample['elapsed_seconds'],'seconds; PID1 unchanged, PON and both full hashes match',flush=True)
print('PASS: four read-only stability checkpoints over',a.minutes,'minutes',flush=True)
