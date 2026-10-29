/* Mach-private half of normal PID1 observability. The caller holds a task
 * reference. Copy at most 32 thread references under task_lock, then observe
 * each under sched_lock; release everything before printing or returning. */
#if defined(KEBAB)
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/queue.h>
#include <kern/sched_prim.h>
#include <kern/spl.h>
#include <mach/thread_info.h>
#include <kern/misc_protos.h>
#include <machine/machine_routines.h>

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
void kebab_pid1_task_report(task_t task);
void
kebab_pid1_task_report(task_t task)
{
	thread_t observed[32], thread;
	unsigned count = 0, thread_total = 0;
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
	printf("KEBAB PID1: thread snapshot count=%u thread_total=%u (cap32; no suspension)\n", count, thread_total);
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
		thread_unlock(thread);
		splx(priority);
		printf("KEBAB PID1: tid=%llu info=0x%x state=0x%x run=%d flags=0x%x suspend=%d sleep=%d wait=0x%llx continuation=0x%llx user=%d.%06d sys=%d.%06d\n",
		    tid, result, state, basic.run_state, basic.flags, basic.suspend_count, basic.sleep_time,
		    event, continuation, basic.user_time.seconds, basic.user_time.microseconds,
		    basic.system_time.seconds, basic.system_time.microseconds);
		for (unsigned frame = 0; frame < frame_count; frame++) {
			printf("KEBAB PID1: tid=%llu kframe[%u]=0x%llx\n", tid, frame, frames[frame]);
		}
		thread_deallocate(thread);
	}
	printf("KEBAB PID1: snapshot end; addresses name kernel waits, not user PCs\n");
}
#endif /* KEBAB */
