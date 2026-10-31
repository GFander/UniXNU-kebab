"""Verify actual keychain C/D production code, models and sealed reboot evidence.

Requires the replayed 0464 source tree, clang/clang++, Git and OpenSSL headers.
This performs no device operations and needs no private firmware or keys.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,shutil,re
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0464.json').read_text())['files'];changes=json.loads((q/'candidate-manifest.json').read_text())['changes']
assert len(manifest)==5268 and len(changes)==5
for n,pins in changes.items():assert sha(source/n)==manifest[n]==pins['after']
core=json.loads((q/'test-keychain-core-result-0464.json').read_text());client=json.loads((q/'test-keychain-client-result-0464.json').read_text())
assert core['passed']and client['passed']and not core['physical_test']and not client['physical_test']
assert core['behavioral_mutants_rejected']==client['behavioral_mutants_rejected']==9
assert sha(q/'keychain-core-production-test.c')==core['production_fixture_sha256']
assert sha(q/'keychain-client-production-test.cpp')==client['fixture_sha256']
assert sha(q/'kebab_file_keys.h')==core['current_public_header_sha256']==manifest['bsd/kern/kebab_file_keys.h']
body=(source/'bsd/kern/kebab_file_keys.c').read_text();prefix=body[body.index('static LCK_GRP_DECLARE'):body.index('#include "kebab_class_b_math_impl.h"')]
fixture=(q/'keychain-core-production-test.c').read_text();assert fixture.count(prefix)==1
for n in ('kebab_device_classbag_impl.h','kebab_keychain_keys_impl.h'):
 text=(source/'bsd/kern'/n).read_text();assert fixture.count(text)==1
text=(source/'iokit/Kernel/arm/kebab_aks_userclient_impl.h').read_text()
for name in ('kebab_aks_cookie_impl.h','kebab_aks_keychain_impl.h'):text=text.replace('#include "'+name+'"',(source/'iokit/Kernel/arm'/name).read_text())
text='\n'.join(line for line in text.splitlines()if not line.startswith('#include'))
assert (q/'keychain-client-production-test.cpp').read_text().count(text)==1
native=json.loads((q/'native-ios-identity-build-0464.json').read_text());assert native['passed']and native['complete_native_build']
mapping=json.loads((q/'original-keychain-class-mapping-0464.json').read_text());assert mapping['CFEqual_stub_checked']
assert {x['keyclass']for x in mapping['mapping']}==set(range(6,13))
old=json.loads((repo/'docs/qualification/0463/all-mac-source-0463.json').read_text())['files']
with tempfile.TemporaryDirectory(prefix='keychain-cd-',dir=cache)as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 for n,pins in changes.items():
  d=tree/n;d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,d)
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(repo/'patches/xnu-8796-kebab/0349-kebab-connect-hardware-backed-keychain-cd-wrap.patch')],cwd=tree,check=True)
 for n,pins in changes.items():
  if pins['before']:assert sha(tree/n)==pins['before']==old[n]
  else:assert not(tree/n).exists()
 for family in ('core','client'):
  tests=temp/family;tests.mkdir()
  name='keychain-'+family+'-production-test.'+('c'if family=='core'else'cpp')
  shutil.copyfile(q/name,tests/name)
  if family=='core':shutil.copyfile(q/'kebab_file_keys.h',tests/'kebab_file_keys.h')
  subprocess.run(['python3',str(q/('run-'+family+'-tests.py')),str(tests)],check=True)
for version in ('0464','0465'):
 folder=q if version=='0464'else q.parent/'0465';receipt=json.loads((folder/(version+'-qualified-runtime.json')).read_text())
 raw=folder/(version+'-media-boot.log');text=raw.read_text();provenance=json.loads((folder/(version+'-capture-provenance.json')).read_text())
 assert sha(raw)==receipt['actual_log_sha256']==provenance['sha256']
 assert receipt['accepted']and receipt['actual_original_userland_wrap_unwrap']and receipt['qualified_watchdog_seconds']>=300
 assert receipt['native_sha256']==native['native_sha256']
 assert not receipt['underlying_classB_implemented']and not receipt['AK_AKU_passcode_implemented']and not receipt['all_original_keychain_implemented']and not receipt['usable_springboard']
 assert not any(s in text for s in ('panic(cpu','completion timeout','probes blocked'))
 for cls in (7,8,10,11):
  for selector in ((10,11)if version=='0464'else(11,)):assert re.search('KEBAB AKS KEYCHAIN: selector='+str(selector)+' class='+str(cls)+r' bytes=\d+/\d+ result=0;',text)
 if version=='0465':assert receipt['full_kernel_restart_reusing_exact_native']and set(receipt['cold_unwrap_before_any_new_wrap_classes'])=={7,8,10,11}
print('PASS actual keychain C/D core+complete client, 18 compiled behavioral mutants, exact reverse patch and two sealed real boots. A/B/passcode/full keychain/migration/SpringBoard remain incomplete.')
