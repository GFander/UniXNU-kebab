
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#define KEBAB 1
#define uint64_t unsigned long long
#define MAXCOMLEN 16
static unsigned procDepth,taskDepth,threadDepth,splDepth,procRefs,taskRefs=1,threadReads,threadDrops,listCalls,copies;
static unsigned interruptContext,preemptionLevel;static bool nullTask,failInfo,failCopy;
static std::string output;
static int checked_printf(const char *format,...){assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth);char line[2048];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);output+=line;return n;}
#define printf checked_printf
using boolean_t=int;static boolean_t ml_at_interrupt_context(){return interruptContext;}static int get_preemption_level(){return preemptionLevel;}
using vm_offset_t=uintptr_t;
enum{TH_WAIT=1,TH_RUN=4};
struct arm_kernel_saved_state_t{uintptr_t lr,fp;};
struct thread_kernel_state{struct{arm_kernel_saved_state_t ss;}machine;};
static constexpr uintptr_t kernel_stack_size=1024;

struct arm_saved_state_t{struct{unsigned flavor=64,count=84;}ash;uint64_t pc=0x1234,lr=0x2345,fp=0;};
static constexpr unsigned ARM_SAVED_STATE64_COUNT=84;
static bool is_saved_state64(const arm_saved_state_t *p){return p->ash.flavor==64;}
static uint64_t get_saved_state_pc(const arm_saved_state_t *p){return p->pc;}
static uint64_t get_saved_state_lr(const arm_saved_state_t *p){return p->lr;}
static uint64_t get_saved_state_fp(const arm_saved_state_t *p){return p->fp;}
struct thread{arm_saved_state_t user;bool hasUser=true;int refs=1;uint64_t wait_event=7;void(*continuation)()=nullptr;unsigned state=TH_WAIT;uintptr_t kernel_stack=0;alignas(16)std::array<uint8_t,kernel_stack_size> wired{};};using thread_t=thread *;
struct task{bool active=true;unsigned thread_count=0;std::vector<thread_t>threads;};using task_t=task *;
struct proc{unsigned p_stat=1,p_flag=0,uid=0;int pid=1,parent=1;std::string name;};using proc_t=proc *;
static task activeTask;static std::vector<proc> processes;
#define TASK_NULL nullptr
#define PROC_NULL nullptr
enum{PROC_RETURNED,PROC_RETURNED_DONE,PROC_ALLPROCLIST=1,PROC_NOWAITTRANS=2};
static proc_t proc_find(int pid){for(auto &p:processes)if(p.pid==pid){procRefs++;return &p;}return nullptr;}
static void proc_rele(proc_t p){assert(p&&procRefs);procRefs--;}
static task_t proc_task(proc_t p){assert(p&&procRefs);return nullTask?nullptr:&activeTask;}
static void task_reference(task_t t){assert(t==&activeTask);taskRefs++;}
static void task_deallocate(task_t t){assert(t==&activeTask&&taskRefs>1);taskRefs--;}
static int proc_pid(proc_t p){return p->pid;}static int proc_ppid(proc_t p){return p->parent;}static unsigned proc_getuid(proc_t p){return p->uid;}
static void proc_name(int pid,char *name,unsigned n){for(auto &p:processes)if(p.pid==pid){snprintf(name,n,"%s",p.name.c_str());return;}assert(false);}
static void proc_lock(proc_t){assert(!procDepth++);}static void proc_unlock(proc_t){assert(procDepth--==1);}
static void proc_iterate(unsigned flags,int(*callback)(proc_t,void *),void *arg,void *,void *){
 assert(flags==(PROC_ALLPROCLIST|PROC_NOWAITTRANS));for(auto &p:processes){listCalls++;procRefs++;int result=callback(&p,arg);procRefs--;assert(result==PROC_RETURNED||result==PROC_RETURNED_DONE);if(result==PROC_RETURNED_DONE)break;}
}
static void task_lock(task_t){assert(!taskDepth++);}static void task_unlock(task_t){assert(taskDepth--==1);}
static void thread_reference(thread_t t){assert(taskDepth&&t->refs==1);t->refs++;}
static void thread_deallocate(thread_t t){assert(!taskDepth&&t->refs==2);t->refs--;threadDrops++;}
#define queue_iterate(head,var,type,field) for(unsigned qi=0;qi<(head)->size()&&((var)=(*(head))[qi],true);qi++)
using spl_t=int;static spl_t splsched(){assert(!splDepth++);return 1;}static void splx(spl_t s){assert(s==1&&splDepth--==1);}
static void thread_lock(thread_t){assert(splDepth&&!threadDepth++);}static void thread_unlock(thread_t){assert(threadDepth--==1);}
static uint64_t thread_tid(thread_t){return 101;}
using kern_return_t=int;using mach_msg_type_number_t=unsigned;
struct test_timeval{int seconds,microseconds;};
struct thread_basic_info_data_t{int run_state=0,flags=0,suspend_count=0,sleep_time=0;test_timeval user_time{},system_time{};};using thread_info_t=thread_basic_info_data_t *;
enum{THREAD_BASIC_INFO=1,THREAD_BASIC_INFO_COUNT=10};
static kern_return_t thread_info_internal(thread_t t,int flavor,thread_info_t info,mach_msg_type_number_t *n){assert(t->refs==2&&!taskDepth&&flavor==1&&*n==10);threadReads++;if(failInfo)return 5;info->run_state=3;return 0;}
static thread_kernel_state *thread_get_kernel_state(thread_t t){assert(threadDepth&&splDepth&&t->refs==2);return(thread_kernel_state *)t->kernel_stack;}
static uintptr_t ml_nofault_copy(uintptr_t from,vm_offset_t to,uintptr_t n){
 assert(threadDepth&&splDepth);copies++;if(failCopy)return 0;
 bool valid=false;for(auto t:activeTask.threads)if(from>=t->kernel_stack&&from<=t->kernel_stack+kernel_stack_size&&n<=t->kernel_stack+kernel_stack_size-from)valid=true;
 for(auto t:activeTask.threads)if(from==(uintptr_t)&t->user&&n==sizeof(t->user))valid=true;
 assert(valid);memcpy((void *)to,(const void *)from,n);return n;
}

