
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
static bool transportModel=false,disableDrain=false,hostReading=true;
static std::string transportRing,transportDelivered;
static unsigned dropped=0,flushCalls=0;
static void kebab_usb_log_flush(){
 assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth);
 flushCalls++;
 if(transportModel&&!disableDrain&&hostReading){transportDelivered+=transportRing;transportRing.clear();}
}

static int checked_printf(const char *format,...){assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth);char line[2048];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);output+=line;if(transportModel){for(char c:std::string(line)){if(transportRing.size()<16383)transportRing+=c;else dropped++;}}return n;}
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
struct proc{unsigned p_stat=1,p_flag=0,uid=0;int pid=1,parent=1;std::string name;std::array<uint8_t,16> uuid{};};using proc_t=proc *;
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
static kern_return_t legacy_copyinmap(vm_map_t m,uintptr_t from,void *to,uintptr_t n){
 assert(!taskDepth&&!threadDepth&&!splDepth&&!procDepth&&m==&userMap&&!m->kernel&&mapRefs==1&&interruptsEnabled);
 assert(n==16&&from>=m->low&&from<m->high&&m->high-from>=n&&!(from&15));mapReads++;
 if(failMapRead)return 1;unsigned i=(from-0x1000)/16;assert(i<modelRows.size());memcpy(to,modelRows[i].data(),n);return 0;
}

using proc_name_t=char[2*MAXCOMLEN+1];
static const char *proc_best_name(proc_t p){assert(procDepth==1&&procRefs);return p->name.c_str();}

static bool pluginMemory=false,failTaskInfo=true,changePrefix=false;
static unsigned prefixReads=0;
struct task_dyld_info_data_t{uint64_t all_image_info_addr=0,all_image_info_size=0;unsigned all_image_info_format=0;};
using task_info_t=task_dyld_info_data_t *;
enum{TASK_DYLD_INFO=17,TASK_DYLD_INFO_COUNT=5,TASK_DYLD_ALL_IMAGE_INFO_64=2};
static task_dyld_info_data_t dyldInfo;
static unsigned returnedCount=TASK_DYLD_INFO_COUNT;
static kern_return_t task_info(task_t t,int flavor,task_info_t out,mach_msg_type_number_t *count){
 assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&taskRefs==2&&t==&activeTask&&flavor==TASK_DYLD_INFO&&*count==TASK_DYLD_INFO_COUNT);
 if(failTaskInfo||!t->active)return 5;*out=dyldInfo;*count=returnedCount;return 0;
}
static void proc_getexecutableuuid(proc_t p,void *out,unsigned size){assert(procDepth==1&&procRefs&&size==16);memcpy(out,p->uuid.data(),16);}
static std::array<uint8_t,0x18000> memory{};
static uint64_t faultAt=0;
static kern_return_t copyinmap(vm_map_t m,uintptr_t from,void *to,uintptr_t n){
 if(!pluginMemory)return legacy_copyinmap(m,from,to,n);
 assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&interruptsEnabled&&m==&userMap&&!m->kernel&&mapRefs==1);
 assert(from>=m->low&&from<m->high&&m->high-from>=n);mapReads++;
 if(failMapRead||(faultAt&&from<=faultAt&&faultAt-from<n))return 1;
 if(from<0x1000||from-0x1000>memory.size()||n>memory.size()-(from-0x1000))return 1;
 memcpy(to,memory.data()+from-0x1000,n);
 if(from==dyldInfo.all_image_info_addr&&n==16&&++prefixReads==2&&changePrefix)((uint64_t *)to)[0]^=1;
 return 0;
}
template<class T>static void put(uint64_t at,const T &value){assert(at>=0x1000&&at-0x1000+sizeof(value)<=memory.size());memcpy(memory.data()+at-0x1000,&value,sizeof(value));}
static void putstr(uint64_t at,const std::string &s){assert(at>=0x1000&&at-0x1000+s.size()+1<=memory.size());memcpy(memory.data()+at-0x1000,s.c_str(),s.size()+1);}

