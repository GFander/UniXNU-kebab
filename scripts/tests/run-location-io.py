"""Verify actual locationd I/O evidence, exact source preservation and production models.

Requires an already replayed 0462 source tree, Git and clang++. It performs no
device operations. Kernel/VFS/credential APIs in the C++ fixture are models.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile,shutil
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();q=a.qualification.resolve();source=a.source.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
native=json.loads((q/'native-ios-identity-build-0462.json').read_text());assert native['passed']
macros=native['actual_preprocessed_macros'];assert macros['CONFIG_EXT_RESOLVER']=='0' and macros['CONFIG_MACF']==macros['CONFIG_PROTECT']==macros['CONFIG_AUDIT']=='1'
test=json.loads((q/'test-location-rw-result-0462.json').read_text());assert test['passed']
for n,h in test['actual_after_sha256'].items():assert sha((source/n).read_bytes())==h
manifest=json.loads((q/'all-mac-source-0462.json').read_text())['files']
bsd=(source/'bsd/kern/kebab_bootdiag.c').read_text();begin=bsd.index('/* Normal, bounded original locationd database I/O evidence.')
end=bsd.index('#endif /* KEBAB */',begin);fragment=bsd[begin:end].removesuffix('\n')
assert sha(fragment.encode())==test['complete_fragment_sha256']
fixture=(q/'location-rw-production-test.cpp').read_text();assert sha(fixture.encode())==test['fixture_sha256'] and fixture.count(fragment)==1
changes=[
 ('rw-success-gate','    if ((!strcmp(operation, "read") || !strcmp(operation, "write")) && !error) return;',''),
 ('uuid',' && !memcmp(uuid, expected, sizeof(uuid))',' && !memcmp(uuid, expected, 0)'),
 ('irq','ml_at_interrupt_context() || ',''),
 ('reference','        vnode_put(vnode);','        /* intentionally missing reference release */'),
 ('path-error','(error && (lookup || path_error))','(error && lookup)'),
 ('errno','operation, proc_pid(process), fd, command, error, result, lookup','operation, proc_pid(process), fd, command, 0, result, lookup'),
 ('cap','return __atomic_fetch_add(counter, 1u, __ATOMIC_RELAXED) < cap;','return true;'),
 ('path-bound','while (bytes < 512 && path[bytes])','while (bytes < 513 && path[bytes])'),
 ('path-scope','if (!kebab_location_io_path(path) || !kebab_location_io_claim','if (false || !kebab_location_io_claim')]
with tempfile.TemporaryDirectory(prefix='location-io-qualification-',dir=cache) as temp:
 root=Path(temp);tree=root/'source';tree.mkdir()
 subprocess.run(['git','init','--quiet',str(tree)],check=True)
 names=('bsd/kern/kebab_bootdiag.c','bsd/kern/sys_generic.c','bsd/kern/kern_descrip.c','bsd/vfs/vfs_syscalls.c')
 for n in names:
  assert sha((source/n).read_bytes())==manifest[n]
  dst=tree/n;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,dst)
 for v,patch in [('0461','0347-kebab-observe-original-locationd-read-write-errors.patch'),('0460','0346-kebab-observe-original-locationd-database-io-results.patch')]:
  path=repo/'patches/xnu-8796-kebab'/patch
  subprocess.run(['git','apply','--reverse','--check',str(path)],cwd=tree,check=True)
  subprocess.run(['git','apply','--reverse',str(path)],cwd=tree,check=True)
  baseq=q.parent/v
  if not baseq.exists():baseq=repo/'docs/qualification'/v
  old=json.loads((baseq/('all-mac-source-'+v+'.json')).read_text())['files']
  assert all(sha((tree/n).read_bytes())==old[n] for n in names)
 def compile_case(name,text):
  src=root/(name+'.cpp');binary=root/name;src.write_text(text)
  result=subprocess.run(['clang++','-std=c++17','-pthread','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(binary)],capture_output=True,text=True)
  assert result.returncode==0,(name,result.stderr)
  return subprocess.run([str(binary)],capture_output=True,text=True)
 baseline=compile_case('production',fixture);assert baseline.returncode==0,baseline.stderr
 for name,old,new in changes:
  assert fixture.count(old)==1,name
  result=compile_case(name,fixture.replace(old,new));assert result.returncode!=0,(name,'not rejected')
for v in ('0461','0462'):
 folder=q.parent/v;raw=(folder/(v+'-media-boot.log')).read_bytes();text=raw.decode()
 runtime=json.loads((folder/(v+'-qualified-runtime.json')).read_text());provenance=json.loads((folder/(v+'-capture-provenance.json')).read_text())
 assert runtime['accepted'] and runtime['actual_log_sha256']==provenance['sha256']==sha(raw)
 assert not any(s in text for s in ('panic(cpu','completion timeout','probes blocked'))
 assert text.count('MIG actual completion observed')==2 and text.count('UI actual completion observed')==1
 assert runtime['qualified_watchdog_seconds']>=300 and not runtime['sqlite_ioerr_root_cause_fixed'] and not runtime['usable_springboard'] and not runtime['full_goal_complete']
 assert json.loads((folder/(v+'-systembag-qualified.json')).read_text())['stage_accepted']
 errors=[(i,line) for i,line in enumerate(text.splitlines()) if 'KEBAB LOC-IO: op=write ' in line and 'command=1 error=1 ' in line and 'class=2 path=' in line and line.endswith('/cache_encryptedB.db-journal')]
 if v=='0462':
  assert len(errors)>=3 and runtime['actual_errno']==1 and runtime['reported_file_class']==2
  lines=text.splitlines()
  assert sum(any('AKS new_key failed for class = 2, error = e00002c7' in lines[j] for j in range(max(0,i-3),i)) for i,_ in errors)>=3
  assert len(runtime['original_sqlite_retry_frames'])==2
print('PASS actual classB journal offset-write EPERM evidence; two patch reversals byte-exact; full observer +9 behavioral mutants; hardware/Data/watchdog receipts preserved. B crypto/GUI remain incomplete.')
