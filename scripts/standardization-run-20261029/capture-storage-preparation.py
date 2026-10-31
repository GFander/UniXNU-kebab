"""Capture the previously out-of-tree, already-tested storage build inputs."""
from pathlib import Path
import difflib,json,subprocess
repo=Path('/home/albert/kebab-dev-ios16');runtime=Path('/home/albert/s4/x');rp=Path('/home/albert/hios-series-diagnosis.json')
record=json.loads(rp.read_text());assert record['failed']=='0174-kebab-blockstorage-contracts.patch'
scratch=Path(record['scratch']);expected={}
name='iokit/Kernel/arm/KebabBlockStorageDevice.cpp'
expected[name]=(Path('/home/albert/series-repair-20261029/storage-inputs')/Path(name).name).read_text()
name='iokit/conf/files.arm64';s=(scratch/name).read_text()
old='# iokit/Kernel/arm/KebabBlockStorageDevice.cpp\toptional iokitcpp';assert s.count(old)==1
s=s.replace(old,old[2:]+'\niokit/Kernel/arm/KebabPrelinkBindings.cpp\toptional iokitcpp',1);expected[name]=s
for name in ['iokit/Kernel/arm/KebabPrelinkBindings.cpp',*[str(p.relative_to(runtime)) for p in (runtime/'iokit/IOKit/storage').glob('*.h')]]:
    if name.endswith('.cpp'):assert not (scratch/name).exists(),name
    expected[name]=(runtime/name).read_text()
chunks=[]
for name,after in sorted(expected.items()):
    f=scratch/name;before=f.read_text() if f.exists() else ''
    chunks.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
        fromfile='a/'+name if f.exists() else '/dev/null',tofile='b/'+name,n=3))
patch=repo/'patches/xnu-8796-kebab/0173-z-kebab-prelink-storage-inputs.patch'
assert not patch.exists();patch.write_text(''.join(chunks))
command=['git','-C',str(scratch),'apply','--whitespace=nowarn']
subprocess.run(command+['--check',str(patch)],check=True);subprocess.run(command+[str(patch)],check=True)
record['applied'].append(patch.name)
record['captured_storage_inputs']={'patch':patch.name,'files':sorted(expected),
 'source':'Already-built phase2 before-0174 snapshot plus unchanged runtime vendored storage headers/bindings',
 'runtime_tree_modified':False}
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
