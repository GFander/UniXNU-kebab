"""Verify replayed0468 production bodies, compiled tests and sealed0468/0469.

Requires clang/clang++, OpenSSL, Python and Git. No device writes or closed
firmware/keys required. Root restoration, VFS, locks and RNG remain host models.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for name in ('repo','source','qualification','cache'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0468.json').read_text())['files']
changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5275 and len(changes)==6
for n,pins in changes.items():assert sha(source/n)==manifest[n]==pins['after']
inputs=json.loads((q/'backend-test-production-inputs.json').read_text())
for n,h in inputs.items():assert sha(source/n)==manifest[n]==h
def expand(n):
 out=[]
 for line in (source/n).read_text().splitlines(True):
  if line.startswith('#include "'):
   name=line.split('"')[1];out.append(line if name=='kebab_file_keys.h' else expand('bsd/kern/'+name))
  elif not line.startswith('#include'):out.append(line)
 return ''.join(out)
actual=expand('bsd/kern/kebab_file_keys.c')
for name in ('class-b-backend-production-test.c','cd-preservation-production-test.c','file-compat-production-test.c','bridge-actual-core.c'):
 assert (q/name).read_text().count(actual)==1,name
driver=(source/'iokit/Kernel/arm/kebab_block_crypto_impl.h').read_text()
assert sha(source/'iokit/Kernel/arm/kebab_block_crypto_impl.h')==manifest['iokit/Kernel/arm/kebab_block_crypto_impl.h']
assert (q/'bridge-actual-driver.cpp').read_text().count(driver)==1
assert sha(q/'kebab_file_keys.h')==manifest['bsd/kern/kebab_file_keys.h']
tests=[('cpx','class-b-backend-production-test.c','run-backend-tests.py'),('cpx-bridge',None,'run-bridge-tests.py'),('cd-preservation','cd-preservation-production-test.c','run-cd-preservation-tests.py'),('file-compat','file-compat-production-test.c','run-file-compat-tests.py')]
for family,name,_ in tests:
 receipt=json.loads((q/('test-class-b-'+family+'-result-0468.json')).read_text());assert receipt['passed'] and not receipt['physical_test']
 if name:assert sha(q/name)==receipt['fixture_sha256']
 else:
  for n,h in receipt['fixture_sha256'].items():assert sha(q/n)==h
native=json.loads((q/'native-ios-identity-build-0468.json').read_text())
assert native['passed'] and native['complete_native_build'] and native['native_bytes']==18980208
assert json.loads((q/'native-class-b-link-0468.json').read_text())['source_unchanged_across_full_build']
parent=json.loads((repo/'docs/qualification/0466/all-mac-source-0466.json').read_text())['files']
with tempfile.TemporaryDirectory(prefix='class-b-cpx-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 for n in changes:
  z=tree/n;z.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,z)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0351-kebab-keep-class-b-file-keys-out-of-apfs-cpx-caches.patch')],cwd=tree,check=True)
 for n,pins in changes.items():
  if pins['before']:assert sha(tree/n)==pins['before']==parent[n]
  else:assert not (tree/n).exists()
 for _,name,runner in tests:
  work=temp/runner.removesuffix('.py');work.mkdir()
  for n in ([name] if name else ['bridge-actual-core.c','bridge-actual-driver.cpp']):shutil.copyfile(q/n,work/n)
  shutil.copyfile(q/'kebab_file_keys.h',work/'kebab_file_keys.h')
  shutil.copytree(source/'libkern/crypto/kebab_curve25519',work/'source')
  subprocess.run(['python3',str(q/runner),str(work)],check=True)
old=None
for v in ('0468','0469'):
 folder=q if v=='0468' else q.parent/'0469';log=folder/(v+'-media-boot.log');s=log.read_text()
 receipt=json.loads((folder/(v+'-B-CPX-qualified.json')).read_text())
 assert sha(log)==receipt['actual_log_sha256']==json.loads((folder/(v+'-capture-provenance.json')).read_text())['sha256']
 assert receipt['accepted'] and receipt['native_sha256']==native['native_sha256'] and receipt['qualified_watchdog_seconds']>=300
 assert not receipt['physical_secret_RAM_dump_or_all_copy_erasure_proof'] and not receipt['full_goal_complete'] and not receipt['usable_springboard']
 assert not any(x in s for x in ('panic(cpu','completion timeout','probes blocked'))
 for name,count in [('DRV',1),('PROC',1),('UI',1),('MIG',2)]:assert s.count(name+' actual completion observed')==count
 assert s.count('KEBAB B CPX: opaque64 -> io-local32 result=0;')==16
 for text in ('locked existing-open protected read/write=0 state=5','locked after-last-close original reopen=1;','locked public-created file reopen=1;','private file lifecycle PASS (0), unlock=0;'):assert 'KEBAB B PROBE: '+text in s
 first={}
 for op,identity in re.findall(r'KEBAB APFS B (NEW|UNWRAP): crypto-id=(\d+) [^\n]*result=0;',s):first.setdefault(int(identity),op)
 assert first[245422]=='UNWRAP'
 keychain={}
 for sel,cls in re.findall(r'KEBAB AKS KEYCHAIN: selector=(\d+) class=(\d+) bytes=\d+/\d+ result=0;',s):keychain.setdefault(int(cls),int(sel))
 assert keychain=={k:11 for k in (6,7,8,9,10,11)}
 stripes={str(off):re.findall(r'class=2 logical='+str(off)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for off in (0,61440)}
 assert stripes==receipt['B_private_ciphertext_stripes'] and all(len(x)==3 for x in stripes.values())
 if old:
  assert first[old['fresh_opaque_locked_create_crypto_id']]=='UNWRAP' and not any(op=='NEW' and int(identity)==old['fresh_opaque_locked_create_crypto_id'] for op,identity in re.findall(r'KEBAB APFS B (NEW|UNWRAP): crypto-id=(\d+) [^\n]*result=0;',s))
  assert receipt['cold_new_HB01_proof_unwrap_before_new_key'] and stripes==old['B_private_ciphertext_stripes']
 else:assert receipt['public_only_fresh_locked_create'] and 'locked exclusive create/write/read/fsync/close=0; public-key-only path' in s
 for name in ('cache_encryptedB.db','cache_encryptedB.db-wal','cache_encryptedB.db-shm'):assert any('KEBAB LOC-IO: op=fcntl ' in line and 'command=63 error=0 result=2 ' in line and 'class=2 path=' in line and line.endswith('/'+name) for line in s.splitlines())
 old=receipt
print('PASS production0468 B CPX/core/driver/XTS/destructor/CD tests,15 compiled behavioral mutants, reverse0351 and two sealed boots. No all-RAM/SEP/passcode/migration/shell/GUI completion claim.')
