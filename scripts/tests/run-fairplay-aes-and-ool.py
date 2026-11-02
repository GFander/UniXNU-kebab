"""Recheck production C++ models, source pins and sealed physical scope."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for n in ('repo','source','qualification','cache'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();source=a.source.resolve();q=a.qualification.resolve();cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((q/'all-mac-source-0482.json').read_text())['files'];assert len(manifest)==5280
assert manifest==json.loads((q/'all-mac-source-0482-postbuild.json').read_text())['files']
changes={}
for v in ('0481','0482'):
 changes.update(json.loads((q/('candidate-manifest-'+v+'.json')).read_text())['changes'])
parent=json.loads((repo/'docs/qualification/0479/all-mac-source-0479.json').read_text())['files']
assert set(changes)=={'iokit/Kernel/arm/NativeAES.cpp','iokit/Kernel/arm/KebabSurface.cpp'}
assert manifest=={**parent,**{n:z['after'] for n,z in changes.items()}}
for n,z in changes.items():assert z['before']==parent[n] and z['after']==sha(source/n)==manifest[n]
host_aes=json.loads((q/'test-fairplay-aes-result-0481.json').read_text());host_ool=json.loads((q/'test-surface-ool-result-0482.json').read_text())
assert host_aes['passed'] and host_aes['compiled_behavioral_mutants']==10 and host_aes['original_nonce_KDF_byte_identical']
assert host_ool['passed'] and host_ool['compiled_behavioral_mutants']==8 and host_ool['all_four_production_call_sites_present']
assert host_aes['production_source_sha256']==manifest['iokit/Kernel/arm/NativeAES.cpp'] and host_ool['source_sha256']==manifest['iokit/Kernel/arm/KebabSurface.cpp']
env=os.environ.copy();env['TMPDIR']=str(cache)
with tempfile.TemporaryDirectory(prefix='fairplay-aes-ool-',dir=cache) as temporary:
 work=Path(temporary).resolve();assert work.parent==cache
 for kind,v,unit,result in [('aes','0481','NativeAES.cpp',host_aes),('ool','0482','KebabSurface.cpp',host_ool)]:
  name='fairplay-aes-0481' if kind=='aes' else 'surface-ool-0482';fixture=work/name;fixture.mkdir()
  for file in (q/'tests'/kind).iterdir():
   if file.is_file():shutil.copyfile(file,fixture/file.name)
  dest=fixture/'candidate/iokit/Kernel/arm'/unit;dest.parent.mkdir(parents=True);shutil.copyfile(source/'iokit/Kernel/arm'/unit,dest)
  if kind=='aes':
   dest=fixture/'before/iokit/Kernel/arm/NativeAES.cpp';dest.parent.mkdir(parents=True);shutil.copyfile(fixture/'NativeAES-before.cpp',dest)
   assert sha(dest)==changes['iokit/Kernel/arm/NativeAES.cpp']['before']
  runner=work/('test-fairplay-aes-0481.py' if kind=='aes' else 'test-surface-ool-0482.py');shutil.copyfile(fixture/'run-tests.py',runner)
  subprocess.run(['python3',str(runner)],cwd=work,check=True,env=env)
  got=json.loads((fixture/('test-fairplay-aes-result-0481.json' if kind=='aes' else 'test-surface-ool-result-0482.json')).read_text())
  assert got['passed'] and got['fixture_sha256']==result['fixture_sha256'] and got['compiled_behavioral_mutants']==result['compiled_behavioral_mutants']
replay=json.loads((q/'all-source-replay-0482.json').read_text());assert replay['all_source_equal'] and (replay['common_patches'],replay['host_patches'],replay['all_source_files'])==(340,4,5280)
assert all(sha(repo/n)==h for n,h in replay['patch_sha256'].items())
assert len(list((repo/'patches/xnu-8796-kebab').glob('*.patch')))==340
native=json.loads((q/'native-ios-identity-build-0482.json').read_text());runtime=json.loads((q/'0482-fairplay-aes-ool-qualified.json').read_text())
assert native['passed'] and native['complete_native_build'] and runtime['native_sha256']==native['native_sha256']
assert runtime['capture_closed'] and runtime['actual_log_sha256']==sha(q/'0482-media-boot.log')
assert runtime['accepted'] and runtime['old_crypto_regression']['qualified_watchdog_seconds']>=300
raw=(q/'0482-media-boot.log').read_text();assert 'KEBAB AES FAIRPLAY: public KDF/selector/CBC/invalid-bits KAT PASS' in raw
assert 'selector=2201 operation=0 bytes=32 completed=1 result=0' in raw and 'IOGMD: not wired' not in raw and 'panic(cpu' not in raw
assert not any(runtime[k] for k in ('full_goal_complete','usable_springboard','migration_complete','system_shell_checkin_complete','full_FairPlay_compatibility'))
print('PASS actual production AES61/OOL24 model checks and10+8compiled behavioral mutants; exact340+4/5280 source and bounded sealed runtime; full FairPlay/migration/GUI unqualified')
