from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--qualification',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);a=p.parse_args()
a.cache.mkdir(parents=True,exist_ok=True);os.environ['TMPDIR']=str(a.cache.resolve());q=a.qualification
header=(a.source/'bsd/dev/kebab_data_identity_impl.h').read_bytes();fixture=(q/'data-device-identity-test.cpp').read_bytes();receipt=json.loads((q/'test-data-device-identity-result.json').read_text())
assert receipt['passed']and hashlib.sha256(header).hexdigest()==receipt['production_sha256']
assert hashlib.sha256(fixture).hexdigest()==receipt['fixture_sha256']and header in fixture
source=fixture.decode()
mutants=[('actual',source,True),('stale-minor3',source.replace('makedev(target.major_number,target.minor_number)','makedev(1,3)'),False),('foreign-mount',source.replace('!vnode_isdir(point)||vnode_mount(point)!=data','!vnode_isdir(point)'),False),('ignored-uuid-error',source.replace('if(error)return error;','if(false)return error;',1),False)]
for tag,text,expected in mutants:
 path=a.cache/(tag+'.cpp');path.write_text(text);exe=path.with_suffix('')
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(path),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
 assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
 if expected:print(result.stdout.strip())
 else:assert 'Assertion'in result.stderr;print('Rejected',tag)
