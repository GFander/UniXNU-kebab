"""Verify pinned classB math foundation and original AKS metadata evidence.

Requires a replayed 0463 XNU tree, clang/clang++, Git and OpenSSL development
headers. No private firmware, hardware key, new device operation or Python
cryptography package is needed; all supplied scalars are public test-only data.
"""
from pathlib import Path
import argparse,hashlib,json,re,tempfile,subprocess,shutil
ap=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):ap.add_argument('--'+n,type=Path,required=True)
a=ap.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
test=json.loads((q/'test-x25519-component-result-0463.json').read_text());assert test['passed'] and test['independent_vectors']==test['kdf_independent_vectors']==514
assert not test['classB_backend_implemented'] and not test['physical_test']
native=json.loads((q/'native-ios-identity-build-0463.json').read_text());assert native['passed']
manifest=json.loads((q/'all-mac-source-0463.json').read_text())['files']
primitive=source/'libkern/crypto/kebab_curve25519'
assert {n:sha(primitive/n) for n in test['source_sha256']}==test['source_sha256']
assert sha(q/'vectors.h')==test['vectors_sha256'] and sha(q/'test.c')==test['test_sha256']
integration=json.loads((q/'test-class-b-integration-0463.json').read_text());assert integration['passed']
for n,h in integration['fixture_sha256'].items():assert sha(q/n)==h
for n,h in integration['actual_source_sha256'].items():assert sha(source/n)==h==manifest[n]
runtime_header=(source/'bsd/kern/kebab_class_b_math_impl.h').read_text();assert (q/'runtime-math-production-test.c').read_text().count(runtime_header)==1
cookie=(source/'iokit/Kernel/arm/kebab_aks_cookie_impl.h').read_text()
client=(source/'iokit/Kernel/arm/kebab_aks_userclient_impl.h').read_text().replace('#include "kebab_aks_cookie_impl.h"',cookie)
client='\n'.join(line for line in client.splitlines() if not line.startswith('#include'))
assert (q/'aks-wrap-meta-production-test.cpp').read_text().count(client)==1
upstream=json.loads((q/'upstream.json').read_text());assert upstream['archive_sha256']=='6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14'
for n,h in upstream['files'].items():assert sha(primitive/n)==h['adapted_sha256']
mutations=[('low-order','kebab_x25519.c','ok &= (nonzero != 0);','ok |= (nonzero == 0);'),
 ('point-high-bit','ec_c25519_m31.c','G[31] &= 0x7F;','G[31] &= 0xFF;'),
 ('scalar-clamp','ec_c25519_m31.c','k[0] &= 0xF8;','k[0] &= 0xFF;'),
 ('failed-output','kebab_x25519.c','if(ok)memcpy(output,work,32);','memcpy(output,work,32);'),
 ('wipe-scalar','ec_c25519_m31.c','\tkebab_x25519_wipe(k, sizeof k);','\t/* intentionally omitted scalar wipe */'),
 ('kdf-counter','kebab_x25519.c','input[100]={0,0,0,1}','input[100]={0,0,0,2}'),
 ('kdf-parties','kebab_x25519.c','memcpy(input+36,ephemeral_public,32);memcpy(input+68,class_public,32);','memcpy(input+36,class_public,32);memcpy(input+68,ephemeral_public,32);'),
 ('kdf-provider-error','kebab_x25519.c','if(ok)memcpy(output,digest,32);','memcpy(output,digest,32);'),
 ('kdf-zero-secret','kebab_x25519.c','if(!nonzero)return 0;','if(!nonzero && !hash)return 0;')]
with tempfile.TemporaryDirectory(prefix='class-b-crypto-',dir=cache) as directory:
 temp=Path(directory);tree=temp/'tree';tree.mkdir();subprocess.run(['git','init','--quiet',str(tree)],check=True)
 names=tuple(integration['actual_source_sha256'])+('bsd/kern/kebab_class_b_math_impl.h',)+tuple('libkern/crypto/kebab_curve25519/'+n for n in test['source_sha256'])
 for n in names:
  assert sha(source/n)==manifest[n];dest=tree/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,dest)
 patch=repo/'patches/xnu-8796-kebab/0348-kebab-qualify-classB-crypto-and-original-wrap-metadata.patch'
 subprocess.run(['git','apply','--reverse','--whitespace=nowarn',str(patch)],cwd=tree,check=True)
 oldq=repo/'docs/qualification/0462/all-mac-source-0462.json';old=json.loads(oldq.read_text())['files']
 assert all(sha(tree/n)==old[n] for n in integration['actual_source_sha256'])
 assert all(not(tree/n).exists() for n in names if n not in integration['actual_source_sha256'])
 def build(folder,label,fixture,audit):
  binary=temp/(label+'.bin');cmd=['clang','-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(q)]
  if audit:cmd+=['-DKEBAB_X25519_WIPE_AUDIT=1']
  result=subprocess.run(cmd+[str(folder/'kebab_x25519.c'),str(fixture),'-lcrypto','-o',str(binary)],capture_output=True,text=True)
  assert result.returncode==0,(label,result.stderr)
  return subprocess.run([str(binary)],capture_output=True,text=True)
 baseline=build(primitive,'actual',q/'test.c',True);assert baseline.returncode==0,baseline.stderr
 for label,name,old,new in mutations:
  folder=temp/label;folder.mkdir()
  for n in test['source_sha256']:shutil.copyfile(primitive/n,folder/n)
  p=folder/name;s=p.read_text();assert s.count(old)==1;p.write_text(s.replace(old,new))
  result=build(folder,label,q/'test.c',True);assert result.returncode!=0,(label,'mutant passed')
 runtime=build(primitive,'runtime',q/'runtime-math-production-test.c',False);assert runtime.returncode==0,runtime.stderr
 binary=temp/'client';result=subprocess.run(['clang++','-std=c++17','-pthread','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(q/'aks-wrap-meta-production-test.cpp'),'-o',str(binary)],capture_output=True,text=True);assert result.returncode==0,result.stderr
 result=subprocess.run([str(binary)],capture_output=True,text=True);assert result.returncode==0,result.stderr
raw=q/'0463-media-boot.log';text=raw.read_text();runtime=json.loads((q/'0463-qualified-runtime.json').read_text());provenance=json.loads((q/'0463-capture-provenance.json').read_text())
assert sha(raw)==runtime['actual_log_sha256']==provenance['sha256']
assert runtime['accepted'] and runtime['actual_runtime_math_KAT_passed'] and runtime['qualified_watchdog_seconds']>=300
assert not runtime['underlying_classB_implemented'] and not runtime['new_authorization_grant'] and not runtime['usable_springboard']
assert text.count('KEBAB CLASS-B CRYPTO: X25519/ConcatKDF KAT PASS; math only, classB services unavailable')==1
assert not any(s in text for s in ('panic(cpu','completion timeout','probes blocked','KAT FAIL'))
assert len(runtime['wrap_metadata'])==16 and {x['key_class'] for x in runtime['wrap_metadata']}=={7,9,10,11}
assert all(x['pid']==102 and not x['administrative'] and x['result']=='0xe00002c1' for x in runtime['wrap_metadata'])
print('PASS514X25519+514KDF/1000iterations/9behavioralmutants, complete runtimeKAT and16572legacyclient cases; reversepatch preserves parents, actual mathKAT+securityd metadata. ClassB/keychain/backend/auth/GUI still incomplete.')