static unsigned references,ndActive,indexNow;static int failLookup=-1,failAttr=-1;static bool unsupported=false;
enum{LOOKUP=1,OP_LOOKUP=2,NOFOLLOW=4,UIO_SYSSPACE=8,NAMEI_NOFOLLOW_ANY=16};
using vfs_context_t=int;struct vnode{unsigned index;};static vnode node;using vnode_t=vnode *;
struct nameidata{unsigned ni_flag;vnode_t ni_vp;const char *path;};
static vfs_context_t vfs_context_current(){return 17;}
static void initialize(nameidata *nd,int look,int op,int nofollow,int segment,const char *path,vfs_context_t context){assert(look==1&&op==2&&nofollow==4&&segment==8&&context==17&&!references&&!ndActive);assert(!strncmp(path,"/private/var",12)&&!strstr(path,".."));nd->ni_flag=0;nd->path=path;nd->ni_vp=nullptr;}
#define NDINIT initialize
#define CAST_USER_ADDR_T(p) (p)
static int namei(nameidata *nd){assert(nd->ni_flag==16&&!references&&!ndActive);node.index=indexNow++;if((int)node.index==failLookup)return 40;ndActive=references=1;nd->ni_vp=&node;return 0;}
static void nameidone(nameidata *nd){assert(nd->ni_vp==&node&&ndActive==1&&references==1);ndActive=0;}
struct vnode_attr{unsigned wanted,supported,va_uid,va_gid,va_mode,va_flags,va_dataprotect_class;};
enum{bit_va_uid=1,bit_va_gid=2,bit_va_mode=4,bit_va_flags=8,bit_va_dataprotect_class=16};
#define VATTR_INIT(p) do{(p)->wanted=(p)->supported=0;}while(0)
#define VATTR_WANTED(p,f) ((p)->wanted|=bit_##f)
#define VATTR_IS_SUPPORTED(p,f) ((p)->supported&bit_##f)
static int vnode_getattr(vnode_t p,vnode_attr *attr,vfs_context_t context){assert(p==&node&&references==1&&!ndActive&&context==17&&attr->wanted==31);assert(!attr->va_uid&&!attr->va_gid&&!attr->va_mode&&!attr->va_flags&&!attr->va_dataprotect_class);if((int)p->index==failAttr)return 5;if(!unsupported){attr->supported=31;attr->va_uid=501;attr->va_gid=501;attr->va_mode=0751;attr->va_flags=0;attr->va_dataprotect_class=4;}return 0;}
static bool vnode_isdir(vnode_t p){assert(p==&node&&references==1&&!ndActive);return true;}
static void vnode_put(vnode_t p){assert(p==&node&&references==1&&!ndActive);references=0;}
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

/* These fixed reports run on the normal USB event thread. Drain outside all
 * task/thread/scheduler locks between bounded thread records, otherwise a fast
 * multi-thread snapshot can fill the 16KiB TX ring before the event loop runs.
 * The existing transport flush is bounded and does not promise host delivery;
 * a disconnected/slow host must still be detected by the capture completion.
 * Keep the complete frame budgets and normal daemon output unchanged. */
static void
kebab_observer_drain(void)
{
    extern void kebab_usb_log_flush(void);
    kebab_usb_log_flush();
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
	kebab_observer_drain();
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
		kebab_observer_drain();
	}
	if (map != VM_MAP_NULL) vm_map_deallocate(map);
	printf("KEBAB %s: snapshot end; addresses name kernel waits, not user PCs\n", label);
	kebab_observer_drain();
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

/* Public64-bit dyld_all_image_infos prefix is version/count/pointer (16B);
 * dyld_image_info is loadAddress/path/modDate (24B). Only original system
 * DataClassMigrators paths are reported, not a general user-memory API.
 * These are bounded best-effort reads: identical prefix rereads do not prove
 * the target was suspended or that no load/unload occurred in between. */
