"""Record existing boot glue, then prove the strict replay equals tested source."""
from pathlib import Path
import difflib,json,subprocess
repo=Path('/home/albert/kebab-dev-ios16');runtime=Path('/home/albert/s4/x')
state_path=Path('/home/albert/hios-series-diagnosis.json');state=json.loads(state_path.read_text())
assert state.get('passed');scratch=Path(state['scratch'])
comparison=json.loads(Path('/home/albert/series-repair-20261029/runtime-comparison.json').read_text())
names=[x['path'] for x in comparison['different']]
assert set(names)=={'bsd/dev/kebab_fwlog.c','bsd/dev/kebab_ufs.c','bsd/dev/kebab_usb.c','libkern/c++/OSKext.cpp','libsa/bootstrap.cpp'}
chunks=[]
for name in sorted(names):
    before=(scratch/name).read_text();after=(runtime/name).read_text()
    chunks.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name,n=3))
patch=repo/'patches/xnu-8796-kebab/0193-kebab-tested-boot-glue.patch';assert not patch.exists();patch.write_text(''.join(chunks))
command=['git','-C',str(scratch),'apply','--whitespace=nowarn']
subprocess.run(command+['--check',str(patch)],check=True);subprocess.run(command+[str(patch)],check=True)
assert all((scratch/name).read_bytes()==(runtime/name).read_bytes() for name in names)
state['applied'].append(patch.name);state['captured_runtime_glue']={'patch':patch.name,'files':sorted(names),
 'runtime_source_changed':False,'purpose':'Make existing RAM log, storage publication and prelink lifetime fixes reproducible.'}
state_path.write_text(json.dumps(state,indent=2)+'\n')
print('Captured existing 5-file boot glue without changing runtime source')