static bool interruptsEnabled=true;
static boolean_t ml_get_interrupts_enabled(){return interruptsEnabled;}
static arm_saved_state_t *get_user_regs(thread_t t){assert(threadDepth&&splDepth&&t->refs==2);return t->hasUser?&t->user:nullptr;}
struct model_map{uintptr_t low=0x1000,high=0x2000;bool kernel=false;};using vm_map_t=model_map *;
static model_map userMap;static unsigned mapRefs=0,mapReads=0;static bool failMap=false,failMapRead=false;
static std::array<std::array<uint64_t,2>,20> modelRows{};
#define VM_MAP_NULL nullptr
static constexpr int KERN_SUCCESS=0;
static vm_map_t get_task_map_reference(task_t t){assert(!taskDepth&&!threadDepth&&!splDepth);if(!t->active||failMap)return nullptr;mapRefs++;return &userMap;}
static void vm_map_deallocate(vm_map_t m){assert(m==&userMap&&mapRefs==1&&!taskDepth&&!threadDepth&&!splDepth);mapRefs--;}
static void *pmap_kernel(){return(void *)1;}
static void *vm_map_pmap(vm_map_t m){return m->kernel?pmap_kernel():(void *)2;}
static uintptr_t vm_map_min(vm_map_t m){return m->low;}
static uintptr_t vm_map_max(vm_map_t m){return m->high;}
static kern_return_t copyinmap(vm_map_t m,uintptr_t from,void *to,uintptr_t n){
 assert(!taskDepth&&!threadDepth&&!splDepth&&!procDepth&&m==&userMap&&!m->kernel&&mapRefs==1&&interruptsEnabled);
 assert(n==16&&from>=m->low&&from<m->high&&m->high-from>=n&&!(from&15));mapReads++;
 if(failMapRead)return 1;unsigned i=(from-0x1000)/16;assert(i<modelRows.size());memcpy(to,modelRows[i].data(),n);return 0;
}

using proc_name_t=char[2*MAXCOMLEN+1];
static const char *proc_best_name(proc_t p){assert(procDepth==1&&procRefs);return p->name.c_str();}
/* Mach-private half of normal PID1 and fixed daemon/UI observability. The caller holds a task
 * reference. Copy at most 32 thread references under task_lock, then observe
 * each under sched_lock; release everything before printing or returning. */
#if defined(KEBAB)

/* Caller holds sched_lock at splsched. Only a waiting, non-runnable thread
 * with a retained full kernel stack is inspected. A continuation-backed or
 * running thread is skipped; no cross-CPU freezing/suspension is attempted.
 * Wired stack memory is copied through the platform's no-fault primitive. */
