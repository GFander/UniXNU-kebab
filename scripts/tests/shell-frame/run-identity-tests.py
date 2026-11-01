from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
r=Path(__file__).resolve().parent;ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path);ap.add_argument('--work',type=Path);a=ap.parse_args();w=a.work or r/'shell-frame-0485';root=a.source or w/'candidate';w.mkdir(parents=True,exist_ok=True);source=root/'bsd/kern/kebab_bootdiag.c';s=source.read_text()
start=s.index('struct kebab_ui_snapshot {');end=s.index('\nvoid\nkebab_ui_report(',start);core=s[start:end];start=end+1;end=s.index('\n}\n',start)+3;report=s[start:end]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
struct Task{unsigned refs;};typedef struct Task* task_t;
#define TASK_NULL NULL
#define MAXCOMLEN 16
#define PROC_RETURNED_DONE 1
#define PROC_RETURNED 0
#define PROC_ALLPROCLIST 1
#define PROC_NOWAITTRANS 2
struct Process{const char *name;int parent;unsigned uid;uint8_t uuid[16];task_t task;};typedef struct Process* proc_t;
static struct Process process;static struct Task task;static bool proc_ref;static unsigned acks,reports;
static bool ml_at_interrupt_context(){return false;}static int get_preemption_level(){return 0;}static bool ml_get_interrupts_enabled(){return true;}
static int proc_pid(proc_t p){assert(p==&process&&proc_ref);return 123;}static int proc_ppid(proc_t p){assert(proc_ref);return p->parent;}static unsigned proc_getuid(proc_t p){assert(proc_ref);return p->uid;}
static void proc_name(int p,char *out,size_t n){assert(proc_ref&&p==123);snprintf(out,n,"%s",process.name);}
static void proc_getexecutableuuid(proc_t p,uint8_t *out,size_t n){assert(proc_ref&&p==&process&&n==16);memcpy(out,p->uuid,16);}
static task_t proc_task(proc_t p){assert(proc_ref);return p->task;}static void task_reference(task_t t){assert(proc_ref&&t==&task);t->refs++;}static void task_deallocate(task_t t){assert(!proc_ref&&t->refs);t->refs--;}
static void proc_iterate(int flags,int(*cb)(proc_t,void*),void *arg,void*,void*){assert(flags==3);proc_ref=true;cb(&process,arg);proc_ref=false;/* proc identity is no longer safe to read, while the task remains referenced. */}
static void kebab_springboard_task_report(task_t t){assert(!proc_ref&&t->refs);reports++;}static void kebab_preboard_task_report(task_t t){assert(!proc_ref&&t->refs);}
static void kebab_shell_ack_task_report(task_t t){assert(!proc_ref&&t->refs);acks++;}
static void *get_bsdtask_info(task_t unused){(void)unused;assert(false&&"No proc dereference after callback");return NULL;}
static void reset(){assert(!proc_ref&&!task.refs);process.name="SpringBoard";process.parent=1;process.uid=501;process.task=&task;uint8_t uuid[]={0xe1,0xa4,0xfe,0x7d,0x29,0xff,0x32,0x69,0xb3,0xab,0x50,0xae,0xd4,0x0c,0x44,0x95};memcpy(process.uuid,uuid,16);acks=reports=0;}
'''
tests=r'''
int main(){reset();kebab_ui_report();assert(acks==1&&reports==1&&!task.refs);
 reset();process.uid=0;kebab_ui_report();assert(!acks&&reports==1&&!task.refs);
 reset();process.uuid[0]^=1;kebab_ui_report();assert(!acks&&reports==1&&!task.refs);
 reset();process.parent=2;kebab_ui_report();assert(!acks&&!reports&&!task.refs);
 reset();process.task=NULL;kebab_ui_report();assert(!acks&&!reports&&!task.refs);
 reset();process.name="SpringBoar";kebab_ui_report();assert(!acks&&!reports&&!task.refs);
 fprintf(stdout,"PASS6 actual production selector/report identity and exit-window model cases; no live proc access after callback\n");}
'''
pieces={'core':core,'report':report};mutants=[('omit-identity-copy','core','proc_getexecutableuuid(process, snapshot->uuid, sizeof(snapshot->uuid));',''),('bypass-uid','report','snapshot.uid == 501','true'),('bypass-uuid','report','!memcmp(snapshot.uuid, original_uuid, sizeof(snapshot.uuid))','true'),('ignore-parent','core','proc_ppid(process) == 1','true'),('late-proc-read','report','if (snapshot.uid == 501','proc_getexecutableuuid((proc_t)get_bsdtask_info(snapshot.task), snapshot.uuid, sizeof(snapshot.uuid)); if (snapshot.uid == 501')]
env=dict(os.environ);env['TMPDIR']=env.get('TMPDIR') or tempfile.gettempdir();runs=[]
with tempfile.TemporaryDirectory(dir=env['TMPDIR']) as temp:
 d=Path(temp)
 for name,part,old,new in [('baseline',None,None,None)]+mutants:
  units=dict(pieces)
  if part:assert units[part].count(old)==1,(name,units[part].count(old));units[part]=units[part].replace(old,new)
  p=d/(name+'.c');p.write_text(prefix+units['core']+'\n'+units['report']+'\n'+tests);exe=d/name
  built=subprocess.run(['clang','-std=c11','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True,env=env);assert built.returncode==0,(name,'compile failure not behavioral rejection',built.stderr[-2500:])
  run=subprocess.run([str(exe)],capture_output=True,text=True,env=env);assert (run.returncode==0)==(name=='baseline'),(name,run.stdout,run.stderr[-2000:]);runs.append(dict(name=name,compiled=True,exit_code=run.returncode))
  if name=='baseline':(w/'identity-production-test.c').write_text(p.read_text(),newline='\n');print(run.stdout.strip())
q=dict(passed=True,production_bsd_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256((w/'identity-production-test.c').read_bytes()).hexdigest(),actual_production_selector_report=True,proc_task_and_exit_are_models=True,compiled_behavioral_mutants=len(mutants),runs=runs,native_or_physical=False)
(w/'test-shell-identity-result-0485.json').write_text(json.dumps(q,indent=2)+'\n',newline='\n');print('PASS5 compiled identity/lifetime behavioral mutants')
