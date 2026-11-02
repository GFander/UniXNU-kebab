"""Replay actual production models and validate sealed scope; no device/private data access."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,tempfile
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args();q=a.qualification;load=lambda n:json.loads((q/n).read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
expected=load('all-mac-source-0485.json')['files'];assert expected==load('all-mac-source-0485-postbuild.json')['files'] and len(expected)==5281
for name in ('all-source-replay-0485.json','public-all-source-replay-0485.json'):
 replay=load(name);assert replay['all_source_equal'] and (replay['common_patches'],replay['host_patches'],replay['all_source_files'])==(343,4,5281)
 for n,pin in replay['patch_sha256'].items():assert sha(a.repo/n)==pin,n
tests=a.repo/'scripts/tests/shell-frame';a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='shell-frame-',dir=a.cache) as temp:
 work=Path(temp)
 prepared=work/'prepared'
 subprocess.run(['git','clone','--quiet','--shared','--no-checkout',str(a.source),str(prepared)],check=True)
 subprocess.run(['git','-C',str(prepared),'reset','--quiet','--hard','1b191cb58250d0705d8a51287127505aa4bc0789'],check=True)
 common=sorted((a.repo/'patches/xnu-8796-kebab').glob('*.patch'));hosts=sorted((a.repo/'patches/hosts/macos-dxb-13.5').glob('*.patch'));assert len(common)==343 and len(hosts)==4
 for patch in common+hosts:
  subprocess.run(['git','-C',str(prepared),'apply','--check','--whitespace=nowarn',str(patch)],check=True)
  subprocess.run(['git','-C',str(prepared),'apply','--whitespace=nowarn',str(patch)],check=True)
 actual={}
 for p in sorted(prepared.rglob('*')):
  rel=p.relative_to(prepared)
  if '.git' in rel.parts or '__pycache__' in rel.parts or p.name in ('.DS_Store','compile_commands.json') or p.suffix=='.pyc':continue
  if p.is_file():actual[rel.as_posix()]=sha(p)
 assert actual==expected,'Fresh common+Mac-host replay differs from native inputs'
 for script,receipt in [('run-production-tests.py','test-shell-frame-result-0485.json'),('run-identity-tests.py','test-shell-identity-result-0485.json')]:
  subprocess.run([sys.executable,str(tests/script),'--source',str(prepared),'--work',str(work)],check=True,stdout=subprocess.DEVNULL)
  new=json.loads((work/receipt).read_text());old=load(receipt);assert new['passed'] and new['compiled_behavioral_mutants']==old['compiled_behavioral_mutants'] and new['fixture_sha256']==old['fixture_sha256']
 parser=work/'parse-shell-frame-0485.py';shutil.copyfile(tests/'parse-shell-frame.py',parser);observations={}
 for name in ('0485-media-boot.log','0485-shell-frame-1.log'):
  shutil.copyfile(q/name,work/name);result=subprocess.run([sys.executable,str(parser),name],check=True,capture_output=True,text=True);observations[name]=json.loads(result.stdout)
runtime=load('0485-shell-frame-qualified.json');crypto=load('0485-A-qualified.json');late=load('0485-shell-frame-1-provenance.json')
assert runtime['accepted'] and runtime['original_checkin_nil_error_completion_observed'] and runtime['system_shell_checkin_complete'] and crypto['accepted'] and crypto['qualified_watchdog_seconds']>=300
assert observations['0485-shell-frame-1.log']['original_success_completion_observed'] and not observations['0485-media-boot.log']['original_success_completion_observed']
assert sha(q/'0485-media-boot.log')==runtime['sealed_regression_sha256']==crypto['actual_log_sha256'] and sha(q/'0485-shell-frame-1.log')==runtime['late_log_sha256']==late['actual_log_sha256']
assert late['serial_released'] and len(late['completed'])==5 and load('0485-migration-state-summary-reviewed.json')['original_success_for_20H392_observed']
assert not any(runtime[n] for n in ('full_goal_complete','usable_springboard','panel_scanout_proven','full_backend_registration_lifetime_proven'))
assert load('0484-superseded-unflashed.json')['flashed'] is False
for name in ('0485-media-boot.log','0485-shell-frame-1.log','0483-shell-frame-1.log'):assert 'KEBAB MGS DATA:' not in (q/name).read_text()
print('PASS343+4/5281 actual source,24+6 production model checks/13+5 compiled mutants, original completion0/1 and sealed crypto318s+; no GUI or private-data claim')