static unsigned
kebab_blocked_kernel_trace(thread_t thread, uint64_t frames[16])
{
	uintptr_t low = thread->kernel_stack, high = low + kernel_stack_size;
	if ((thread->state & (TH_WAIT | TH_RUN)) != TH_WAIT || thread->continuation != NULL ||
	    low == 0 || high < low || kernel_stack_size < sizeof(struct thread_kernel_state)) { return 0; }
	arm_kernel_saved_state_t saved;
	uintptr_t source = (uintptr_t)&thread_get_kernel_state(thread)->machine.ss;
	if (ml_nofault_copy(source, (vm_offset_t)&saved, sizeof(saved)) != sizeof(saved)) { return 0; }
	unsigned count = 0;
	if (saved.lr != 0) { frames[count++] = saved.lr; }
	uintptr_t fp = saved.fp;
	while (count < 16 && fp >= low && fp <= high - 2 * sizeof(uint64_t) && (fp & 7) == 0) {
		uint64_t pair[2];
		if (ml_nofault_copy(fp, (vm_offset_t)pair, sizeof(pair)) != sizeof(pair)) { break; }
		if (pair[1] == 0) { break; }
		frames[count++] = pair[1];
		if (pair[0] <= fp) { break; }
		fp = pair[0];
	}
	return count;
}

/* Read only a referenced, waiting/non-runnable thread's kernel-owned save area
 * under sched_lock. Never run backtrace_user on an unsuspended foreign thread.
 * No user address is dereferenced while holding task/thread/scheduler locks. */
struct kebab_saved_user_site { uint64_t pc, lr, fp; };
static bool
kebab_waiting_user_site(thread_t thread, struct kebab_saved_user_site *site)
{
    if ((thread->state & (TH_WAIT | TH_RUN)) != TH_WAIT) return false;
    const arm_saved_state_t *source = get_user_regs(thread);
    arm_saved_state_t saved;
    if (source == NULL || ml_nofault_copy((vm_offset_t)source, (vm_offset_t)&saved,
        sizeof(saved)) != sizeof(saved) || !is_saved_state64(&saved) ||
        saved.ash.count != ARM_SAVED_STATE64_COUNT) return false;
    site->pc = get_saved_state_pc(&saved);
    site->lr = get_saved_state_lr(&saved);
    site->fp = get_saved_state_fp(&saved);
    return true;
}

/* The target can resume after the above snapshot. These are explicitly
 * best-effort call sites, not a consistent suspended-thread backtrace. The
 * retained user map survives exit; copyinmap reports invalid/unmapped pages.
 * Bound every read to that map, aligned increasing frames in a64KiB window,
 * at most16pairs (the fixed backboardd wrapper keeps its8pair budget). Never use copyinmap's kernel-pmap memcpy fast path. */
static void
kebab_user_wait_report(vm_map_t map, const char *label, uint64_t tid, const struct kebab_saved_user_site *site, unsigned frame_cap)
{
    if (map == VM_MAP_NULL || vm_map_pmap(map) == pmap_kernel()) return;
    printf("KEBAB %s USER: tid=%llu saved pc=0x%llx lr=0x%llx fp=0x%llx; best-effort, target may resume\n",
        label, tid, site->pc, site->lr, site->fp);
    if (frame_cap > 16) frame_cap = 16;
    uint64_t fp = site->fp, origin = fp;
    for (unsigned i = 0; i < frame_cap; i++) {
        uint64_t pair[2];
        if ((fp & 15) != 0 || fp < vm_map_min(map) || fp >= vm_map_max(map) ||
            vm_map_max(map) - fp < sizeof(pair) || fp < origin || fp - origin > 65536) break;
        kern_return_t result = copyinmap(map, fp, pair, sizeof(pair));
        if (result != KERN_SUCCESS) {
            printf("KEBAB %s USER: tid=%llu read stopped error=0x%x depth=%u\n", label, tid, result, i);
            break;
        }
        if (pair[1] == 0) break;
        printf("KEBAB %s USER: tid=%llu frame[%u]=0x%llx\n", label, tid, i, pair[1]);
        if (pair[0] <= fp) break;
        fp = pair[0];
    }
}

