"""Save the tested, gated series and replay it independently; no Git mutation."""
from pathlib import Path
import subprocess
r=Path(__file__).parent
paths=[r/'software-aes/0187-kebab-software-aes-20H392.patch',r/'aes-kat-trace/0188-kebab-aes-kat-diagnostics.patch',
 r/'static-upl/0189-kebab-iomd-source-static-upl.patch',r/'aes-gate/0190-kebab-software-aes-opt-in.patch']
for p in paths:
    assert p.is_file() and p.stat().st_size>100
    target='/home/albert/kebab-dev-ios16/patches/xnu-8796-kebab/'+p.name
    exists=subprocess.run(['ssh','ubuntualbert','test -e '+target]).returncode==0
    if exists:raise SystemExit('Already present; inspect before replay: '+p.name)
    subprocess.run(['scp',str(p),'ubuntualbert:'+target],check=True)
    mac='/Users/albertlee/kebab-build/'+p.name
    subprocess.run(['scp',str(p),'albertlee@192.168.30.165:'+mac],check=True)
    subprocess.run(['ssh','albertlee@192.168.30.165',
        'cd /Users/albertlee/kebab-build/verify-0181 && patch --dry-run -p1 < '+mac+' && patch -p1 < '+mac],check=True)
    print('Saved and replayed',p.name,flush=True)
subprocess.run(['ssh','ubuntualbert','mkdir -p /home/albert/kebab-dev-ios16/licenses'],check=True)
subprocess.run(['scp',str(r/'SoftwareAES-LICENSE'),'ubuntualbert:/home/albert/kebab-dev-ios16/licenses/SoftwareAES.txt'],check=True)
