"""Check the complete parent/current source and bounded current-sender trace model."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;receipt=json.loads((q/'test-ui-kill-caller-result-0440.json').read_text());assert receipt['passed']
raw=(a.source/'bsd/kern/kern_sig.c').read_bytes();s=raw.decode();NL=chr(10);TAB=chr(9)
assert hashlib.sha256(raw).hexdigest()==receipt['source_sha256']==json.loads((q/'all-mac-source-0440.json').read_text())['files']['bsd/kern/kern_sig.c']
include=NL.join(['#if defined(KEBAB)','#include <kern/backtrace.h>','#endif',''])
assert s.count(include)==1
start=s.index('    /* This is the current sender');end=s.index(NL+'}'+NL+'#endif',start)
parent=(s[:start]+s[end+1:]).replace(include,'')
expected=json.loads((q/'all-mac-source-0439.json').read_text())['files']['bsd/kern/kern_sig.c']
assert hashlib.sha256(parent.encode()).hexdigest()==expected==receipt['parent_sha256'],'Removing only the new trace must restore the entire previous source file'
start=s.index('static void'+NL+'kebab_ui_kill_request(');end=s.index(NL+'#endif',start);helper=s[start:end]
start=s.index('int'+NL+'kill(proc_t cp,');end=s.index(NL+'os_reason_t'+NL+'build_userspace_exit_reason',start);body=s[start:end]
call=NL.join(['#if defined(KEBAB)',TAB*3+'kebab_ui_kill_request(cp, p, uap->signum);','#endif',''])
assert body.count(call)==1
original=body.replace(call,'').replace(NL+'kill(',NL+'kill_before(')
current=body.replace(NL+'kill(',NL+'kill_after(')
raw=(q/'ui-kill-caller-production-test.cpp').read_bytes();fixture=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']
assert all(text in fixture for text in(helper,original,current)),'Fixture must compile complete current helper and original/current kill bodies'
variants=[('actual',fixture,True)]
def variant(name,old,new):
 assert fixture.count(old)==1;variants.append((name,fixture.replace(old,new),False))
variant('discard-partial-trace','    for (unsigned i = 0; i < count; i++) {','    if (info.btui_error) return;'+NL+'    for (unsigned i = 0; i < count; i++) {')
variant('hide-trace-error','count, info.btui_error);','count, 0);')
variant('unbounded-frame-count','backtrace_user(frames, 12, NULL, &info)','backtrace_user(frames, 13, NULL, &info)')
variant('read-past-returned-frames','i < count; i++','i <= count; i++')
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-kill-caller-',dir=a.cache)as td:
 for name,text,ok in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==ok,(name,result.stdout,result.stderr)
  if ok:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected',name)
print('PASS complete parent/current source, bounded12frames and trace errors preserve real syscall control flow; backtrace/proc/credentials/signals are explicit host models')