static void
kebab_task_threads_report(task_t task, const char *label, bool user_sites, unsigned user_frame_cap)
{
	thread_t observed[32], thread;
	unsigned count = 0, thread_total = 0;
	vm_map_t map = user_sites ? get_task_map_reference(task) : VM_MAP_NULL;
	task_lock(task);
	if (task->active) {
		thread_total = task->thread_count;
		queue_iterate(&task->threads, thread, thread_t, task_threads) {
			if (count == sizeof(observed) / sizeof(observed[0])) { break; }
			thread_reference(thread);
			observed[count++] = thread;
		}
	}
	task_unlock(task);
	printf("KEBAB %s: thread snapshot count=%u thread_total=%u (cap32; no suspension)\n", label, count, thread_total);
	for (unsigned i = 0; i < count; i++) {
		thread = observed[i];
		thread_basic_info_data_t basic = {};
		mach_msg_type_number_t size = THREAD_BASIC_INFO_COUNT;
		kern_return_t result = thread_info_internal(thread, THREAD_BASIC_INFO,
		    (thread_info_t)&basic, &size);
		spl_t priority = splsched();
		thread_lock(thread);
		uint64_t tid = thread_tid(thread);
		uint64_t event = thread->wait_event;
		uint64_t continuation = (uint64_t)(uintptr_t)thread->continuation;
		unsigned state = thread->state;
		uint64_t frames[16];
		unsigned frame_count = kebab_blocked_kernel_trace(thread, frames);
		struct kebab_saved_user_site site = {};
		bool have_site = user_sites && kebab_waiting_user_site(thread, &site);
		thread_unlock(thread);
		splx(priority);
		printf("KEBAB %s: tid=%llu info=0x%x state=0x%x run=%d flags=0x%x suspend=%d sleep=%d wait=0x%llx continuation=0x%llx user=%d.%06d sys=%d.%06d\n", label,
		    tid, result, state, basic.run_state, basic.flags, basic.suspend_count, basic.sleep_time,
		    event, continuation, basic.user_time.seconds, basic.user_time.microseconds,
		    basic.system_time.seconds, basic.system_time.microseconds);
		for (unsigned frame = 0; frame < frame_count; frame++) {
			printf("KEBAB %s: tid=%llu kframe[%u]=0x%llx\n", label, tid, frame, frames[frame]);
		}
		if (have_site) kebab_user_wait_report(map, label, tid, &site, user_frame_cap);
		thread_deallocate(thread);
	}
	if (map != VM_MAP_NULL) vm_map_deallocate(map);
	printf("KEBAB %s: snapshot end; addresses name kernel waits, not user PCs\n", label);
}

void kebab_pid1_task_report(task_t task);
void kebab_backboardd_task_report(task_t task);
void kebab_pid1_task_report(task_t task) { kebab_task_threads_report(task, "PID1", false, 0); }
void kebab_backboardd_task_report(task_t task) { kebab_task_threads_report(task, "BBOARD", true, 8); }
void kebab_springboard_task_report(task_t task);
void kebab_preboard_task_report(task_t task);
void kebab_springboard_task_report(task_t task) { kebab_task_threads_report(task, "UI-SB", true, 16); }
void kebab_preboard_task_report(task_t task) { kebab_task_threads_report(task, "UI-PB", true, 16); }
void kebab_migrator_task_report(task_t task);
void kebab_migrator_task_report(task_t task) { kebab_task_threads_report(task, "MIG", true, 16); }
#endif /* KEBAB */

/* Normal USB boot observability. Snapshot a bounded set of referenced objects;
 * never print while holding a proc/task/thread lock. No thread suspension,
 * saved-state modification, filesystem operation, or user memory access.
 * The USB panic dispatcher rejects these commands before calling this file. */
#if defined(KEBAB)

extern void kebab_pid1_task_report(task_t task);
void kebab_process_report(void);
void kebab_pid1_report(void);

static int
kebab_process_line(proc_t process, void *arg)
{
	unsigned *count = (unsigned *)arg;
	char name[MAXCOMLEN + 1] = {};
	int pid = proc_pid(process), parent = proc_ppid(process);
	proc_name(pid, name, sizeof(name));
	proc_lock(process);
	unsigned state = process->p_stat, flags = process->p_flag;
	proc_unlock(process);
	printf("KEBAB PROC: pid=%d parent=%d state=%u flags=0x%x name=%s\n",
	    pid, parent, state, flags, name);
	return ++*count >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;
}

void
kebab_process_report(void)
{
	unsigned count = 0;
	printf("KEBAB PROC: active process snapshot begin (cap512)\n");
	proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_process_line, &count, NULL, NULL);
	printf("KEBAB PROC: snapshot end count=%u%s\n", count, count == 512 ? "+" : "");
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

/* Fixed launchd child selector, not an arbitrary task/user-memory debugger.
 * A system daemon need not run as root. Observe and report its actual UID;
 * this diagnostic never grants credentials or changes the selected task. The
 * iterator retains the proc during the callback; take the task reference
 * before returning and print only after iteration has released its locks. */
