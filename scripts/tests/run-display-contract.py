"""Host coverage of exact production fragments, not physical rendering/input."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--qualification',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);p.add_argument('--compiler',default='clang++');a=p.parse_args()
here=Path(__file__).resolve().parent;raw=(here/'display-contract.cpp').read_bytes();q=json.loads((a.qualification/'test-display-contract-result.json').read_text())
assert q['passed'] and hashlib.sha256(raw).hexdigest()==q['fixture_sha256']
a.cache.mkdir(parents=True,exist_ok=True);os.environ['TMPDIR']=str(a.cache.resolve())
source=raw.decode();old='ret = kIOReturnUnsupported;';assert source.count(old)==1
for tag,text,expected in [('actual',source,True),('wrong-success',source.replace(old,'ret = 0;'),False)]:
 path=a.cache/(tag+'.cpp');path.write_text(text);binary=path.with_suffix('')
 subprocess.run([a.compiler,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True)
 result=subprocess.run([str(binary)],capture_output=True,text=True)
 assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
 if expected:print(result.stdout.strip())
 else:
  assert 'Assertion' in result.stderr,result.stderr
  print('Wrong-success behavioral mutation refused')
