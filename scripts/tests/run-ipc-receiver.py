"""Verify current and original IPC observer helpers, native pins and sealed boot.

Requires clang, Python and Git. No device writes, closed binary or message
payload. Native IPC/task/scheduler lifetime remains an explicit host model.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0474.json').read_text())['files'];parent=json.loads((repo/'docs/qualification/0470/all-mac-source-0470.json').read_text())['files']
oldq=q.parent/'0472';oldchanges=json.loads((oldq/'candidate-manifest.json').read_text())['changes'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes'];combined={**oldchanges,**changes}
assert len(manifest)==5279 and len(oldchanges)==2 and len(changes)==1
assert manifest=={**parent,**{n:pins['after'] for n,pins in combined.items()}}
for n in combined:assert sha(source/n)==manifest[n],n
header='osfmk/kern/kebab_mach_wait_ipc_impl.h';body=(source/header).read_text();fixture=q/'ipc-receiver-production-test.c'
assert fixture.read_text().count(body)==1
host=json.loads((q/'test-ipc-receiver-result-0474.json').read_text());assert host['passed'] and host['compiled_behavioral_mutants']==11 and not host['physical_test'] and sha(fixture)==host['fixture_sha256']
native=json.loads((q/'native-ios-identity-build-0474.json').read_text());link=json.loads((q/'native-ipc-receiver-link-0474.json').read_text())
assert native['passed'] and native['complete_native_build'] and link['passed'] and link['source_unchanged_across_full_build']
assert {x['callee'] for x in link['actual_native_IPC_call_edges']}=={'_ipc_port_translate_send','_ipc_port_get_receiver_task_locked'}
with tempfile.TemporaryDirectory(prefix='ipc-receiver-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 # --source is the common board tree. Host adaptations are replayed separately
 # from the pinned OSS base, then every compiled-source manifest entry is checked.
 subprocess.run(['python3',str(repo/'scripts/qualification/replay-xnu.py'),'--repo',str(repo),'--xnu-source',str(source),'--manifest',str(q/'all-mac-source-0474.json'),'--receipt',str(temp/'fresh-full-replay.json'),'--cache',str(cache),'--expected-common','333'],check=True)
 for n in combined:
  z=tree/n;z.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,z)
 patch=repo/'patches/xnu-8796-kebab/0354-kebab-observe-sync-wait-ports-with-bounded-log-lines.patch'
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(patch)],cwd=tree,check=True)
 oldbody=(tree/header).read_text();assert sha(tree/header)==oldchanges[header]['after']
 oldfixture=oldq/'ipc-receiver-production-test.c';oldhost=json.loads((oldq/'test-ipc-receiver-result-0472.json').read_text())
 assert oldhost['passed'] and oldhost['compiled_behavioral_mutants']==9 and sha(oldfixture)==oldhost['fixture_sha256'] and oldfixture.read_text().count(oldbody)==1
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0353-kebab-observe-fixed-mach-message-receivers.patch')],cwd=tree,check=True)
 for n,pins in combined.items():
  if n in parent:assert sha(tree/n)==parent[n]
  else:assert not (tree/n).exists()
 for label,folder,fixture_name in [('old',oldq,'ipc-receiver-production-test.c'),('current',q,'ipc-receiver-production-test.c')]:
  work=temp/label;work.mkdir();shutil.copyfile(folder/fixture_name,work/fixture_name)
  subprocess.run(['python3',str(folder/'run-tests.py'),str(work)],check=True)
 log=q/'0474-media-boot.log';receipt=json.loads((q/'0474-IPC-qualified.json').read_text());crypto=receipt['original_crypto_regression']
 assert sha(log)==receipt['actual_log_sha256']==json.loads((q/'0474-capture-provenance.json').read_text())['sha256']
 assert receipt['accepted'] and receipt['successful_sync_contexts']>0 and crypto['accepted'] and crypto['qualified_watchdog_seconds']>=300
 assert receipt['native_sha256']==native['native_sha256'] and crypto['native_version']=='0474'
 assert not receipt['atomic_transaction_pairing_proven'] and not receipt['dependency_cycle_proven'] and not receipt['full_goal_complete'] and not receipt['usable_springboard']
 work=temp/'parse';work.mkdir()
 for n in ('0474-media-boot.log','0474-capture-provenance.json','parse-ipc-receiver-0474.py'):shutil.copyfile(q/n,work/n)
 subprocess.run(['python3',str(work/'parse-ipc-receiver-0474.py')],check=True)
 parsed=json.loads((work/'0474-IPC-observations.json').read_text());assert parsed['rows']==receipt['rows'] and parsed['capture_closed']
 assert parsed['successful_sync_contexts']==receipt['successful_sync_contexts']
 # Verify physical protection evidence from the original sealed bytes.
 s=log.read_text();assert not any(x in s for x in ('panic(cpu','completion timeout','probes blocked'))
 for operation in ('cached-read','nocache-read','write'):assert 'KEBAB A PROBE: locked already-open '+operation+' result=1 residual=65559;' in s
 assert 'KEBAB A PROBE: private file lifecycle PASS (0), unlock=0;' in s and 'KEBAB B PROBE: private file lifecycle PASS (0), unlock=0;' in s
 assert crypto['cold_keychain_classes_unwrap_before_new']==[6,7,8,9,10,11] and crypto['old_B_ciphertext_stripes_preserved']
 first={}
 for sel,cls in re.findall(r'KEBAB AKS KEYCHAIN: selector=(\d+) class=(\d+) bytes=\d+/\d+ result=0;',s):first.setdefault(int(cls),int(sel))
 assert first=={k:11 for k in (6,7,8,9,10,11)}
 for cls,oldfile,field in [(1,'0471/0471-A-qualified.json','private_A_ciphertext_stripes'),(2,'0469/0469-B-CPX-qualified.json','B_private_ciphertext_stripes')]:
  stripes={str(offset):re.findall(r'class='+str(cls)+' logical='+str(offset)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for offset in (0,61440)}
  assert stripes==json.loads((repo/'docs/qualification'/oldfile).read_text())[field]
 for name in ('0474-defaults-qualified.json','0474-systembag-qualified.json'):
  state=json.loads((q/name).read_text());assert state['actual_log_sha256']==sha(log)
 assert json.loads((q/'0474-systembag-qualified.json').read_text())['stage_accepted']
print('PASS original204/current208 IPC helper cases,20 compiled behavioral mutants, reverse0354+0353, exact source/native and sealed native receivers. RPC atomic pairing, cycle and full migration/shell remain unqualified.')