extern void kebab_backboardd_task_report(task_t);
extern boolean_t ml_at_interrupt_context(void);
extern boolean_t ml_get_interrupts_enabled(void);
extern int get_preemption_level(void);
void kebab_backboardd_report(void);
struct kebab_backboard_snapshot { task_t task; unsigned scanned; int pid; unsigned uid; bool found; };
static int
kebab_backboard_select(proc_t process, void *argument)
{
    struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;
    char name[MAXCOMLEN + 1] = {};
    ++snapshot->scanned;
    proc_name(proc_pid(process), name, sizeof(name));
    if (!strcmp(name, "backboardd") && proc_ppid(process) == 1) {
        snapshot->found = true;snapshot->pid = proc_pid(process);snapshot->uid = proc_getuid(process);
        task_t task = proc_task(process);
        if (task != TASK_NULL) {task_reference(task);snapshot->task = task;}
        return PROC_RETURNED_DONE;
    }
    return snapshot->scanned >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;
}
void
kebab_backboardd_report(void)
{
    /* No allocation, proc access, or console write in unsafe context. */
    if (ml_at_interrupt_context() || get_preemption_level() != 0 || !ml_get_interrupts_enabled()) return;
    struct kebab_backboard_snapshot snapshot = {};
    proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_backboard_select, &snapshot, NULL, NULL);
    if (snapshot.task == TASK_NULL) {
        printf("KEBAB BBOARD: no referenced task found=%u scanned=%u (parent1/exact-name filter; cap512)\n",
            snapshot.found, snapshot.scanned);
    } else {
        printf("KEBAB BBOARD: fixed daemon pid=%d uid=%u scanned=%u; read-only kernel waits\n",snapshot.pid,snapshot.uid,snapshot.scanned);
        kebab_backboardd_task_report(snapshot.task);
        task_deallocate(snapshot.task);
    }
    printf("KEBAB BBOARD: observer end; no suspension or modification; user frames are best-effort reads\n");
}

/* Only these two fixed launchd children are selected. Report actual UID;
 * names, PIDs and maps are not accepted from the USB command. The iterator
 * holds the process; retain its task there and print outside its locks. */
extern void kebab_springboard_task_report(task_t);
extern void kebab_preboard_task_report(task_t);
void kebab_ui_report(void);
struct kebab_ui_snapshot {
    const char *name; task_t task; unsigned scanned; int pid; unsigned uid; bool found;
};
static int
kebab_ui_select(proc_t process, void *argument)
{
    struct kebab_ui_snapshot *snapshot = (struct kebab_ui_snapshot *)argument;
    char name[MAXCOMLEN + 1] = {};
    ++snapshot->scanned;
    proc_name(proc_pid(process), name, sizeof(name));
    if (!strcmp(name, snapshot->name) && proc_ppid(process) == 1) {
        snapshot->found = true;
        snapshot->pid = proc_pid(process);snapshot->uid = proc_getuid(process);
        task_t task = proc_task(process);
        if (task != TASK_NULL) {task_reference(task);snapshot->task = task;}
        return PROC_RETURNED_DONE;
    }
    return snapshot->scanned >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;
}
void
kebab_ui_report(void)
{
    if (ml_at_interrupt_context() || get_preemption_level() != 0 ||
        !ml_get_interrupts_enabled()) return;
    static const char *const names[] = {"SpringBoard", "PreBoard"};
    static const char *const labels[] = {"UI-SB", "UI-PB"};
    for (unsigned i = 0; i < 2; i++) {
        struct kebab_ui_snapshot snapshot = {};snapshot.name = names[i];
        proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_ui_select, &snapshot, NULL, NULL);
        if (snapshot.task == TASK_NULL) {
            printf("KEBAB %s: no referenced task found=%u scanned=%u (parent1/exact-name; cap512)\n",
                labels[i], snapshot.found, snapshot.scanned);
        } else {
            printf("KEBAB %s: fixed process pid=%d uid=%u scanned=%u; best-effort wait snapshot\n",
                labels[i], snapshot.pid, snapshot.uid, snapshot.scanned);
            if (i == 0) kebab_springboard_task_report(snapshot.task);
            else kebab_preboard_task_report(snapshot.task);
            task_deallocate(snapshot.task);
        }
    }
    printf("KEBAB UI: observer end; no suspension or modification; user frames are best-effort reads\n");
}

/* proc_name() reads the truncated p_comm. Select this one fixed launchd
 * child using its full best name, copied while holding the proc lock.
 * Mutable names are diagnostic identifiers, never authorization evidence. */
