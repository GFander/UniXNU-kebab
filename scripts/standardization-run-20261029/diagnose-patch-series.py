"""Keep a scratch, strict replay at its first failure for patch-chain repair."""
from pathlib import Path
import json,subprocess,tempfile
repo=Path('/home/albert/kebab-dev-ios16');source=Path('/home/albert/ios-work/xnu8796/.git')
scratch=Path(tempfile.mkdtemp(prefix='hios-series-',dir=Path.home()/'.cache'))
subprocess.run(['git','clone','-q','--shared','--no-checkout',str(source),str(scratch)],check=True)
subprocess.run(['git','-C',str(scratch),'checkout','-q','1b191cb'],check=True)
applied=[];record={'scratch':str(scratch),'base':'1b191cb','applied':applied}
for patch in sorted((repo/'patches/xnu-8796-kebab').glob('*.patch')):
    command=['git','-C',str(scratch),'apply','--whitespace=nowarn']
    check=subprocess.run(command+['--check',str(patch)],capture_output=True,text=True)
    if check.returncode:
        record.update(failed=patch.name,error=check.stdout+check.stderr)
        print(patch.name,record['error'],flush=True);break
    subprocess.run(command+[str(patch)],check=True,capture_output=True)
    applied.append(patch.name)
else:record['passed']=True
out=Path('/home/albert/hios-series-diagnosis.json');out.write_text(json.dumps(record,indent=2)+'\n')
print('Scratch',scratch,'strictly applied',len(applied),'patches; record',out,flush=True)
