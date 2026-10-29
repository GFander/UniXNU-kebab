"""Canonicalize one inspected historical patch's stale edge context.

GNU patch's old output is retained, then a strict git diff must produce
byte-identical files from the same input. Never touch the running source tree.
"""
from pathlib import Path
import difflib,json,os,subprocess,sys
repo=Path('/home/albert/kebab-dev-ios16');rp=Path('/home/albert/hios-series-diagnosis.json')
record=json.loads(rp.read_text());name=sys.argv[1];assert record['failed']==name
scratch=Path(record['scratch']).resolve();assert scratch.parent==Path.home()/'.cache'
p=repo/'patches/xnu-8796-kebab'/name;original=p.read_text();paths=set();oldname=None
for line in original.splitlines():
    if line.startswith('--- '):oldname=line[4:].split('\t')[0]
    if line.startswith('+++ '):
        n=line[4:].split('\t')[0];n=oldname if n=='/dev/null' else n
        assert n.startswith(('a/','b/'));n=n[2:];f=(scratch/n).resolve()
        assert f.is_relative_to(scratch);paths.add(n)
before={n:(scratch/n).read_bytes() if (scratch/n).exists() else None for n in paths}
env=dict(os.environ,TMPDIR=str(Path.home()/'.cache'))
run=subprocess.run(['patch','-p1','--batch','--forward','--fuzz=2','--no-backup-if-mismatch'],
    input=original,text=True,capture_output=True,cwd=scratch,env=env)
print(run.stdout+run.stderr,flush=True)
if run.returncode:raise SystemExit('Do not normalize a failed GNU application; inspect retained scratch')
after={n:(scratch/n).read_bytes() if (scratch/n).exists() else None for n in paths}
archive=Path('/home/albert/series-repair-20261029')/name.removesuffix('.patch');archive.mkdir(parents=True,exist_ok=True)
(archive/'original.patch').write_text(original);(archive/'gnu-application.txt').write_text(run.stdout+run.stderr)
chunks=[]
for n in sorted(paths):
    for group,values in [('before',before),('after',after)]:
        if values[n] is not None:
            f=archive/group/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(values[n])
    chunks.extend(difflib.unified_diff((before[n] or b'').decode().splitlines(True),(after[n] or b'').decode().splitlines(True),
        fromfile='a/'+n if before[n] is not None else '/dev/null',tofile='b/'+n if after[n] is not None else '/dev/null',n=3))
    f=scratch/n
    if before[n] is None:f.unlink(missing_ok=True)
    else:f.write_bytes(before[n])
candidate=archive/'canonical.patch';candidate.write_text(''.join(chunks))
command=['git','-C',str(scratch),'apply','--whitespace=nowarn']
subprocess.run(command+['--check',str(candidate)],check=True)
subprocess.run(command+[str(candidate)],check=True)
assert after=={n:(scratch/n).read_bytes() if (scratch/n).exists() else None for n in paths}
p.write_text(candidate.read_text());record.setdefault('normalized_contexts',[]).append({'patch':name,
 'strict_output_identical_to_original_gnu_application':True,'archive':str(archive),'files':sorted(paths)})
record['applied'].append(name)
for patch in sorted((repo/'patches/xnu-8796-kebab').glob('*.patch')):
    if patch.name in record['applied']:continue
    check=subprocess.run(command+['--check',str(patch)],capture_output=True,text=True)
    if check.returncode:
        record.update(failed=patch.name,error=check.stdout+check.stderr);print(patch.name,record['error']);break
    subprocess.run(command+[str(patch)],check=True)
    record['applied'].append(patch.name)
else:
    record['passed']=True;record.pop('failed',None);record.pop('error',None)
rp.write_text(json.dumps(record,indent=2)+'\n')
print('Strict replay applied',len(record['applied']),'passed',record.get('passed',False))