extern void kebab_migrator_task_report(task_t);
void kebab_migrator_report(void);
static int
kebab_migrator_select(proc_t process, void *argument)
{
    struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;
    char name[sizeof(proc_name_t)] = {};
    ++snapshot->scanned;
    proc_lock(process);
    strlcpy(name, proc_best_name(process), sizeof(name));
    proc_unlock(process);
    if (!strcmp(name, "com.apple.datamigrator") && proc_ppid(process) == 1) {
        snapshot->found = true;
        snapshot->pid = proc_pid(process);snapshot->uid = proc_getuid(process);
        task_t task = proc_task(process);
        if (task != TASK_NULL) {task_reference(task);snapshot->task = task;}
        return PROC_RETURNED_DONE;
    }
    return snapshot->scanned >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;
}
void
kebab_migrator_report(void)
{
    if (ml_at_interrupt_context() || get_preemption_level() != 0 ||
        !ml_get_interrupts_enabled()) return;
    struct kebab_backboard_snapshot snapshot = {};
    proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_migrator_select, &snapshot, NULL, NULL);
    if (snapshot.task == TASK_NULL) {
        printf("KEBAB MIG: no referenced task found=%u scanned=%u (parent1/full-name; cap512)\n",
            snapshot.found, snapshot.scanned);
    } else {
        printf("KEBAB MIG: fixed process pid=%d uid=%u scanned=%u; best-effort wait snapshot\n",
            snapshot.pid, snapshot.uid, snapshot.scanned);
        kebab_migrator_task_report(snapshot.task);
        task_deallocate(snapshot.task);
    }
    printf("KEBAB MIG: observer end; no suspension or modification; user frames are best-effort reads\n");
}
#endif /* KEBAB */
static void clear(){output.clear();listCalls=threadReads=threadDrops=copies=mapReads=0;assert(!mapRefs);assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&!procRefs&&taskRefs==1);}
static void balanced(){assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&!procRefs&&taskRefs==1);assert(!mapRefs);for(auto t:activeTask.threads)assert(t->refs==1);}
int main(){
 thread store[40];for(auto &t:store){t.kernel_stack=(uintptr_t)t.wired.data();auto state=(thread_kernel_state *)t.kernel_stack;state->machine.ss={0x1234,t.kernel_stack+128};uint64_t pair[2]={t.kernel_stack+144,0x2345};memcpy((void *)(t.kernel_stack+128),pair,16);pair[0]=0;pair[1]=0x3456;memcpy((void *)(t.kernel_stack+144),pair,16);activeTask.threads.push_back(&t);}activeTask.thread_count=40;
 processes={{1,0,0,1,0,"launchd"},{1,0,501,52,1,"backboard"},{1,0,0,53,50,"backboardd"},{1,0,501,57,1,"backboardd"}};
 clear();kebab_backboardd_report();balanced();assert(listCalls==4&&threadReads==32&&threadDrops==32&&output.find("fixed daemon pid=57 uid=501")!=std::string::npos&&output.find("KEBAB BBOARD: tid=")!=std::string::npos&&output.find("kframe[2]=0x3456")!=std::string::npos);
 for(unsigned unsafe=0;unsafe<3;unsafe++){clear();interruptContext=unsafe==0;preemptionLevel=unsafe==1;interruptsEnabled=unsafe!=2;kebab_backboardd_report();balanced();assert(output.empty()&&!listCalls&&!copies);interruptContext=preemptionLevel=0;interruptsEnabled=true;}
 nullTask=true;clear();kebab_backboardd_report();balanced();assert(!threadReads&&output.find("found=1")!=std::string::npos);nullTask=false;
 activeTask.active=false;clear();kebab_backboardd_report();balanced();assert(!threadReads&&output.find("thread snapshot count=0")!=std::string::npos);activeTask.active=true;
 failInfo=true;clear();kebab_backboardd_report();balanced();assert(threadReads==32&&threadDrops==32);failInfo=false;
 failCopy=true;clear();kebab_backboardd_report();balanced();assert(output.find("kframe[")==std::string::npos&&copies==64);failCopy=false;
 for(auto &t:store)t.state=TH_RUN;clear();kebab_backboardd_report();balanced();assert(!copies);for(auto &t:store)t.state=TH_WAIT;
 for(auto &t:store)t.continuation=(void(*)())1;clear();kebab_backboardd_report();balanced();assert(copies==32);for(auto &t:store)t.continuation=nullptr;
 clear();kebab_pid1_report();balanced();assert(threadReads==32&&output.find("KEBAB PID1: tid=")!=std::string::npos);
 processes.clear();for(int i=0;i<600;i++)processes.push_back({1,0,0,100+i,1,i==520?"backboardd":"other"});clear();kebab_backboardd_report();balanced();assert(listCalls==512&&!threadReads&&output.find("scanned=512")!=std::string::npos);
 processes[511].name="backboardd";clear();kebab_backboardd_report();balanced();assert(listCalls==512&&threadReads==32);
 processes.clear();clear();kebab_pid1_report();balanced();assert(output.find("process absent")!=std::string::npos);

 // Production save-area selection: no kernel/32-bit/truncated state read as64bit.
 activeTask.threads={&store[0]};activeTask.thread_count=1;processes={{1,0,501,57,1,"backboardd"}};
 clear();kebab_backboardd_report();balanced();assert(output.find("saved pc=0x1234 lr=0x2345")!=std::string::npos&&!mapReads);
 store[0].user.ash.flavor=32;clear();kebab_backboardd_report();balanced();assert(output.find("KEBAB BBOARD USER:")==std::string::npos);store[0].user.ash.flavor=64;
 store[0].user.ash.count=0;clear();kebab_backboardd_report();balanced();assert(output.find("KEBAB BBOARD USER:")==std::string::npos);store[0].user.ash.count=84;
 store[0].hasUser=false;clear();kebab_backboardd_report();balanced();assert(output.find("KEBAB BBOARD USER:")==std::string::npos);store[0].hasUser=true;
 failMap=true;clear();kebab_backboardd_report();balanced();assert(output.find("KEBAB BBOARD USER:")==std::string::npos);failMap=false;
 userMap.kernel=true;clear();kebab_backboardd_report();balanced();assert(output.find("KEBAB BBOARD USER:")==std::string::npos&&!mapReads);userMap.kernel=false;
 // Actual bounded foreign-map walker: eight reads, faults, map edges, cycles,
 // alignment and distance guards. Frame contents are explicit stand-ins.
 store[0].user.fp=0x1000;
 for(unsigned i=0;i<modelRows.size();i++)modelRows[i]={0x1010+16*i,0x4000+4*i};
 clear();kebab_backboardd_report();balanced();assert(mapReads==8&&output.find("frame[7]=0x401c")!=std::string::npos&&output.find("frame[8]")==std::string::npos);
 failMapRead=true;clear();kebab_backboardd_report();balanced();assert(mapReads==1&&output.find("read stopped error=0x1 depth=0")!=std::string::npos&&output.find("KEBAB BBOARD USER: tid=101 frame[")==std::string::npos);failMapRead=false;
 userMap.high=0x1008;clear();kebab_backboardd_report();balanced();assert(!mapReads);userMap.high=0x2000;
 store[0].user.fp=0xff0;clear();kebab_backboardd_report();balanced();assert(!mapReads);
 store[0].user.fp=0x2000;clear();kebab_backboardd_report();balanced();assert(!mapReads);
 store[0].user.fp=0x1008;clear();kebab_backboardd_report();balanced();assert(!mapReads);
 store[0].user.fp=0x1000;modelRows[0][0]=0x1000;clear();kebab_backboardd_report();balanced();assert(mapReads==1);
 userMap.high=0x20000;modelRows[0][0]=0x11010;clear();kebab_backboardd_report();balanced();assert(mapReads==1);userMap.high=0x2000;
 modelRows[0][0]=0x1010;modelRows[0][1]=0;clear();kebab_backboardd_report();balanced();assert(mapReads==1&&output.find("KEBAB BBOARD USER: tid=101 frame[")==std::string::npos);
 clear();kebab_pid1_report();balanced();assert(!mapReads&&output.find("KEBAB BBOARD USER:")==std::string::npos);

 // New fixed UI command reuses the actual complete bounded task/map walker.
 store[0].user.fp=0x1000;
 for(unsigned i=0;i<modelRows.size();i++)modelRows[i]={0x1010+16*i,0x4000+4*i};
 processes={{1,0,0,1,0,"launchd"},{1,0,501,181,1,"SpringBoardHelp"},{1,0,501,182,2,"SpringBoard"},{1,0,501,192,1,"SpringBoard"},{1,0,0,191,1,"PreBoard"}};
 clear();kebab_ui_report();balanced();
 assert(listCalls==9&&threadReads==2&&threadDrops==2&&mapReads==32);
 assert(output.find("KEBAB UI-SB: fixed process pid=192 uid=501")!=std::string::npos&&output.find("KEBAB UI-PB: fixed process pid=191 uid=0")!=std::string::npos);
 assert(output.find("KEBAB UI-SB USER: tid=101 saved pc=0x1234")!=std::string::npos&&output.find("KEBAB UI-PB USER: tid=101 frame[15]=0x403c")!=std::string::npos);
 assert(output.find("KEBAB BBOARD USER:")==std::string::npos&&output.find("KEBAB UI: observer end;")!=std::string::npos);
 for(unsigned unsafe=0;unsafe<3;unsafe++){clear();interruptContext=unsafe==0;preemptionLevel=unsafe==1;interruptsEnabled=unsafe!=2;kebab_ui_report();balanced();assert(output.empty()&&!listCalls&&!copies&&!mapReads);interruptContext=preemptionLevel=0;interruptsEnabled=true;}
 nullTask=true;clear();kebab_ui_report();balanced();assert(!threadReads&&!mapReads&&output.find("found=1")!=std::string::npos&&output.find("KEBAB UI: observer end;")!=std::string::npos);nullTask=false;
 activeTask.active=false;clear();kebab_ui_report();balanced();assert(!threadReads&&!mapReads&&output.find("thread snapshot count=0")!=std::string::npos);activeTask.active=true;
 failMapRead=true;clear();kebab_ui_report();balanced();assert(mapReads==2&&output.find("KEBAB UI-SB USER: tid=101 read stopped error=0x1 depth=0")!=std::string::npos&&output.find("KEBAB UI-PB USER: tid=101 read stopped error=0x1 depth=0")!=std::string::npos);failMapRead=false;
 store[0].state=TH_RUN;clear();kebab_ui_report();balanced();assert(!copies&&!mapReads&&output.find(" USER:")==std::string::npos);store[0].state=TH_WAIT;
 processes.clear();for(int i=0;i<600;i++)processes.push_back({1,0,0,100+i,1,i==520?"SpringBoard":"other"});
 clear();kebab_ui_report();balanced();assert(listCalls==1024&&!threadReads&&output.find("scanned=512")!=std::string::npos);
 processes[511].name="SpringBoard";clear();kebab_ui_report();balanced();assert(listCalls==1024&&threadReads==1&&mapReads==16);

 // The helper itself caps even a mistaken internal caller at16. The fixed
 // wrappers still give BBOARD8, UI16; zero requests no frame pairs.
 struct kebab_saved_user_site bounded_site={0x1234,0x2345,0x1000};
 clear();vm_map_t retained=get_task_map_reference(&activeTask);
 kebab_user_wait_report(retained,"UI-SB",101,&bounded_site,32);
 vm_map_deallocate(retained);balanced();assert(mapReads==16&&output.find("frame[15]=0x403c")!=std::string::npos&&output.find("frame[16]")==std::string::npos);
 clear();retained=get_task_map_reference(&activeTask);
 kebab_user_wait_report(retained,"UI-SB",101,&bounded_site,0);
 vm_map_deallocate(retained);balanced();assert(!mapReads);

 // A truncated prefix and a full-name extension must not select the migrator.
 processes={{1,0,0,1,0,"launchd"},{1,0,0,110,1,"com.apple.datami"},{1,0,0,111,1,"com.apple.datamigratorx"},{1,0,0,112,2,"com.apple.datamigrator"},{1,0,0,132,1,"com.apple.datamigrator"}};
 clear();kebab_migrator_report();balanced();
 assert(listCalls==5&&threadReads==1&&threadDrops==1&&mapReads==16);
 assert(output.find("KEBAB MIG: fixed process pid=132 uid=0")!=std::string::npos&&output.find("KEBAB MIG USER: tid=101 frame[15]=0x403c")!=std::string::npos&&output.find("KEBAB MIG: observer end;")!=std::string::npos);
 processes[4].uid=501;clear();kebab_migrator_report();balanced();assert(output.find("pid=132 uid=501")!=std::string::npos&&mapReads==16);
 for(unsigned unsafe=0;unsafe<3;unsafe++){clear();interruptContext=unsafe==0;preemptionLevel=unsafe==1;interruptsEnabled=unsafe!=2;kebab_migrator_report();balanced();assert(output.empty()&&!listCalls&&!copies&&!mapReads);interruptContext=preemptionLevel=0;interruptsEnabled=true;}
 nullTask=true;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("found=1")!=std::string::npos);nullTask=false;
 activeTask.active=false;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("thread snapshot count=0")!=std::string::npos);activeTask.active=true;
 failMapRead=true;clear();kebab_migrator_report();balanced();assert(mapReads==1&&output.find("KEBAB MIG USER: tid=101 read stopped error=0x1 depth=0")!=std::string::npos);failMapRead=false;
 store[0].state=TH_RUN;clear();kebab_migrator_report();balanced();assert(!copies&&!mapReads&&output.find(" USER:")==std::string::npos);store[0].state=TH_WAIT;
 processes[4].name="com.apple.datamigrator.extension";clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("found=0")!=std::string::npos);
 processes.clear();for(int i=0;i<600;i++)processes.push_back({1,0,0,100+i,1,i==520?"com.apple.datamigrator":"other"});
 clear();kebab_migrator_report();balanced();assert(listCalls==512&&!threadReads&&output.find("scanned=512")!=std::string::npos);
 processes[511].name="com.apple.datamigrator";clear();kebab_migrator_report();balanced();assert(listCalls==512&&threadReads==1&&mapReads==16);
 fprintf(stdout,"PASS actual UI16/BBOARD8 depth and strict16global bound + legacy observers: fixed names/parent/actualUID, cap512 each/32threads/8frames, accurate UI labels, balanced refs and unsafe contexts; map references, no locks during reads,8frames,64KiB window, exact64bit saved-state, faults/kernel-map/IRQ refusals; parent/exact-name selection, actual UID, cap512/32, inactive/missing/fault/running/continuation paths, no print under locks and balanced references; task/scheduler/nofault explicit stand-ins\n");
}
