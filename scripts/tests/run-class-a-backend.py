"""Verify replayed0470 A backend, actual production host tests and two boots.

Requires clang/clang++, OpenSSL, Python and Git; no device writes, proprietary
binary, private Data or hardware key. Root restoration/VFS/locks/RNG/bufattrs
are explicit host models. Original APFS lock admission is a physical gate.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0470.json').read_text())['files'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5278 and len(changes)==10
for n,pins in changes.items():assert sha(source/n)==manifest[n]==pins['after']
for n,h in json.loads((q/'backend-test-production-inputs.json').read_text()).items():assert sha(source/n)==manifest[n]==h
def expand(n):
 out=[]
 for line in (source/n).read_text().splitlines(True):
  if line.startswith('#include "'):
   name=line.split('"')[1];out.append(line if name=='kebab_file_keys.h' else expand('bsd/kern/'+name))
  elif not line.startswith('#include'):out.append(line)
 return ''.join(out)
actual=expand('bsd/kern/kebab_file_keys.c')
for n in ('class-a-backend-production-test.c','class-b-preservation-production-test.c','cd-preservation-production-test.c','file-compat-production-test.c','bridge-actual-core.c','bridge-A-core.c'):assert (q/n).read_text().count(actual)==1,n
driver=(source/'iokit/Kernel/arm/kebab_block_crypto_impl.h').read_text();assert sha(source/'iokit/Kernel/arm/kebab_block_crypto_impl.h')==manifest['iokit/Kernel/arm/kebab_block_crypto_impl.h']
for n in ('bridge-actual-driver.cpp','bridge-A-driver.cpp'):assert (q/n).read_text().count(driver)==1
assert sha(q/'kebab_file_keys.h')==manifest['bsd/kern/kebab_file_keys.h']
for family in ('a','a-bridge','b-cd-preservation','b-file-compat','b-cpx-bridge'):
 name='test-class-'+family+'-result-0470.json';receipt=json.loads((q/name).read_text());assert receipt['passed'] and not receipt['physical_test']
 if isinstance(receipt['fixture_sha256'],dict):
  for n,h in receipt['fixture_sha256'].items():assert sha(q/n)==h
 else:assert sha(q/({'b-cd-preservation':'cd-preservation-production-test.c','b-file-compat':'file-compat-production-test.c'}[family]))==receipt['fixture_sha256']
native=json.loads((q/'native-ios-identity-build-0470.json').read_text());assert native['passed'] and native['complete_native_build'] and native['native_bytes']==18947880
assert json.loads((q/'native-class-b-link-0470.json').read_text())['source_unchanged_across_full_build']
parent=json.loads((repo/'docs/qualification/0468/all-mac-source-0468.json').read_text())['files']
cases=[('run-backend-tests.py',['class-a-backend-production-test.c','class-b-preservation-production-test.c']),('run-A-bridge-tests.py',['bridge-A-core.c','bridge-A-driver.cpp']),('run-bridge-tests.py',['bridge-actual-core.c','bridge-actual-driver.cpp']),('run-cd-preservation-tests.py',['cd-preservation-production-test.c']),('run-file-compat-tests.py',['file-compat-production-test.c'])]
with tempfile.TemporaryDirectory(prefix='class-a-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 for n in changes:
  z=tree/n;z.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,z)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0352-kebab-connect-complete-protection-file-keys.patch')],cwd=tree,check=True)
 for n,pins in changes.items():
  if pins['before']:assert sha(tree/n)==pins['before']==parent[n]
  else:assert not (tree/n).exists()
 for runner,fixtures in cases:
  work=temp/runner.removesuffix('.py');work.mkdir()
  for n in fixtures+['kebab_file_keys.h']:shutil.copyfile(q/n,work/n)
  shutil.copytree(source/'libkern/crypto/kebab_curve25519',work/'source')
  subprocess.run(['python3',str(q/runner),str(work)],check=True)
old=None
for v in ('0470','0471'):
 folder=q if v=='0470' else q.parent/'0471';log=folder/(v+'-media-boot.log');s=log.read_text();receipt=json.loads((folder/(v+'-A-qualified.json')).read_text())
 assert sha(log)==receipt['actual_log_sha256']==json.loads((folder/(v+'-capture-provenance.json')).read_text())['sha256']
 assert receipt['accepted'] and receipt['native_sha256']==native['native_sha256'] and receipt['qualified_watchdog_seconds']>=300
 assert not receipt['physical_all_RAM_erasure_proven'] and not receipt['full_goal_complete'] and not receipt['usable_springboard']
 assert not any(x in s for x in ('panic(cpu','completion timeout','probes blocked'))
 for name,count in [('DRV',1),('PROC',1),('UI',1),('MIG',2),('BBOARD',1)]:assert s.count(name+' actual completion observed')==count
 for operation in ('cached-read','nocache-read','write'):assert 'KEBAB A PROBE: locked already-open '+operation+' result=1 residual=65559; expected EPERM/EACCES, zero transferred' in s
 for operation in ('closed reopen','exclusive create'):assert 'KEBAB A PROBE: locked '+operation+'=1; expected EPERM/EACCES' in s
 assert 'KEBAB A PROBE: private file lifecycle PASS (0), unlock=0; original cached I/O checks retained' in s
 end=s.index('KEBAB A PROBE: private file lifecycle PASS');ops=re.findall(r'KEBAB APFS A (NEW|UNWRAP): crypto-id=(\d+) result=0;',s[:end]);identity=receipt['private_A_crypto_id']
 assert ops[0]==('NEW' if v=='0470' else 'UNWRAP',str(identity)) and ('UNWRAP',str(identity)) in ops
 stripes={str(off):re.findall(r'class=1 logical='+str(off)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for off in (0,61440)}
 assert stripes==receipt['private_A_ciphertext_stripes'] and all(len(x)==2 and len(set(x))==1 for x in stripes.values())
 if old:assert receipt['A_complete_kernel_restart_proven'] and identity==old['private_A_crypto_id'] and stripes==old['private_A_ciphertext_stripes'] and not any(op=='NEW' for op,_ in ops)
 else:assert not receipt['A_complete_kernel_restart_proven']
 assert receipt['cold_keychain_classes_unwrap_before_new']==[6,7,8,9,10,11]
 keychain={}
 for sel,cls in re.findall(r'KEBAB AKS KEYCHAIN: selector=(\d+) class=(\d+) bytes=\d+/\d+ result=0;',s):keychain.setdefault(int(cls),int(sel))
 assert keychain=={k:11 for k in (6,7,8,9,10,11)}
 bfirst={}
 for op,n in re.findall(r'KEBAB APFS B (NEW|UNWRAP): crypto-id=(\d+) [^\n]*result=0;',s):bfirst.setdefault(int(n),op)
 for n in (245422,252640):assert bfirst[n]=='UNWRAP'
 bstripes={str(off):re.findall(r'class=2 logical='+str(off)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for off in (0,61440)}
 assert bstripes==json.loads((repo/'docs/qualification/0469/0469-B-CPX-qualified.json').read_text())['B_private_ciphertext_stripes']
 old=receipt
print('PASS actual A/core, A+B native driver/XTS/destructor, C/D vectors,16 compiled behavioral mutants, reverse0352 and sealed full-restart proof. Full key interfaces, migration and usable GUI remain unqualified.')