static bool
kebab_plugin_user_copy(vm_map_t map, uint64_t address, void *out, size_t bytes)
{
    if (map == VM_MAP_NULL || vm_map_pmap(map) == pmap_kernel() || !bytes ||
        address < vm_map_min(map) || address >= vm_map_max(map) ||
        vm_map_max(map) - address < bytes) return false;
    return copyinmap(map, address, out, bytes) == KERN_SUCCESS;
}
static bool
kebab_plugin_image_uuid(vm_map_t map, uint64_t base, uint8_t uuid[16])
{
    uint32_t header[8];
    if (!kebab_plugin_user_copy(map, base, header, sizeof(header)) ||
        header[0] != 0xfeedfacfu || header[4] > 128 || header[5] > 16384 ||
        vm_map_max(map) - base < sizeof(header) + (uint64_t)header[5]) return false;
    uint64_t offset = 0;
    for (unsigned i = 0; i < header[4]; i++) {
        uint32_t command[2];
        if (offset > header[5] || header[5] - offset < sizeof(command) ||
            !kebab_plugin_user_copy(map, base + sizeof(header) + offset, command, sizeof(command)) ||
            command[1] < sizeof(command) || command[1] > header[5] - offset) return false;
        if (command[0] == 0x1bu) {
            return command[1] == 24 &&
                kebab_plugin_user_copy(map, base + sizeof(header) + offset + sizeof(command), uuid, 16);
        }
        offset += command[1];
    }
    return false;
}
static void
kebab_plugin_images_report(task_t task)
{
    task_dyld_info_data_t info = {};
    mach_msg_type_number_t size = TASK_DYLD_INFO_COUNT;
    kern_return_t result = task_info(task, TASK_DYLD_INFO, (task_info_t)&info, &size);
    if (result != KERN_SUCCESS || size != TASK_DYLD_INFO_COUNT ||
        info.all_image_info_format != TASK_DYLD_ALL_IMAGE_INFO_64 ||
        info.all_image_info_size < 16) {
        printf("KEBAB MIG-W IMAGES: dyld info unavailable result=0x%x count=%u\n", result, size);
        return;
    }
    vm_map_t map = get_task_map_reference(task);
    if (map == VM_MAP_NULL) return;
    uint64_t head[2], again[2];
    struct { uint64_t base; char path[256]; uint8_t uuid[16]; bool have_uuid; } selected[8] = {};
    unsigned found = 0, scanned = 0;
    bool stable = false, readable = kebab_plugin_user_copy(map, info.all_image_info_addr, head, sizeof(head));
    if (readable && (uint32_t)head[0] >= 1 && (uint32_t)head[0] <= 20 && head[1] != 0) {
        unsigned count = (unsigned)(head[0] >> 32), limit = count < 512 ? count : 512;
        static const char prefix[] = "/System/Library/DataClassMigrators/";
        for (; scanned < limit && found < 8; scanned++) {
            uint64_t record[3];
            if (head[1] > UINT64_MAX - (uint64_t)scanned * sizeof(record) ||
                !kebab_plugin_user_copy(map, head[1] + (uint64_t)scanned * sizeof(record), record, sizeof(record))) break;
            char first[sizeof(prefix) - 1];
            if (!kebab_plugin_user_copy(map, record[1], first, sizeof(first)) ||
                memcmp(first, prefix, sizeof(first))) continue;
            char path[256] = {};bool complete = false;
            memcpy(path, first, sizeof(first));
            for (unsigned j = sizeof(first); j < sizeof(path); j++) {
                if (record[1] > UINT64_MAX - j ||
                    !kebab_plugin_user_copy(map, record[1] + j, &path[j], 1)) break;
                if (path[j] == '\0') { complete = true;break; }
                if ((unsigned char)path[j] < 32 || (unsigned char)path[j] > 126) break;
            }
            if (!complete) continue;
            selected[found].base = record[0];
            memcpy(selected[found].path, path, sizeof(path));
            selected[found].have_uuid = kebab_plugin_image_uuid(map, record[0], selected[found].uuid);
            found++;
        }
        stable = kebab_plugin_user_copy(map, info.all_image_info_addr, again, sizeof(again)) &&
            head[0] == again[0] && head[1] == again[1];
    }
    vm_map_deallocate(map);
    if (!stable) {
        printf("KEBAB MIG-W IMAGES: unavailable/changing prefix; no module inventory accepted\n");
        return;
    }
    printf("KEBAB MIG-W IMAGES: dyld-version=%u count=%u scanned=%u found=%u (cap512/8, best-effort)\n",
        (unsigned)head[0], (unsigned)(head[0] >> 32), scanned, found);
    for (unsigned i = 0; i < found; i++) {
        printf("KEBAB MIG-W IMAGE: base=0x%llx uuid-valid=%u uuid=", selected[i].base, selected[i].have_uuid);
        for (unsigned j = 0; j < 16; j++) printf("%02x", selected[i].uuid[j]);
        printf(" path=%s\n", selected[i].path);
    }
}
void kebab_plugin_task_report(task_t task);
void
kebab_plugin_task_report(task_t task)
{
    kebab_task_threads_report(task, "MIG-W", true, 16);
    kebab_plugin_images_report(task);
}
void kebab_installer_task_report(task_t task);
void
kebab_installer_task_report(task_t task)
{
    kebab_task_threads_report(task, "MIG-I", true, 16);
}
#endif /* KEBAB */

