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
		thread_unlock(thread);
		splx(priority);
		printf("KEBAB PID1: tid=%llu info=0x%x state=0x%x run=%d flags=0x%x suspend=%d sleep=%d wait=0x%llx continuation=0x%llx user=%d.%06d sys=%d.%06d\n",
		    tid, result, state, basic.run_state, basic.flags, basic.suspend_count, basic.sleep_time,
		    event, continuation, basic.user_time.seconds, basic.user_time.microseconds,
		    basic.system_time.seconds, basic.system_time.microseconds);
		thread_deallocate(thread);
	}
	printf("KEBAB PID1: snapshot end; addresses name kernel waits, not user PCs\n");
}
#endif /* KEBAB */
