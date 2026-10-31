"""Repair a stale context line, preserving every added/removed runtime line."""
from pathlib import Path
import difflib,hashlib,json,subprocess
repo=Path('/home/albert/kebab-dev-ios16');record_path=Path('/home/albert/hios-series-diagnosis.json')
record=json.loads(record_path.read_text());scratch=Path(record['scratch']).resolve()
assert scratch.parent==Path.home()/'.cache' and scratch.name.startswith('hios-series-')
assert record['failed']=='0167-kebab-panic-fastboot.patch'
p=repo/'patches/xnu-8796-kebab'/record['failed'];old=p.read_text()
a=old.index('@@ -1830,14 +1844,18 @@');z=old.index('@@ -2953,7 +2971,7 @@',a)
block=old[a:z]
needle=' \t\t/*\n';assert block.count(needle)==1
new=old[:a]+block.replace(needle,' \t\tprintf("KEBAB PANIC: command 0x%x refused while panicking; it can "\n',1)+old[z:]
def mutations(text):return [x for x in text.splitlines() if x.startswith(('+','-')) and not x.startswith(('+++','---'))]
assert mutations(new)==mutations(old),'A runtime mutation changed'
candidate=Path('/home/albert/.cache/0167-fixed-context.patch');candidate.write_text(new)
subprocess.run(['git','-C',str(scratch),'apply','--check','--whitespace=nowarn',str(candidate)],check=True)
archive=Path('/home/albert/series-repair-20261029');archive.mkdir(exist_ok=True)
(archive/'0167-original.patch').write_text(old)
(archive/'0167-context-only.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='old-0167',tofile='fixed-0167')))
p.write_text(new)
record['context_repair']={'patch':p.name,'old_sha256':hashlib.sha256(old.encode()).hexdigest(),
 'new_sha256':hashlib.sha256(new.encode()).hexdigest(),'added_removed_runtime_lines_identical':True}
for patch in sorted((repo/'patches/xnu-8796-kebab').glob('*.patch')):
    if patch.name in record['applied']:continue
    check=subprocess.run(['git','-C',str(scratch),'apply','--check','--whitespace=nowarn',str(patch)],text=True,capture_output=True)
    if check.returncode:
        record.update(failed=patch.name,error=check.stdout+check.stderr);print(patch.name,record['error']);break
    subprocess.run(['git','-C',str(scratch),'apply','--whitespace=nowarn',str(patch)],check=True)
    record['applied'].append(patch.name)
else:
    record['passed']=True;record.pop('failed',None);record.pop('error',None)
record_path.write_text(json.dumps(record,indent=2)+'\n')
print('Strict replay applied',len(record['applied']),'passed',record.get('passed',False))