/* Normal USB boot observability. Snapshot a bounded set of referenced objects;
 * never print while holding a proc/task/thread lock. No thread suspension,
 * saved-state modification or filesystem mutation. Fixed migration metadata
 * and user frames are read only, outside proc/task/thread locks.
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

/* Fixed launchd children only. Names are mutable diagnostic identifiers,
 * never authorization. The wrapper name fills proc_name_t: also match the
 * original executable UUID so a longer, truncated prefix is not selected.
 * No task suspension or filesystem lookup is used to identify the process. */
extern void kebab_migrator_task_report(task_t);
extern void kebab_plugin_task_report(task_t);
extern void kebab_installer_task_report(task_t);
void kebab_migrator_report(void);
struct kebab_migrator_snapshot {
    const char *name; const uint8_t *expected_uuid; task_t task;
    unsigned scanned; int pid; unsigned uid; bool found; unsigned uuid_refused;
};
static int
kebab_migrator_select(proc_t process, void *argument)
{
    struct kebab_migrator_snapshot *snapshot = (struct kebab_migrator_snapshot *)argument;
    char name[sizeof(proc_name_t)] = {};
    uint8_t uuid[16] = {};
    ++snapshot->scanned;
    proc_lock(process);
    strlcpy(name, proc_best_name(process), sizeof(name));
    if (snapshot->expected_uuid) proc_getexecutableuuid(process, uuid, sizeof(uuid));
    proc_unlock(process);
    if (!strcmp(name, snapshot->name) && proc_ppid(process) == 1) {
        snapshot->found = true;
        if (snapshot->expected_uuid && memcmp(uuid, snapshot->expected_uuid, sizeof(uuid))) {
            ++snapshot->uuid_refused;
        } else {
            snapshot->pid = proc_pid(process);snapshot->uid = proc_getuid(process);
            task_t task = proc_task(process);
            if (task != TASK_NULL) {task_reference(task);snapshot->task = task;}
            return PROC_RETURNED_DONE;
        }
    }
    return snapshot->scanned >= 512 ? PROC_RETURNED_DONE : PROC_RETURNED;
}
/* Fixed canonical Data paths implicated by original installer EACCES.
 * No directory enumeration, arbitrary path input, authorization override or
 * metadata write. Refuse symlinks in any component; report only supported
 * attributes and release vnode/namei state before printing. This is normal
 * sleepable context only, not a panic/interrupt filesystem operation. */
static void
kebab_installer_directories_report(void)
{
    static const char *const paths[] = {
        "/private/var",
        "/private/var/containers",
        "/private/var/containers/Shared",
        "/private/var/containers/Shared/SystemGroup",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library/InstallCoordination"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        struct nameidata nd;
        vfs_context_t context = vfs_context_current();
        NDINIT(&nd, LOOKUP, OP_LOOKUP, NOFOLLOW, UIO_SYSSPACE,
            CAST_USER_ADDR_T(paths[i]), context);
        nd.ni_flag |= NAMEI_NOFOLLOW_ANY;
        int error = namei(&nd);
        if (error) {
            printf("KEBAB MIG-DIR: index=%u lookup=%d path=%s (fixed nofollow)\n", i, error, paths[i]);
            continue;
        }
        vnode_t vnode = nd.ni_vp;
        nameidone(&nd);
        struct vnode_attr attr = {};
        VATTR_INIT(&attr);
        VATTR_WANTED(&attr, va_uid);VATTR_WANTED(&attr, va_gid);
        VATTR_WANTED(&attr, va_mode);VATTR_WANTED(&attr, va_flags);
        VATTR_WANTED(&attr, va_dataprotect_class);
        error = vnode_getattr(vnode, &attr, context);
        bool directory = vnode_isdir(vnode);
        vnode_put(vnode);
        unsigned supported = (VATTR_IS_SUPPORTED(&attr, va_uid) ? 1u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_gid) ? 2u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_mode) ? 4u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_flags) ? 8u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_dataprotect_class) ? 16u : 0u);
        printf("KEBAB MIG-DIR: index=%u getattr=%d dir=%u supported=0x%x uid=%u gid=%u mode=0%o flags=0x%x class=%u path=%s\n",
            i, error, directory, supported, attr.va_uid, attr.va_gid,
            attr.va_mode & 07777, attr.va_flags, attr.va_dataprotect_class, paths[i]);
    }
}

