/* Normal USB boot observability. Snapshot a bounded set of referenced objects;
 * never print while holding a proc/task/thread lock. No thread suspension,
 * saved-state modification, filesystem operation, or user memory access.
 * The USB panic dispatcher rejects these commands before calling this file. */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/proc_internal.h>
#include <sys/systm.h>
#include <kern/task.h>

extern void kebab_pid1_task_report(task_t task);
void kebab_process_report(void);
void kebab_pid1_report(void);

static int
kebab_process_line(proc_t process, void *arg)
{
	unsigned *count = arg;
	char name[MAXCOMLEN + 1] = {};
	int pid = proc_pid(process), parent = proc_ppid(process);
	proc_name(pid, name, sizeof(name));
	proc_lock(process);
	unsigned state = process->p_stat, flags = process->p_flag;
	proc_unlock(process);
	printf("KEBAB PROC: pid=%d parent=%d state=%u flags=0x%x name=%s\n",
	    pid, parent, state, flags, name);
	return ++*count >= 64 ? PROC_RETURNED_DONE : PROC_RETURNED;
}

void
kebab_process_report(void)
{
	unsigned count = 0;
	printf("KEBAB PROC: active process snapshot begin (cap64)\n");
	proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_process_line, &count, NULL, NULL);
	printf("KEBAB PROC: snapshot end count=%u%s\n", count, count == 64 ? "+" : "");
}

void
kebab_pid1_report(void)
{
	proc_t process = proc_find(1);
	if (process == PROC_NULL) {
		printf("KEBAB PID1: process absent\n");
		return;
	}
	task_t task = proc_task(process);
	if (task == TASK_NULL) {
		proc_rele(process);
		printf("KEBAB PID1: task absent\n");
		return;
	}
	task_reference(task);
	proc_rele(process);
	kebab_pid1_task_report(task);
	task_deallocate(task);
}
#endif /* KEBAB */
