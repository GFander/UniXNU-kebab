"""Check complete fixed receiver production and sealed actual wait evidence."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
for n in ('source','qualification','cache'):ap.add_argument('--'+n,type=Path,required=True)
a=ap.parse_args();q=a.qualification;sha=lambda b:hashlib.sha256(b).hexdigest()
manifest=json.loads((q/'all-mac-source-0460.json').read_text())['files']
receipt=json.loads((q/'test-location-receiver-result-0460.json').read_text());assert receipt['passed']
raw=(q/'location-receiver-production-test.cpp').read_bytes();fixture=raw.decode();assert sha(raw)==receipt['fixture_sha256']
for n,h in receipt['actual_complete_source_sha256'].items():
 source=(a.source/n).read_text();assert sha(source.encode())==h==manifest[n]
 source=re.sub(r'^#include [^\n]+\n','',source,flags=re.M)
 for old,new in [('unsigned *count = arg;','unsigned *count = (unsigned *)arg;'),('struct kebab_backboard_snapshot *snapshot = argument;','struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;'),('struct kebab_ui_snapshot *snapshot = argument;','struct kebab_ui_snapshot *snapshot = (struct kebab_ui_snapshot *)argument;'),('struct kebab_migrator_snapshot *snapshot = argument;','struct kebab_migrator_snapshot *snapshot = (struct kebab_migrator_snapshot *)argument;')]:source=source.replace(old,new)
 assert source in fixture,n
log=(q/'0460-media-boot.log').read_bytes();text=log.decode();runtime=json.loads((q/'0460-qualified-runtime.json').read_text())
assert sha(log)==runtime['actual_log_sha256']==json.loads((q/'0460-capture-provenance.json').read_text())['sha256']
assert runtime['accepted']and not runtime['root_cause_fixed']and not runtime['full_goal_complete']
assert text.count('MIG actual completion observed')==3 and text.count('UI actual completion observed')==1
assert text.count('KEBAB MIG-L: snapshot end;')==3 and not any(s in text for s in ('panic(cpu','completion timeout'))
symbolic=json.loads((q/'0460-migrator-waits-symbolicated.json').read_text());assert symbolic['actual_log_sha256']==sha(log)
assert len([p for p in symbolic['rows']if p['label']=='MIG-L'and p.get('unslid_pc')=='0x100a3893c'and p.get('image')=='original locationd'])==3
native=json.loads((q/'0460-flash.json').read_text())['kernel_sha256'];assert native==runtime['native_sha256']==json.loads((q/'native-ios-identity-build-0460.json').read_text())['native_sha256']

def variants_for(fixture):
 variants=[]
 def change(name,old,new):
  assert fixture.count(old)==1,(name,fixture.count(old));variants.append((name,fixture.replace(old,new)))
 change('wrong-fixed-name','"installcoordinationd", "locationd"','"installcoordinationd", "location"')
 change('wrong-location-label','"MIG", "MIG-W", "MIG-I", "MIG-L"','"MIG", "MIG-W", "MIG-I", "MIG-X"')
 change('wrong-original-uuid','0x72, 0x36, 0xf5, 0x20','0x73, 0x36, 0xf5, 0x20')
 change('omit-location-uuid','i == 3 ? locationd_uuid : NULL','i == 99 ? locationd_uuid : NULL')
 change('dispatch-location-as-installer','else kebab_locationd_task_report(snapshot.task);','else kebab_installer_task_report(snapshot.task);')
 change('short-location-frames','kebab_task_threads_report(task, "MIG-L", true, 16);','kebab_task_threads_report(task, "MIG-L", true, 8);')
 start=fixture.index('static int\nkebab_migrator_select(');end=fixture.index('\n}\n',start)+3;body=fixture[start:end]
 for name,old,new in [
  ('accept-other-parent','proc_ppid(process) == 1','proc_ppid(process) != 0'),
  ('force-root-uid','snapshot->uid = proc_getuid(process);','snapshot->uid = 0;'),
  ('remove512bound','snapshot->scanned >= 512','snapshot->scanned >= 513')]:
  assert body.count(old)==1;variants.append((name,fixture[:start]+body.replace(old,new)+fixture[end:]))
 return variants

a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='location-receiver-',dir=a.cache)as td:
 for index,(name,source)in enumerate([('actual',fixture)]+variants_for(fixture)):
  p=Path(td)/(name+'.cpp');p.write_text(source);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-missing-field-initializers','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
  if not index:assert result.returncode==0,(name,result.stdout,result.stderr);print(result.stdout.strip())
  else:assert result.returncode and 'Assertion'in result.stderr,(name,result.stdout,result.stderr);print('Rejected',name,flush=True)
print('PASS complete fixed original locationd observer/legacy regressions/9behavioral mutants and sealed actual3sample provenance; failed VFS operation and GUI remain unresolved')
