"""Record the builder's already-applied xcconfig preparation in the host series."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'series-repair/host-libsyscall';n='libsyscall/Libsyscall.xcconfig'
def read(host,root):return json.loads(subprocess.run(['ssh',host,'python3 source-reader.py '+root],input=json.dumps([n]),text=True,capture_output=True,check=True).stdout)[n]['source']
before=read('ubuntualbert','/home/albert/s4/x');after=read('albertlee@192.168.30.165','/Users/albertlee/kebab-build/dxb/xnu')
needle='#include "<DEVELOPER_DIR>/Makefiles/CoreOS/Xcode/BSD.xcconfig"'
assert before.count(needle)==1 and after==before.replace(needle,'')
for folder,s in [('before',before),('after',after)]:
    p=d/folder/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,newline='\n')
(d/'0004-fakeroot-libsyscall-config.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+n,tofile='b/'+n)),newline='\n')
print('Recorded exact existing builder transform; no runtime source change')
