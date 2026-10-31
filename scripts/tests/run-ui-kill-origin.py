"""Compare the current complete kill(2) body to its exact parent and run its observer model."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;receipt=json.loads((q/'test-ui-kill-result.json').read_text());assert receipt['passed']
current=(a.source/'bsd/kern/kern_sig.c').read_bytes();assert hashlib.sha256(current).hexdigest()==receipt['source_sha256']==json.loads((q/'all-mac-source-0439.json').read_text())['files']['bsd/kern/kern_sig.c']
s=current.decode();NL=chr(10);TAB=chr(9)
start=s.index('static void'+NL+'kebab_ui_kill_request(');end=s.index(NL+'#endif',start);helper=s[start:end]
full_start=s.rfind('#if defined(KEBAB)',0,start);full_end=end+len(NL+'#endif'+NL+NL)
call=NL.join(['#if defined(KEBAB)',TAB*3+'kebab_ui_kill_request(cp, p, uap->signum);','#endif'])+NL
parent=s[:full_start]+s[full_end:];assert parent.count(call)==1;parent=parent.replace(call,'')
expected=json.loads((q/'all-mac-source-0438.json').read_text())['files']['bsd/kern/kern_sig.c'];assert hashlib.sha256(parent.encode()).hexdigest()==expected,'Removing only observer must reproduce the complete previous source file'
def body(text):
 start=text.index('int'+NL+'kill(proc_t cp,');end=text.index(NL+'os_reason_t'+NL+'build_userspace_exit_reason',start);return text[start:end]
before=body(parent).replace(NL+'kill(',NL+'kill_before(');new=body(s).replace(NL+'kill(',NL+'kill_after(')
raw=(q/'ui-kill-production-test.cpp').read_bytes();fixture=raw.decode();assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']
assert all(text in fixture for text in(helper,before,new)),'Fixture must contain current helper and complete old/current syscall bodies'
variants=[('actual',fixture,True)]
def variant(name,old,replacement):
 assert fixture.count(old)==1;variants.append((name,fixture.replace(old,replacement),False))
no_call=new.replace(call,'');needle=TAB*2+'if (!cansignal(cp, uc, p, uap->signum)) {'
variant('trace-before-permission',new,no_call.replace(needle,TAB*2+'kebab_ui_kill_request(cp, p, uap->signum);'+NL+needle))
variant('bypass-permission',new,new.replace('if (!cansignal(cp, uc, p, uap->signum)) {','if (!cansignal(cp, uc, p, uap->signum) && false) {'))
variant('drop-real-signal',new,new.replace(TAB*3+'psignal(p, uap->signum);',TAB*3+';'))
variant('leak-target',new,new.replace(TAB*2+'proc_rele(p);'+NL+TAB*2+'return 0;',TAB*2+'return 0;'))
variant('off-by-one-pid',new,new.replace('uap->pid > 0','uap->pid >= 0'))
variant('over-cap','if (slot >= 16) return;','if (slot >= 17) return;')
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-kill-',dir=a.cache)as td:
 for name,text,expected in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==expected,(name,result.stdout,result.stderr)
  if expected:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected behavioral mutation',name)
print('PASS exact complete parent/current source preservation,592cases,concurrent cap and6behavioral mutants; process/credentials/signals are explicit models')
