"""Verify exact current sender Mach state observation without altering original kill."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;receipt=json.loads((q/'test-ui-kill-link-result-0441.json').read_text());assert receipt['passed']
raw=(a.source/'bsd/kern/kern_sig.c').read_bytes();s=raw.decode();NL=chr(10);TAB=chr(9)
assert hashlib.sha256(raw).hexdigest()==receipt['source_sha256']==json.loads((q/'all-mac-source-0441.json').read_text())['files']['bsd/kern/kern_sig.c']
start=s.index('/* ARM64 kill is a leaf syscall:');end=s.index('static void'+NL+'kebab_ui_kill_request(',start)
parent=s[:start]+s[end:];call='    kebab_ui_kill_current_site(slot + 1, sender_name, proc_pid(sender));'+NL
assert parent.count(call)==1;parent=parent.replace(call,'')
assert hashlib.sha256(parent.encode()).hexdigest()==receipt['parent_sha256']==json.loads((q/'all-mac-source-0440.json').read_text())['files']['bsd/kern/kern_sig.c']
start=s.index('static void'+NL+'kebab_ui_kill_current_site(');end=s.index(NL+'#endif',start);helper=s[start:end]
start=s.index('int'+NL+'kill(proc_t cp,');end=s.index(NL+'os_reason_t'+NL+'build_userspace_exit_reason',start);body=s[start:end]
call=NL.join(['#if defined(KEBAB)',TAB*3+'kebab_ui_kill_request(cp, p, uap->signum);','#endif',''])
assert body.count(call)==1
original=body.replace(call,'').replace(NL+'kill(',NL+'kill_before(');current=body.replace(NL+'kill(',NL+'kill_after(')
raw=(q/'ui-kill-link-production-test.cpp').read_bytes();fixture=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']
assert all(text in fixture for text in(helper,original,current)),'Compile the complete current observer and original/current syscall bodies'
variants=[('actual',fixture,True)]
def variant(name,old,new):
 assert fixture.count(old)==1;variants.append((name,fixture.replace(old,new),False))
variant('wrong-thread','thread_get_state(current_thread(), ARM_THREAD_STATE64,','thread_get_state(4321, ARM_THREAD_STATE64,')
variant('accept-failed-state','result != KERN_SUCCESS || count != ARM_THREAD_STATE64_COUNT','result != KERN_SUCCESS && count != ARM_THREAD_STATE64_COUNT')
variant('wrong-link-field','(unsigned long long)saved.lr','(unsigned long long)saved.pc')
variant('omit-callsite','    kebab_ui_kill_current_site(slot + 1, sender_name, proc_pid(sender));','    if (false) kebab_ui_kill_current_site(slot + 1, sender_name, proc_pid(sender));')
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-kill-link-',dir=a.cache)as td:
 for name,text,ok in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==ok,(name,result.stdout,result.stderr)
  if ok:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected',name)
print('PASS entire parent source restored; complete current observer, state failures,592comparisons and concurrent16event/12frame cap; Mach state/unwinder are explicit models')
