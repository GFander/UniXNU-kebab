"""Reproduce the exact bounded FBS validity observer and its user-read guards."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;receipt=json.loads((q/'test-fbs-mode-evidence-result-0442.json').read_text());assert receipt['passed']
raw=(a.source/'bsd/kern/kern_exit.c').read_bytes();s=raw.decode();NL=chr(10)
assert hashlib.sha256(raw).hexdigest()==receipt['source_sha256']==json.loads((q/'all-mac-source-0442.json').read_text())['files']['bsd/kern/kern_exit.c']
start=s.index('/* Matched20H392 FrontBoardServices');end=s.index('/* First real exit per selected daemon,',start)
call=' kebab_fbs_mode_failure_evidence(p,rv);'+NL
parent=s[:start]+s[end:];assert parent.count(call)==1;parent=parent.replace(call,'')
assert hashlib.sha256(parent.encode()).hexdigest()==receipt['parent_sha256']==json.loads((q/'all-mac-source-0441.json').read_text())['files']['bsd/kern/kern_exit.c']
at=s.index('static void'+NL+'kebab_fbs_mode_failure_evidence(',start);helper=s[at:end]
raw=(q/'fbs-mode-evidence-production-test.cpp').read_bytes();fixture=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']and helper in fixture
variants=[('actual',fixture,True)]
def variant(name,old,new,count=1):
 assert fixture.count(old)==count;variants.append((name,fixture.replace(old,new),False))
variant('foreign-task-read','p != current_proc() ||','false ||')
variant('wrong-pc-read','saved.pc != 0x19b91abd4ULL','false')
variant('skip-initial-type','(isa & 0x0000000ffffffff8ULL) != 0x1e1dd3380ULL','false')
variant('read-allocation-padding','copyin(object, mode, 73)','copyin(object, mode, 80)')
variant('wrong-width-field','(unsigned long long)mode[1]','(unsigned long long)mode[2]')
variant('over-cap','if (slot >= 8) return;','if (slot >= 9) return;',2)
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fbs-mode-evidence-',dir=a.cache)as td:
 for name,text,ok in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==ok,(name,result.stdout,result.stderr)
  if ok:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected',name)
print('PASS entire parent source restored; actual observer13cases/6behavioralmutants; proc/Mach/copyin are explicit models, no original exit policy changes')
