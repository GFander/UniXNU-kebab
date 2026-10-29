from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent
rows=json.loads((r/'actual-input-comparison-0217.json').read_text())['different']
names=[x['path'] for x in rows if x['ubuntu'] is not None and x['mac'] is not None]
def read(host,root):
    return json.loads(subprocess.run(['ssh',host,'python3 source-reader.py '+root],input=json.dumps(names),text=True,capture_output=True,check=True).stdout)
before=read('ubuntualbert','/home/albert/s4/x');after=read('albertlee@192.168.30.165','/Users/albertlee/kebab-build/dxb/xnu')
for name in names:
    for group,values in [('before',before),('after',after)]:
        p=r/'macos-overlay'/group/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(values[name]['source'],newline='\n')
    diff=''.join(difflib.unified_diff(before[name]['source'].splitlines(True),after[name]['source'].splitlines(True),fromfile='ubuntu/'+name,tofile='mac/'+name))
    p=r/'macos-overlay/diffs'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(diff,newline='\n')
    print(name,'diff lines',len(diff.splitlines()))
