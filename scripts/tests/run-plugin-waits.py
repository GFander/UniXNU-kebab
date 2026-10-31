"""Validate exact complete BSD/Mach fixed-plugin diagnostic bodies and behavior.
The kernel API/scheduler/map models are explicit and do not replace device tests.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
for name in ('source','qualification','cache'):ap.add_argument('--'+name,type=Path,required=True)
a=ap.parse_args();q=a.qualification
manifest=json.loads((q/'all-mac-source-0449.json').read_text())['files']
receipt=json.loads((q/'test-plugin-waits-result-0449.json').read_text());assert receipt['passed']
files=['osfmk/kern/kebab_bootdiag.c','bsd/kern/kebab_bootdiag.c']
strip=lambda text:re.sub(r'^#include [^\n]+\n','',text,flags=re.M)
sources={n:(a.source/n).read_text()for n in files}
for n in files:assert hashlib.sha256(sources[n].encode()).hexdigest()==receipt['actual_production_sources'][n]==manifest[n]
prod='\n'.join(strip(sources[n])for n in files)
for old,new,count in [('unsigned *count = arg;','unsigned *count = (unsigned *)arg;',1),('struct kebab_backboard_snapshot *snapshot = argument;','struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;',1),('struct kebab_ui_snapshot *snapshot = argument;','struct kebab_ui_snapshot *snapshot = (struct kebab_ui_snapshot *)argument;',1),('struct kebab_migrator_snapshot *snapshot = argument;','struct kebab_migrator_snapshot *snapshot = (struct kebab_migrator_snapshot *)argument;',1)]:
 assert prod.count(old)==count;prod=prod.replace(old,new)
raw=(q/'plugin-waits-production-test.cpp').read_bytes();s=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256'] and prod in s
# USB parsing/normal-only/panic handling is unchanged from the prior qualified observer.
old=json.loads((q/'test-migrator-usb-result-0446.json').read_text())
usb=a.source/'bsd/dev/kebab_usb.c';assert old['passed'] and hashlib.sha256(usb.read_bytes()).hexdigest()==manifest['bsd/dev/kebab_usb.c']==old['source_sha256']
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='plugin-waits-',dir=a.cache)as td:
 p=Path(td)/'production.cpp';variants=[]
 def change(name,old,new,expected=1):
  assert s.count(old)==expected,(name,s.count(old));variants.append((name,s.replace(old,new)))
 change('ignore-wrapper-uuid','snapshot->expected_uuid && memcmp(uuid, snapshot->expected_uuid, sizeof(uuid))','snapshot->expected_uuid && false && memcmp(uuid, snapshot->expected_uuid, sizeof(uuid))')
 change('wrong-parent','!strcmp(name, snapshot->name) && proc_ppid(process) == 1','!strcmp(name, snapshot->name) && proc_ppid(process) != 0',2)
 change('missing-process-cap','snapshot->scanned >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;','snapshot->scanned >= 513 ? PROC_RETURNED_DONE : PROC_RETURNED;',3)
 change('wrong-wrapper-label','kebab_task_threads_report(task, "MIG-W", true, 16);','kebab_task_threads_report(task, "UI-SB", true, 16);')
 change('wrong-dyld-format','info.all_image_info_format != TASK_DYLD_ALL_IMAGE_INFO_64','false')
 change('ignore-dyld-count','size != TASK_DYLD_INFO_COUNT','false')
 change('ignore-dyld-size','info.all_image_info_size < 16','false')
 change('ignore-dyld-version','(uint32_t)head[0] >= 1 && (uint32_t)head[0] <= 20','true')
 change('ignore-prefix-change','head[0] == again[0] && head[1] == again[1]','true')
 change('ignore-path-prefix','memcmp(first, prefix, sizeof(first))','false')
 change('record-cap513','count < 512 ? count : 512','count < 513 ? count : 513')
 change('selected-cap9','scanned < limit && found < 8','scanned < limit && found < 9')
 change('ignore-mach-header-magic','header[0] != 0xfeedfacfu','false')
 change('ignore-command-count','header[4] > 128','false')
 change('ignore-command-extent','header[5] > 16384','false')
 change('accept-wrong-uuid-command-size','command[1] == 24 &&','command[1] >= 23 &&')
 variants.insert(0,('actual',s))
 results=[]
 for name,text in variants:
  q=p.with_name('plugin-waits-'+name+'.cpp');q.write_text(text);exe=q.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-missing-field-initializers','-fsanitize=address,undefined',str(q),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  if name=='actual':
   assert result.returncode==0,(result.stdout,result.stderr);print(result.stdout.strip())
  else:
   assert result.returncode!=0 and ('Assertion' in result.stderr or 'AddressSanitizer' in result.stderr),(name,result.stdout,result.stderr)
   print('Rejected',name,flush=True);results.append(name)
print('PASS exact production sources, unchanged USB dispatcher, complete observer fixture and16behavioral mutants; runtime/GUI qualification is separate')
