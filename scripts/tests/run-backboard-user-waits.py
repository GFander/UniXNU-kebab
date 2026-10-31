"""Test actual bounded call-site sampler bodies; kernel, VM and scheduler are explicit models."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--source',type=Path,required=True)
ap.add_argument('--qualification',type=Path,required=True)
ap.add_argument('--cache',type=Path,required=True)
a=ap.parse_args();q=a.qualification
manifest=json.loads((q/'all-mac-source-0436.json').read_text())['files']
receipt=json.loads((q/'test-backboard-user-result.json').read_text());assert receipt['passed']
sources={}
for name,expected in receipt['actual_production_sources'].items():
 raw=(a.source/name).read_bytes();assert hashlib.sha256(raw).hexdigest()==expected==manifest[name]
 sources[name]=raw.decode()
strip=lambda s:re.sub(r'^#include [^\n]+\n','',s,flags=re.M)
body=strip(sources['osfmk/kern/kebab_bootdiag.c'])+'\n'+strip(sources['bsd/kern/kebab_bootdiag.c']).replace('unsigned *count = arg;','unsigned *count = (unsigned *)arg;').replace('struct kebab_backboard_snapshot *snapshot = argument;','struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;')
raw=(q/'backboard-user-production-test.cpp').read_bytes();s=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256'] and body in s
mutations=[('wrong-name','!strcmp(name, "backboardd")','true'),('leak-task','task_deallocate(snapshot.task);',';'),('ignore-context','if (ml_at_interrupt_context() || get_preemption_level() != 0 || !ml_get_interrupts_enabled()) return;','if(false) return;'),('running-user','if ((thread->state & (TH_WAIT | TH_RUN)) != TH_WAIT) return false;','if(false) return false;'),('wrong-flavor','!is_saved_state64(&saved)','false'),('wrong-count','saved.ash.count != ARM_SAVED_STATE64_COUNT','(saved.ash.count != ARM_SAVED_STATE64_COUNT && false)'),('kernel-map','vm_map_pmap(map) == pmap_kernel()','false'),('over-cap','i < 8; i++','i < 9; i++'),('no-map-edge','vm_map_max(map) - fp < sizeof(pair)','false'),('leak-map','if (map != VM_MAP_NULL) vm_map_deallocate(map);',';')]
variants=[('actual',s,True)]
for name,before,after in mutations:
 assert s.count(before)==1,(name,s.count(before));variants.append((name,s.replace(before,after),False))
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='bboard-user-',dir=a.cache)as td:
 for name,text,expected in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==expected,(name,result.stdout,result.stderr)
  if expected:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected behavioral mutation',name)
print('PASS exact current sampler bodies and ten behavioral mutants; no hardware/consistent-stack claim')
