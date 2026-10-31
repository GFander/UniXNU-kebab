"""Verify source0466 B backend, explicit host models and two sealed real boots.
Needs the replayed0466 XNU tree, clang/clang++, OpenSSL, Python and Git.
No device operation, proprietary firmware, private Data, keyblob or secret needed.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0466.json').read_text())['files'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5274 and len(changes)==19
for n,pins in changes.items():assert sha(source/n)==manifest[n]==pins['after']
inputs=json.loads((q/'backend-test-production-inputs.json').read_text())
for n,h in inputs.items():assert sha(source/n)==manifest[n]==h
def expand(n):
 out=[]
 for line in(source/n).read_text().splitlines(True):
  if line.startswith('#include "'):
   name=line.split('"')[1];out.append(line if name=='kebab_file_keys.h'else expand('bsd/kern/'+name))
  elif not line.startswith('#include'):out.append(line)
 return ''.join(out)
actual=expand('bsd/kern/kebab_file_keys.c')
for name in('class-b-backend-production-test.c','cd-preservation-production-test.c','file-compat-production-test.c'):assert(q/name).read_text().count(actual)==1,name
client=(source/'iokit/Kernel/arm/kebab_aks_userclient_impl.h').read_text()
for name in('kebab_aks_cookie_impl.h','kebab_aks_keychain_impl.h','kebab_aks_device_lock_impl.h'):client=client.replace('#include "'+name+'"',(source/'iokit/Kernel/arm'/name).read_text())
client='\n'.join(line for line in client.splitlines()if not line.startswith('#include'))
assert(q/'class-b-client-production-test.cpp').read_text().count(client)==1
close=''.join(line for line in(source/'bsd/kern/kebab_b_file_lifecycle.c').read_text().splitlines(True)if not line.startswith('#include'))
assert(q/'class-b-lifecycle-production-test.c').read_text().count(close)==1
assert sha(q/'kebab_file_keys.h')==manifest['bsd/kern/kebab_file_keys.h']
cases=[('backend','class-b-backend-production-test.c','run-backend-tests.py'),('client','class-b-client-production-test.cpp','run-device-client-tests.py'),('lifecycle','class-b-lifecycle-production-test.c','run-lifecycle-tests.py'),('cd-preservation','cd-preservation-production-test.c','run-cd-preservation-tests.py'),('file-compat','file-compat-production-test.c','run-file-compat-tests.py')]
for family,name,_ in cases:
 receipt=json.loads((q/('test-class-b-'+family+'-result-0466.json')).read_text());assert receipt['passed']and not receipt['physical_test'];assert sha(q/name)==receipt['fixture_sha256']
native=json.loads((q/'native-ios-identity-build-0466.json').read_text());assert native['passed']and native['complete_native_build']and native['native_bytes']==18980056
assert json.loads((q/'native-class-b-link-0466.json').read_text())['source_unchanged_across_full_build']
parent=json.loads((repo/'docs/qualification/0464/all-mac-source-0464.json').read_text())['files']
with tempfile.TemporaryDirectory(prefix='class-b-',dir=cache)as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 for n,pins in changes.items():
  z=tree/n;z.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,z)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0350-kebab-connect-protected-unless-open-and-device-lock.patch')],cwd=tree,check=True)
 for n,pins in changes.items():
  if pins['before']:assert sha(tree/n)==pins['before']==parent[n]
  else:assert not(tree/n).exists()
 for family,name,runner in cases:
  work=temp/family;work.mkdir();shutil.copyfile(q/name,work/name);shutil.copyfile(q/'kebab_file_keys.h',work/'kebab_file_keys.h')
  shutil.copytree(source/'libkern/crypto/kebab_curve25519',work/'source')
  subprocess.run(['python3',str(q/runner),str(work)],check=True)
for v in('0466','0467'):
 folder=q if v=='0466'else q.parent/'0467';raw=folder/(v+'-media-boot.log');s=raw.read_text();receipt=json.loads((folder/(v+'-class-b-functional.json')).read_text())
 assert sha(raw)==receipt['actual_log_sha256']==json.loads((folder/(v+'-capture-provenance.json')).read_text())['sha256']
 assert receipt['qualified_partial_milestone']and receipt['qualified_watchdog_seconds']>=300 and receipt['native_sha256']==native['native_sha256']
 assert not receipt['last_close_memory_key_erasure_proven']and not receipt['full_goal_complete']and not receipt['usable_springboard']
 assert not any(x in s for x in('panic(cpu','completion timeout','probes blocked'))
 if v=='0466':
  for name in('cache_encryptedB.db','cache_encryptedB.db-journal'):assert any('KEBAB LOC-IO: op=fsync 'in line and 'error=0 'in line and 'class=2 path='in line and line.endswith('/'+name)for line in s.splitlines())
 else:
  for name in('cache_encryptedB.db','cache_encryptedB.db-wal','cache_encryptedB.db-shm'):assert any('KEBAB LOC-IO: op=fcntl 'in line and 'command=63 error=0 result=2 'in line and 'class=2 path='in line and line.endswith('/'+name)for line in s.splitlines())
 assert 'KEBAB B PROBE: locked existing-open protected read/write=0 state=5'in s and 'KEBAB B PROBE: private file lifecycle PASS (0)'in s
 if v=='0467':
  assert receipt['private_B_full_restart_restoration_proven']and set(receipt['cold_B_crypto_ids_unwrap_before_new'])=={245422,245423}
  assert set(receipt['cold_keychain_classes_unwrap_before_new'])=={6,7,8,9,10,11}
print('PASS actual0466 B/core/client/close/CD compatibility,22 compiled mutants, reverse0350 and two sealed real boots. Close-time memory erasure, complete key services/migration/shell/GUI remain unqualified.')
