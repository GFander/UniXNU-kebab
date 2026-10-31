"""Register one exact native userland candidate after full source replay."""
from pathlib import Path
import ast,hashlib,json,re,sys
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
v,previous,native,replay_version,*options=sys.argv[1:]
assert options in ([],['fsck']);fsck=bool(options)
assert all(len(x)==4 and x.isdigit() for x in (v,previous,replay_version)) and int(v)>int(previous)
assert len(native)==64 and hashlib.sha256((b/('kernel-'+v+'.kebab')).read_bytes()).hexdigest()==native
q=json.loads((r/('all-source-replay-'+replay_version+'.json')).read_text());assert q['all_source_equal']
p=r/'flash-stage.py';s=p.read_text()
allowline=next(line for line in s.splitlines() if line.startswith('assert sys.argv[2:]'))
assert v not in ast.literal_eval(allowline.split('VERSION in ',1)[1]),'Version already registered'
lines=s.splitlines(True)
for i,l in enumerate(lines):
 if l.startswith('assert sys.argv[2:]'):
  prefix=l.split('VERSION in ',1)[0]+'VERSION in ';vs=ast.literal_eval(l.split('VERSION in ',1)[1]);lines[i]=prefix+repr(vs+(v,))+'\n'
 if l.startswith('reference = '):
  refs=ast.literal_eval(l.removeprefix('reference = ').split('[VERSION]')[0]);refs[v]=previous;lines[i]='reference = '+repr(refs)+'[VERSION]\n'
 if l.startswith('DATA_STAGES='):
  stages=ast.literal_eval(l.split('=',1)[1]);stages[v]=('7','0') if fsck else ('12','1');lines[i]='DATA_STAGES='+repr(stages)+'\n'
 if l.startswith('if VERSION in ') and '): changed_files.add' in l:
  values=ast.literal_eval(l.split('if VERSION in ',1)[1].split(': changed_files',1)[0]);lines[i]='if VERSION in '+repr(values+(v,))+': changed_files'+l.split(': changed_files',1)[1]
s=''.join(lines).replace('sha = lambda data:',f'if VERSION=={v!r}: NATIVE={native!r}\nsha = lambda data:')
def rw(m):return m[1]+repr(ast.literal_eval(m[2])+(v,))+m[3]
if not fsck:s=re.sub(r"(ufsrw='1' if VERSION in )(\([^)]*\))( else '0')",rw,s)
pattern=r"(\*\(\['--data-files-proof',)(\{[^}]+\})(\[VERSION\]\] if VERSION in )(\([^)]*\))( else \[\]\))"
def profile(m):
 d=ast.literal_eval(m[2]);d[v]='template-userland';vs=ast.literal_eval(m[4]);return m[1]+repr(d)+m[3]+repr(vs+(v,))+m[5]
if fsck:
 pattern=r"(\*\(\['--data-fsck'\] if VERSION in )(\([^)]*\))( else \[\]\))"
 def checker(m):return m[1]+repr(ast.literal_eval(m[2])+(v,))+m[3]
 s,count=re.subn(pattern,checker,s)
else:s,count=re.subn(pattern,profile,s)
assert count==1;p.write_bytes(s.encode())
p=r/'audit-aks-provider-abi.py';s=p.read_text()
def abi(m):
 vs=ast.literal_eval(m[1]);return 'version in '+repr(vs+(v,)) if previous in vs else m[0]
s=re.sub(r'version in (\([^\n)]*\))',abi,s)
s=s.replace('assert version in pins',f'pins[{v!r}]={native!r}\nassert version in pins');p.write_bytes(s.encode())
print('Registered',v,'exact native and profile, reference',previous)
