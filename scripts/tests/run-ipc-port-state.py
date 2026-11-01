"""Verify production port-state observer, fresh full source replay and sealed boot.

Requires clang, Python and Git. No device write or message payload access.
IPC/task/scheduler/refcount concurrency remains an explicit host model.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0475.json').read_text())['files'];parent=json.loads((repo/'docs/qualification/0474/all-mac-source-0474.json').read_text())['files'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5279 and len(changes)==1 and manifest=={**parent,**{n:pin['after'] for n,pin in changes.items()}}
for n,pin in changes.items():assert sha(source/n)==manifest[n]==pin['after'] and parent[n]==pin['before']
header='osfmk/kern/kebab_mach_wait_ipc_impl.h';fixture=q/'ipc-port-state-production-test.c';assert fixture.read_text().count((source/header).read_text())==1
host=json.loads((q/'test-ipc-port-state-result-0475.json').read_text());assert host['passed'] and host['cases']==240 and host['compiled_behavioral_mutants']==13 and not host['physical_test'] and sha(fixture)==host['fixture_sha256']
native=json.loads((q/'native-ios-identity-build-0475.json').read_text());link=json.loads((q/'native-ipc-receiver-link-0475.json').read_text());assert native['passed'] and native['complete_native_build'] and link['passed'] and link['source_unchanged_across_full_build']
assert manifest==json.loads((q/'all-mac-source-0475-postbuild.json').read_text())['files']
with tempfile.TemporaryDirectory(prefix='ipc-port-state-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 subprocess.run(['python3',str(repo/'scripts/qualification/replay-xnu.py'),'--repo',str(repo),'--xnu-source',str(source),'--manifest',str(q/'all-mac-source-0475.json'),'--receipt',str(temp/'fresh-full-replay.json'),'--cache',str(cache),'--expected-common','334'],check=True)
 for n in changes:
  target=tree/n;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,target)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0355-kebab-observe-bounded-ipc-port-destination-state.patch')],cwd=tree,check=True)
 for n in changes:assert sha(tree/n)==parent[n]
 work=temp/'host';work.mkdir();shutil.copyfile(fixture,work/fixture.name);subprocess.run(['python3',str(q/'run-tests.py'),str(work)],check=True)
 work=temp/'parse';work.mkdir()
 for n in ('0475-media-boot.log','0475-capture-provenance.json','parse-ipc-port-state-0475.py'):shutil.copyfile(q/n,work/n)
 subprocess.run(['python3',str(work/'parse-ipc-port-state-0475.py')],check=True)
 parsed=json.loads((work/'0475-IPC-observations.json').read_text());receipt=json.loads((q/'0475-IPC-qualified.json').read_text());crypto=receipt['original_crypto_regression'];raw=q/'0475-media-boot.log';s=raw.read_text()
 assert parsed['rows']==receipt['rows'] and parsed['capture_closed'] and receipt['accepted'] and receipt['native_sha256']==native['native_sha256']
 assert len(parsed['rows'])==29 and parsed['port_state_records']==14 and parsed['in_transit_paths']==3
 assert parsed['unowned_head_kinds']==[4] and not parsed['atomic_destination_path_proven'] and not receipt['migration_complete'] and not receipt['system_shell_checkin_complete'] and not receipt['usable_springboard']
 assert sha(raw)==receipt['actual_log_sha256']==crypto['actual_log_sha256']==json.loads((q/'0475-capture-provenance.json').read_text())['sha256']
 assert crypto['accepted'] and crypto['qualified_watchdog_seconds']>=300 and not any(x in s for x in ('panic(cpu','completion timeout','probes blocked'))
 for op in ('cached-read','nocache-read','write'):assert 'KEBAB A PROBE: locked already-open '+op+' result=1 residual=65559;' in s
 assert 'KEBAB A PROBE: private file lifecycle PASS (0), unlock=0;' in s and 'KEBAB B PROBE: private file lifecycle PASS (0), unlock=0;' in s
 first={}
 for sel,cls in re.findall(r'KEBAB AKS KEYCHAIN: selector=(\d+) class=(\d+) bytes=\d+/\d+ result=0;',s):first.setdefault(int(cls),int(sel))
 assert first=={k:11 for k in (6,7,8,9,10,11)}
 for cls,oldfile,field in [(1,'0471/0471-A-qualified.json','private_A_ciphertext_stripes'),(2,'0469/0469-B-CPX-qualified.json','B_private_ciphertext_stripes')]:
  stripes={str(offset):re.findall(r'class='+str(cls)+' logical='+str(offset)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for offset in (0,61440)}
  assert stripes==json.loads((repo/'docs/qualification'/oldfile).read_text())[field]
 for name in ('0475-defaults-qualified.json','0475-systembag-qualified.json'):assert json.loads((q/name).read_text())['actual_log_sha256']==sha(raw)
 assert json.loads((q/'0475-systembag-qualified.json').read_text())['stage_accepted']
 for name in ('0475-migrator-waits-symbolicated.json','0475-backboard-user-symbolicated.json','0475-ui-waits-symbolicated.json'):assert json.loads((q/name).read_text())['actual_log_sha256']==sha(raw)
print('PASS actual240/13 behavioral mutants, reverse0355, fresh334+4/5279source replay, native pins and sealed14states/3transit paths. Original encryption preserved; migration/shell/GUI remain unqualified.')
