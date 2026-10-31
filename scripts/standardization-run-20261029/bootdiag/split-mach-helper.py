from pathlib import Path
r=Path(__file__).parent
p=r/'after/bsd/kern/kebab_bootdiag.c';s=p.read_text()
at=s.index('\tthread_t threads[32], thread;');end=s.index('\ttask_deallocate(task);',at)
body=s[at:end].replace('total','thread_total')
s=s[:at]+'\tkebab_pid1_task_report(task);\n'+s[end:]
s=s.replace('\tprintf("KEBAB PID1: snapshot end; addresses name kernel waits, not user PCs\\n");\n','')
s=s.replace('void kebab_process_report(void);','extern void kebab_pid1_task_report(task_t task);\nvoid kebab_process_report(void);')
for header in ['#include <kern/thread.h>\n','#include <kern/queue.h>\n','#include <machine/machine_routines.h>\n','#include <mach/thread_info.h>\n']:
    s=s.replace(header,'')
p.write_text(s,newline='\n')
helper='''/* Mach-private half of normal PID1 observability. The caller holds a task
 * reference. Copy at most 32 thread references under task_lock, then observe
 * each under sched_lock; release everything before printing or returning. */
#if defined(KEBAB)
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/queue.h>
#include <kern/sched_prim.h>
#include <kern/spl.h>
#include <mach/thread_info.h>
#include <libkern/libkern.h>
void kebab_pid1_task_report(task_t task);
void
kebab_pid1_task_report(task_t task)
{
'''+body+'''\tprintf("KEBAB PID1: snapshot end; addresses name kernel waits, not user PCs\\n");
}
#endif /* KEBAB */
'''
(r/'after/osfmk/kern/kebab_bootdiag.c').write_text(helper,newline='\n')
p=Path('osfmk/conf/files');s=(r/'before'/p).read_text()
line=next(x for x in s.splitlines() if x.startswith('osfmk/kern/task.c'))
(r/'after'/p).write_text(s+'\n'+line.replace('osfmk/kern/task.c','osfmk/kern/kebab_bootdiag.c')+'\n',newline='\n')
