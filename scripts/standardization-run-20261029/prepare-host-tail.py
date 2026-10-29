"""Record the missing availability script and FPD exports; retain the upstream validator."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'series-repair/host-tail';names=['bsd/sys/make_symbol_aliasing.sh','config/Private.exports']
def read(root):return json.loads(subprocess.run(['ssh','albertlee@192.168.30.165','python3 source-reader.py '+root],input=json.dumps(names),text=True,capture_output=True,check=True).stdout)
if not (d/'before').exists():
    now=read('/Users/albertlee/kebab-build/dxb/xnu');old=read('/Users/albertlee/kebab-build/xnu-before-clean-replay-0222')
    for n in names:
        for folder,raw in [('before',now[n]['source']),('old-tested',old[n]['source'])]:
            p=d/folder/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(raw,newline='\n')
patch=[]
for n in names:
    before=(d/'before'/n).read_text()
    if n.endswith('.sh'):
        needle='AVAILABILITY_PL="${XNU_AVAILABILITY_PL:-${SDKROOT}/${DRIVERKITROOT}/usr/local/libexec/availability.pl}"'
        assert before.count(needle)==1
        after=before.replace(needle,'AVAILABILITY_PL="${XNU_AVAILABILITY_PL:-${FAKEROOT_DIR}/usr/local/libexec/availability.pl}"')
    else:after=(d/'old-tested'/n).read_text()
    p=d/'after'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
    patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+n,tofile='b/'+n))
(d/'0002-fakeroot-availability-fpd-exports.patch').write_text(''.join(patch),newline='\n')
print('Recorded availability path with override preserved, plus 3 tested FPD exports; validator remains upstream')
