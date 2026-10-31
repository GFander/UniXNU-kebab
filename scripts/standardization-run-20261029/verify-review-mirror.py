"""Check copied review files by bytes without staging or publishing anything."""
from pathlib import Path
import hashlib,json,subprocess
r=Path(__file__).parent;expected=json.loads((r/'review-file-manifest.json').read_text())
for name,p in [('scripts/verify-all.sh',r/'verify-all.sh'),('scripts/test-panic-fastboot.py',r/'reboot-layer/test-panic-fastboot.py'),
 ('docs/standardization-run-20261029/review-file-manifest.json',r/'review-file-manifest.json')]:
    expected[name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
code='''from pathlib import Path
import hashlib,json,sys
r=Path('/home/albert/kebab-dev-ios16').resolve();expected=json.load(sys.stdin);bad=[]
for name,record in expected.items():
 p=(r/name).resolve();assert r in p.parents
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=record['sha256']:bad.append(name)
print(json.dumps({'files':len(expected),'equal':not bad,'different':bad}))
'''
result=subprocess.run(['ssh','ubuntualbert','python3 -c '+__import__('shlex').quote(code)],input=json.dumps(expected),text=True,capture_output=True,check=True)
got=json.loads(result.stdout);assert got['equal'],got
(r/'review-mirror-verification.json').write_text(json.dumps(got,indent=2)+'\n');print('PASS:',got['files'],'review files match byte-for-byte on Ubuntu; no commit/push')
