"""Compile exact current fixed UI/legacy observers and complete USB dispatcher."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;manifest=json.loads((q/'all-mac-source-0445.json').read_text())['files']
names=['bsd/kern/kebab_bootdiag.c','osfmk/kern/kebab_bootdiag.c','bsd/dev/kebab_usb.c'];sources={name:(a.source/name).read_text()for name in names}
for name,text in sources.items():assert hashlib.sha256(text.encode()).hexdigest()==manifest[name]
receipt=json.loads((q/'test-fixed-ui-depth-result-0445.json').read_text());assert receipt['passed']
for name,expected in receipt['actual_production_sources'].items():assert manifest[name]==expected
strip=lambda text:re.sub(r'^#include [^\n]+\n','',text,flags=re.M)
production=strip(sources[names[1]])+'\n'+strip(sources[names[0]])
for old,new in [('unsigned *count = arg;','unsigned *count = (unsigned *)arg;'),('struct kebab_backboard_snapshot *snapshot = argument;','struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;'),('struct kebab_ui_snapshot *snapshot = argument;','struct kebab_ui_snapshot *snapshot = (struct kebab_ui_snapshot *)argument;')]:
 assert production.count(old)==1;production=production.replace(old,new)
raw=(q/'fixed-ui-depth-production-test.cpp').read_bytes();fixture=raw.decode()
assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']and production in fixture
variants=[('actual-observers',fixture,True,'cpp')]
def variant(name,old,new):
 assert fixture.count(old)==1;variants.append((name,fixture.replace(old,new),False,'cpp'))
start=fixture.index('void\nkebab_ui_report(void)\n{');end=fixture.index('\n}\n',start)+3;body=fixture[start:end]
start=fixture.index('static int\nkebab_ui_select(');end=fixture.index('\n}\n',start)+3;select=fixture[start:end]
variant('wrong-name',select,select.replace('!strcmp(name, snapshot->name)','true'))
variant('wrong-parent',select,select.replace('proc_ppid(process) == 1','true'))
variant('leak-ui-task',body,body.replace('task_deallocate(snapshot.task);',';'))
variant('unsafe-context',body,body.replace('if (ml_at_interrupt_context() || get_preemption_level() != 0 ||\n        !ml_get_interrupts_enabled()) return;','if(false)return;'))
variant('wrong-preboard-label','void kebab_preboard_task_report(task_t task) { kebab_task_threads_report(task, "UI-PB", true, 16); }','void kebab_preboard_task_report(task_t task) { kebab_task_threads_report(task, "UI-SB", true, 16); }')
variant('wrong-user-label','kebab_user_wait_report(map, label, tid, &site, user_frame_cap);','kebab_user_wait_report(map, "BBOARD", tid, &site, user_frame_cap);')
variant('no-global-bound','if (frame_cap > 16) frame_cap = 16;',';')
variant('short-ui-budget','kebab_task_threads_report(task, "UI-SB", true, 16);','kebab_task_threads_report(task, "UI-SB", true, 8);')
variant('changed-backboard-budget','kebab_task_threads_report(task, "BBOARD", true, 8);','kebab_task_threads_report(task, "BBOARD", true, 16);')
variant('ignore-window','fp - origin > 65536','false')
variant('ignore-alignment','(fp & 15) != 0','false')
variant('leak-map','if (map != VM_MAP_NULL) vm_map_deallocate(map);',';')
usb=json.loads((q/'test-fixed-ui-usb-result-0444.json').read_text());raw=(q/'fixed-ui-usb-production-test.c').read_bytes();cfixture=raw.decode()
assert usb['passed']and usb['source_sha256']==manifest[names[2]]and hashlib.sha256(raw).hexdigest()==usb['fixture_sha256']
def function(text,name):
 at=text.index('\n'+name+'(');start=text.rfind('\nstatic ',0,at)+1;end=text.index('\n}\n',at)+3;return text[start:end]
assert all(function(sources[names[2]],name)in cfixture for name in ['ud_command_seen','ud_reboot_command','ud_run_reboot_command'])
variants.append(('actual-usb',cfixture,True,'c'))
old='return UD_REBOOT_UI_REPORT;';assert cfixture.count(old)==2;variants.append(('wrong-usb-command',cfixture.replace(old,'return UD_REBOOT_BBOARD_REPORT;'),False,'c'))
old='if (panicking) {';assert cfixture.count(old)==1;variants.append(('panic-normal-path',cfixture.replace(old,'if (panicking && false) {'),False,'c'))
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fixed-ui-waits-',dir=a.cache)as td:
 for name,text,ok,kind in variants:
  p=Path(td)/(name+'.'+kind);p.write_text(text);exe=p.with_suffix('')
  command=['clang++','-std=c++17']if kind=='cpp'else['clang','-std=c11']
  subprocess.run([*command,'-O1','-g','-Wall','-Wextra','-Werror',*(['-Wno-unused-function']if kind=='cpp'else[]),'-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==ok,(name,result.stdout,result.stderr)
  if ok:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected',name)
print('PASS exact3productionfiles, full observer+USB bodies,14behavioralmutants; task/scheduler/map/nofault/action functions are explicit models')