void
kebab_migrator_report(void)
{
    if (ml_at_interrupt_context() || get_preemption_level() != 0 ||
        !ml_get_interrupts_enabled()) return;
    kebab_installer_directories_report();
    static const char *const names[] = {"com.apple.datamigrator", "com.apple.migrationpluginwrapper", "installcoordinationd"};
    static const char *const labels[] = {"MIG", "MIG-W", "MIG-I"};
    static const uint8_t wrapper_uuid[16] = {0xa3,0x6b,0x14,0xaa,0x98,0x19,0x3d,0x1f,0x90,0x0c,0xe8,0x0b,0x45,0xc8,0xea,0xc7};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        struct kebab_migrator_snapshot snapshot = {};
        snapshot.name = names[i];snapshot.expected_uuid = i == 1 ? wrapper_uuid : NULL;
        proc_iterate(PROC_ALLPROCLIST | PROC_NOWAITTRANS, kebab_migrator_select, &snapshot, NULL, NULL);
        if (snapshot.task == TASK_NULL) {
            printf("KEBAB %s: no referenced task found=%u scanned=%u uuid-refused=%u (parent1/full-name; cap512)\n",
                labels[i], snapshot.found, snapshot.scanned, snapshot.uuid_refused);
        } else {
            printf("KEBAB %s: fixed process pid=%d uid=%u scanned=%u; best-effort wait snapshot\n",
                labels[i], snapshot.pid, snapshot.uid, snapshot.scanned);
            if (i == 0) kebab_migrator_task_report(snapshot.task);
            else if (i == 1) kebab_plugin_task_report(snapshot.task);
            else kebab_installer_task_report(snapshot.task);
            task_deallocate(snapshot.task);
        }
    }
    printf("KEBAB MIG: observer end; no suspension or modification; user frames are best-effort reads\n");
}
#endif /* KEBAB */
static void clear(){assert(!references&&!ndActive);indexNow=0;output.clear();listCalls=threadReads=threadDrops=copies=mapReads=0;assert(!mapRefs);assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&!procRefs&&taskRefs==1);}
static void balanced(){assert(!references&&!ndActive);assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth&&!procRefs&&taskRefs==1);assert(!mapRefs);for(auto t:activeTask.threads)assert(t->refs==1);}
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
 assert(listCalls==15&&threadReads==1&&threadDrops==1&&mapReads==16);
 assert(output.find("KEBAB MIG: fixed process pid=132 uid=0")!=std::string::npos&&output.find("KEBAB MIG USER: tid=101 frame[15]=0x403c")!=std::string::npos&&output.find("KEBAB MIG: observer end;")!=std::string::npos);
 processes[4].uid=501;clear();kebab_migrator_report();balanced();assert(output.find("pid=132 uid=501")!=std::string::npos&&mapReads==16);
 for(unsigned unsafe=0;unsafe<3;unsafe++){clear();interruptContext=unsafe==0;preemptionLevel=unsafe==1;interruptsEnabled=unsafe!=2;kebab_migrator_report();balanced();assert(output.empty()&&!listCalls&&!copies&&!mapReads);interruptContext=preemptionLevel=0;interruptsEnabled=true;}
 nullTask=true;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("found=1")!=std::string::npos);nullTask=false;
 activeTask.active=false;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("thread snapshot count=0")!=std::string::npos);activeTask.active=true;
 failMapRead=true;clear();kebab_migrator_report();balanced();assert(mapReads==1&&output.find("KEBAB MIG USER: tid=101 read stopped error=0x1 depth=0")!=std::string::npos);failMapRead=false;
 store[0].state=TH_RUN;clear();kebab_migrator_report();balanced();assert(!copies&&!mapReads&&output.find(" USER:")==std::string::npos);store[0].state=TH_WAIT;
 processes[4].name="com.apple.datamigrator.extension";clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("found=0")!=std::string::npos);
 processes.clear();for(int i=0;i<600;i++)processes.push_back({1,0,0,100+i,1,i==520?"com.apple.datamigrator":"other"});
 clear();kebab_migrator_report();balanced();assert(listCalls==1536&&!threadReads&&output.find("scanned=512")!=std::string::npos);
 processes[511].name="com.apple.datamigrator";clear();kebab_migrator_report();balanced();assert(listCalls==1536&&threadReads==1&&mapReads==16);

 // Exact wrapper UUID is required even if a longer name truncates to32bytes.
 std::array<uint8_t,16> wrapperUuid={0xa3,0x6b,0x14,0xaa,0x98,0x19,0x3d,0x1f,0x90,0x0c,0xe8,0x0b,0x45,0xc8,0xea,0xc7};
 processes={{1,0,501,260,1,"com.apple.migrationpluginwrapper"}};
 clear();kebab_migrator_report();balanced();assert(!threadReads&&output.find("uuid-refused=1")!=std::string::npos);
 processes[0].uuid=wrapperUuid;clear();kebab_migrator_report();balanced();assert(threadReads==1&&mapReads==16&&output.find("KEBAB MIG-W: fixed process pid=260 uid=501")!=std::string::npos&&output.find("dyld info unavailable")!=std::string::npos);
 processes[0].name+=".other";processes[0].uuid={};clear();kebab_migrator_report();balanced();assert(!threadReads);
 processes[0].name="com.apple.migrationpluginwrapper";processes[0].uuid=wrapperUuid;processes[0].parent=2;clear();kebab_migrator_report();balanced();assert(!threadReads);processes[0].parent=1;

 // Real production dyld parser, bounded model memory; not actual task proof.
 pluginMemory=true;failTaskInfo=false;userMap.high=0x19000;store[0].user.fp=0;
 auto setup=[&](){memory.fill(0);dyldInfo={0x2000,16,2};returnedCount=5;prefixReads=0;changePrefix=false;faultAt=0;
  std::array<uint64_t,2> head={1ull|(1ull<<32),0x3000};put(0x2000,head);
  std::array<uint64_t,3> rec={0x5000,0x4000,0};put(0x3000,rec);
  putstr(0x4000,"/System/Library/DataClassMigrators/Test.migrator/Test");
  std::array<uint32_t,8> hdr={0xfeedfacf,0,0,0,1,24,0,0};put(0x5000,hdr);
  std::array<uint32_t,2> cmd={0x1b,24};put(0x5020,cmd);put(0x5028,wrapperUuid);
 };
 auto images=[&](){clear();prefixReads=0;task_reference(&activeTask);kebab_plugin_images_report(&activeTask);task_deallocate(&activeTask);balanced();};
 setup();images();assert(output.find("found=1")!=std::string::npos&&output.find("uuid-valid=1 uuid=a36b14aa98193d1f900ce80b45c8eac7")!=std::string::npos&&output.find("path=/System/Library/DataClassMigrators/Test.migrator/Test")!=std::string::npos);
 setup();clear();kebab_migrator_report();balanced();assert(output.find("MIG-W USER:")!=std::string::npos&&output.find("MIG-W IMAGE:")!=std::string::npos);
 for(unsigned v:{0u,21u,0xffffffffu}){setup();uint64_t word=v|(1ull<<32);put(0x2000,word);images();assert(output.find("no module inventory accepted")!=std::string::npos&&output.find("MIG-W IMAGE:")==std::string::npos);}
 setup();putstr(0x4000,"/private/var/NotASystemPlugin");images();assert(output.find("found=0")!=std::string::npos);
 setup();memory[0x4000-0x1000+36]=1;images();assert(output.find("found=0")!=std::string::npos);
 setup();memset(memory.data()+0x4000-0x1000+35,'A',256);images();assert(output.find("found=0")!=std::string::npos);
 setup();faultAt=0x4000+40;images();assert(output.find("found=0")!=std::string::npos);
 setup();changePrefix=true;images();assert(output.find("no module inventory accepted")!=std::string::npos&&output.find("MIG-W IMAGE:")==std::string::npos);
 for(unsigned field:{0u,4u,5u}){setup();uint32_t bad=field==0?0:0xffffffffu;put(0x5000+field*4,bad);images();assert(output.find("uuid-valid=0")!=std::string::npos);}
 setup();uint32_t badCount=129;put(0x5010,badCount);images();assert(output.find("uuid-valid=0")!=std::string::npos);
 setup();uint32_t badExtent=16385;put(0x5014,badExtent);images();assert(output.find("uuid-valid=0")!=std::string::npos);
 for(unsigned size:{0u,7u,23u,25u,0xffffffffu}){setup();put(0x5024,size);images();assert(output.find("uuid-valid=0")!=std::string::npos);}
 setup();faultAt=0x5028;images();assert(output.find("uuid-valid=0")!=std::string::npos);
 setup();uint32_t otherCmd=2;put(0x5020,otherCmd);images();assert(output.find("uuid-valid=0")!=std::string::npos);
 setup();failTaskInfo=true;images();assert(!mapReads);failTaskInfo=false;
 setup();returnedCount=4;images();assert(!mapReads);
 setup();dyldInfo.all_image_info_format=1;images();assert(!mapReads);
 setup();dyldInfo.all_image_info_size=15;images();assert(!mapReads);
 setup();failMap=true;images();assert(!mapReads);failMap=false;
 setup();userMap.kernel=true;images();assert(!mapReads&&output.find("MIG-W IMAGE:")==std::string::npos);userMap.kernel=false;
 for(uint64_t at:{0xfffull,0x19000ull,0xfffffffffffffff8ull}){setup();dyldInfo.all_image_info_addr=at;images();assert(!mapReads);}
 setup();faultAt=0x2000;images();assert(output.find("no module inventory accepted")!=std::string::npos);
 setup();std::array<uint64_t,2> nullArray={1,0};put(0x2000,nullArray);images();assert(output.find("no module inventory accepted")!=std::string::npos);
 //512 records and8 selected images, independent caps, including huge count.
 setup();std::array<uint64_t,2> manyHead={1ull|(0xffffffffull<<32),0x6000};put(0x2000,manyHead);
 std::array<uint64_t,3> noPlugin={0x5000,0x4000,0};putstr(0x4000,"/outside");for(unsigned i=0;i<513;i++)put(0x6000+24*i,noPlugin);images();assert(output.find("scanned=512 found=0")!=std::string::npos);
 putstr(0x4000,"/System/Library/DataClassMigrators/Test");images();assert(output.find("scanned=8 found=8")!=std::string::npos);size_t matches=0,pos=0;while((pos=output.find("KEBAB MIG-W IMAGE:",pos))!=std::string::npos){matches++;pos++;}assert(matches==8);
 // Address bounds refuse before attempting copy, including command span.
 setup();task_reference(&activeTask);vm_map_t retainedMap=get_task_map_reference(&activeTask);uint8_t byte=0;uint8_t uuidOut[16]={};
 mapReads=0;assert(!kebab_plugin_user_copy(retainedMap,0x19000,&byte,1)&&!kebab_plugin_user_copy(retainedMap,0x1000,&byte,0)&&!kebab_plugin_user_copy(retainedMap,0x18fff,&byte,2)&&!mapReads);
 std::array<uint32_t,8> edgeHeader={0xfeedfacf,0,0,0,1,24,0,0};put(0x18fe0,edgeHeader);assert(!kebab_plugin_image_uuid(retainedMap,0x18fe0,uuidOut));vm_map_deallocate(retainedMap);task_deallocate(&activeTask);balanced();

 // Fixed installer receiver has a different full name/label and no wrapper
 // UUID requirement. Existing directory query still precedes all three.
 pluginMemory=false;failTaskInfo=true;userMap.high=0x2000;store[0].user.fp=0x1000;
 for(unsigned i=0;i<modelRows.size();i++)modelRows[i]={0x1010+16*i,0x4000+4*i};
 processes={{1,0,501,301,1,"installcoordinat"},{1,0,501,302,1,"installcoordinationd.extra"},{1,0,501,303,2,"installcoordinationd"},{1,0,501,304,1,"installcoordinationd"}};
 clear();kebab_migrator_report();balanced();assert(listCalls==12&&threadReads==1&&threadDrops==1&&mapReads==16&&indexNow==7);
 assert(output.find("KEBAB MIG-I: fixed process pid=304 uid=501")!=std::string::npos&&output.find("KEBAB MIG-I USER: tid=101 frame[15]=0x403c")!=std::string::npos&&output.find("KEBAB MIG-W IMAGES:")==std::string::npos);
 assert(output.find("KEBAB MIG: observer end;")!=std::string::npos);
 processes[3].uid=0;clear();kebab_migrator_report();balanced();assert(threadReads==1&&output.find("MIG-I: fixed process pid=304 uid=0")!=std::string::npos);
 for(unsigned unsafe=0;unsafe<3;unsafe++){clear();interruptContext=unsafe==0;preemptionLevel=unsafe==1;interruptsEnabled=unsafe!=2;kebab_migrator_report();balanced();assert(output.empty()&&!listCalls&&!mapReads&&!indexNow);interruptContext=preemptionLevel=0;interruptsEnabled=true;}
 nullTask=true;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("MIG-I: no referenced task found=1")!=std::string::npos);nullTask=false;
 activeTask.active=false;clear();kebab_migrator_report();balanced();assert(!threadReads&&!mapReads&&output.find("MIG-I: thread snapshot count=0")!=std::string::npos);activeTask.active=true;
 failMapRead=true;clear();kebab_migrator_report();balanced();assert(mapReads==1&&output.find("MIG-I USER: tid=101 read stopped error=0x1 depth=0")!=std::string::npos);failMapRead=false;
 processes.clear();for(int i=0;i<600;i++)processes.push_back({1,0,501,100+i,1,i==520?"installcoordinationd":"other"});clear();kebab_migrator_report();balanced();assert(listCalls==1536&&!threadReads&&!mapReads);
 processes[511].name="installcoordinationd";clear();kebab_migrator_report();balanced();assert(listCalls==1536&&threadReads==1&&mapReads==16);

 // Three real fixed targets, each with32 model threads and full16pairs:
 // old event-thread behavior loses bytes; the same production report with
 // bounded drains delivers every byte, including all final markers.
 processes={{1,0,501,800,1,"com.apple.datamigrator"},{1,0,501,801,1,"com.apple.migrationpluginwrapper"},{1,0,501,802,1,"installcoordinationd"}};
 // Exact original wrapper executable UUID, as required by the full producer.
 const uint8_t originalWrapperUUID[16]={0xa3,0x6b,0x14,0xaa,0x98,0x19,0x3d,0x1f,0x90,0x0c,0xe8,0x0b,0x45,0xc8,0xea,0xc7};
 memcpy(processes[1].uuid.data(),originalWrapperUUID,16);
 activeTask.threads.clear();activeTask.thread_count=32;
 for(unsigned i=0;i<32;i++)activeTask.threads.push_back(&store[i]);
 for(unsigned i=0;i<32;i++){store[i].user.fp=0x1000;store[i].state=TH_WAIT;store[i].hasUser=true;}
 for(unsigned i=0;i<modelRows.size();i++)modelRows[i]={0x1010+16*i,0x4000+4*i};
 failMapRead=false;pluginMemory=false;failTaskInfo=true;
 for(unsigned policy=0;policy<3;policy++){
  clear();transportModel=true;disableDrain=policy==0;hostReading=policy!=2;
  transportRing.clear();transportDelivered.clear();dropped=flushCalls=0;
  kebab_migrator_report();balanced();
  assert(threadReads==96&&threadDrops==96&&mapReads==1536&&flushCalls==102);
  assert(output.size()>65536&&output.find("KEBAB MIG: observer end;")!=std::string::npos);
  if(policy==1){kebab_usb_log_flush();assert(!dropped&&transportDelivered==output);}
  else {assert(dropped>0&&transportDelivered.empty());}
  transportModel=false;
 }
 fprintf(stdout,"PASS actual complete BSD/Mach production: strict fixed installer receiver/full-name/parent/actualUID/label/refs/16frames/unsafe/cap512; legacy observers and seven nofollow metadata reads; fixed wrapper name/parent/UUID; balanced references/unsafe-context refusals; exact dyld metadata, fixed system prefix, path faults/ASCII/NUL, stable-prefix failure, bounded512/8, LC_UUID validation and map bounds. Scheduler/task/map/UUID APIs are explicit models, not hardware proof.\n");
}
