"""Recheck exact production ring, full native source replay and sealed boot.

No device access. Kernel proc/UUID/scheduler/locks remain explicit host models.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0479.json').read_text())['files'];parent=json.loads((repo/'docs/qualification/0478/all-mac-source-0478.json').read_text())['files'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5280 and len(changes)==2 and manifest=={**parent,**{n:pin['after'] for n,pin in changes.items()}}
assert sha(q/'fairplay-services-0479/syscalls.master')==manifest['bsd/kern/syscalls.master']
for n,pin in changes.items():
 assert sha(source/n)==manifest[n]==pin['after']
 assert parent.get(n)==pin['before']
header='bsd/kern/kebab_fairplay_bootdiag_impl.h';fixture=q/'fairplay-services-production-test.c'
assert fixture.read_text().count((source/header).read_text())==1
host=json.loads((q/'test-fairplay-services-result-0479.json').read_text());assert host['passed'] and host['cases']==789 and host['compiled_behavioral_mutants']==35 and not host['physical_test'] and sha(fixture)==host['fixture_sha256']
cpp=q/'fairplay-match-production-test.cpp';helper=q/'match-observer-production.inc'
assert cpp.read_text().count(helper.read_text())==1 and (source/'iokit/Kernel/IOUserClient.cpp').read_text().count(helper.read_text())==1
assert host['matching_dictionary_model_cases']==7 and host['matching_compiled_behavioral_mutants']==3 and sha(cpp)==host['matching_fixture_sha256']
assert json.loads((q/'test-production-inputs.json').read_text())['matching_helper_sha256']==sha(helper)
native=json.loads((q/'native-ios-identity-build-0479.json').read_text());link=json.loads((q/'native-fairplay-services-link-0479.json').read_text())
assert native['passed'] and native['complete_native_build'] and link['passed'] and link['source_unchanged_across_full_build']
assert link['A_B_CD_keychain_hardware_Root_sources_unchanged'] and link['original_syscall_ptrace_MACF_exit_logic_preserved'] and link['all_original_TU_source_lines_retained'] and len(link['actual_native_call_edges'])==10
assert manifest==json.loads((q/'all-mac-source-0479-postbuild.json').read_text())['files']
with tempfile.TemporaryDirectory(prefix='fairplay-services-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 subprocess.run(['python3',str(repo/'scripts/qualification/replay-xnu.py'),'--repo',str(repo),'--xnu-source',str(source),'--manifest',str(q/'all-mac-source-0479.json'),'--receipt',str(temp/'fresh-full-replay.json'),'--cache',str(cache),'--expected-common','338'],check=True)
 for n in changes:
  target=tree/n;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,target)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0359-kebab-observe-original-fairplay-service-results.patch')],cwd=tree,check=True)
 for n,pin in changes.items():
  if pin['before']:assert sha(tree/n)==pin['before']
  else:assert not (tree/n).exists()
 work=temp/'host';work.mkdir();shutil.copyfile(fixture,work/fixture.name);shutil.copyfile(cpp,work/cpp.name);subprocess.run(['python3',str(q/'run-tests.py'),str(work)],check=True)
 work=temp/'parse';work.mkdir()
 for n in ('0479-media-boot.log','0479-capture-provenance.json','parse-fairplay-syscalls-0479.py'):shutil.copyfile(q/n,work/n)
 shutil.copytree(q/'fairplay-services-0479',work/'fairplay-services-0479')
 result=subprocess.run(['python3',str(work/'parse-fairplay-syscalls-0479.py')],check=True,capture_output=True,text=True);print(result.stdout.splitlines()[0])
 parsed=json.loads((work/'0479-fairplay-syscalls-observations.json').read_text());receipt=json.loads((q/'0479-fairplay-syscalls-qualified.json').read_text());crypto=receipt['original_crypto_regression'];raw=q/'0479-media-boot.log';s=raw.read_text()
 assert parsed=={k:receipt[k] for k in parsed} and parsed['capture_closed'] and receipt['accepted'] and receipt['native_sha256']==native['native_sha256']
 assert (parsed['total_completed_BSD_calls'],parsed['retained_calls'],parsed['first_retained_sequence'])==(138,32,107)
 deny=parsed['first_deny_attach'];assert (deny['sequence'],deny['syscall'],deny['request'],deny['error'],deny['flags'],deny['debugger'])==(100,26,31,0,2,False)
 assert (parsed['exit_signal'],parsed['exit_status'])==(0,0)
 assert sum(row['error']==2 for row in parsed['rows'])==3
 assert next(row for row in parsed['rows'] if row['syscall']==381)['syscall_name']=='__mac_syscall'
 assert next(row for row in parsed['rows'] if row['syscall']==372)['syscall_name']=='thread_selfid'
 assert parsed['rows'][-1]['error']==-2 and parsed['rows'][-1]['syscall']==368
 assert not receipt['exit_cause_proven'] and not receipt['migration_complete'] and not receipt['system_shell_checkin_complete'] and not receipt['usable_springboard'] and not receipt['full_goal_complete']
 assert sha(raw)==receipt['actual_log_sha256']==crypto['actual_log_sha256']==json.loads((q/'0479-capture-provenance.json').read_text())['sha256']
 assert crypto['accepted'] and crypto['qualified_watchdog_seconds']>=300 and not any(x in s for x in ('panic(cpu','completion timeout','probes blocked'))
 for op in ('cached-read','nocache-read','write'):assert 'KEBAB A PROBE: locked already-open '+op+' result=1 residual=65559;' in s
 assert 'KEBAB A PROBE: private file lifecycle PASS (0), unlock=0;' in s and 'KEBAB B PROBE: private file lifecycle PASS (0), unlock=0;' in s
 first={}
 for sel,cls in re.findall(r'KEBAB AKS KEYCHAIN: selector=(\d+) class=(\d+) bytes=\d+/\d+ result=0;',s):first.setdefault(int(cls),int(sel))
 assert first=={k:11 for k in (6,7,8,9,10,11)}
 for cls,oldfile,field in [(1,'0471/0471-A-qualified.json','private_A_ciphertext_stripes'),(2,'0469/0469-B-CPX-qualified.json','B_private_ciphertext_stripes')]:
  stripes={str(offset):re.findall(r'class='+str(cls)+' logical='+str(offset)+r' ciphertext-sha256=([0-9a-f]{64}) differs from plaintext',s) for offset in (0,61440)}
  assert stripes==json.loads((repo/'docs/qualification'/oldfile).read_text())[field]
 for n in ('parse-fairplay-services-0479.py',):shutil.copyfile(q/n,work/n)
 subprocess.run(['python3',str(work/'parse-fairplay-services-0479.py')],check=True,capture_output=True,text=True)
 resource=json.loads((work/'0479-fairplay-services-observations.json').read_text());actual=json.loads((q/'0479-fairplay-services-qualified.json').read_text())
 assert resource=={k:actual[k] for k in resource} and actual['accepted'] and not actual['all_IOKit_or_Mach_calls_observed'] and not actual['atomic_request_pairing_proven']
 assert len(resource['rows'])==7 and not resource['budget_reached'] and all(not z['copy_error'] and not z['truncated'] and not z['sanitized'] for z in resource['rows'])
 matches=[z for z in resource['rows'] if z['kind']==5];assert len(matches)==1 and matches[0]['name']=='com_apple_driver_FairPlayIOKit' and matches[0]['result']==0 and matches[0]['aux']==0
 assert actual['actual_FairPlayIOKit_class_consumer_proven'] and actual['original_plural_matching_nil_iterator'] and not actual['unique_exit_cause_proven']
 assert actual['actual_log_sha256']==sha(raw)
 for name in ('0479-defaults-qualified.json','0479-systembag-qualified.json'):assert json.loads((q/name).read_text())['actual_log_sha256']==sha(raw)
 assert json.loads((q/'0479-systembag-qualified.json').read_text())['stage_accepted']
 for name in ('0479-migrator-waits-symbolicated.json','0479-backboard-user-symbolicated.json','0479-ui-waits-symbolicated.json'):assert json.loads((q/name).read_text())['actual_log_sha256']==sha(raw)
print('PASS789C/7C++/35+3 compiled behavioral mutants, reverse0359, fresh338+4/5280source replay, native callback edges and sealed return ring. Original encryption preserved; FairPlay cause, migration/shell/GUI remain unqualified.')
