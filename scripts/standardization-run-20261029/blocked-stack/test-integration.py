"""Run actual proc/Mach snapshots with object reference and lock accounting."""
from pathlib import Path
import re
import subprocess
import tempfile

r=Path(__file__).parent
bsd=(r.parent/'bootdiag/after/bsd/kern/kebab_bootdiag.c').read_text()
mach=(r/'after/osfmk/kern/kebab_bootdiag.c').read_text()
strip=lambda s:re.sub(r'^#include [^\n]+\n','',s,flags=re.M)
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#define KEBAB 1
#define uint64_t unsigned long long
#define MAXCOMLEN 16
static int procDepth,taskDepth,threadDepth,splDepth,mode,procRefs,taskRefs=1,threadReads,threadDrops,listCalls;
static std::string output;
static int checked_printf(const char*format,...){
 assert(!procDepth&&!taskDepth&&!threadDepth&&!splDepth);
 char line[1024];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);output+=line;return n;
}
#define printf checked_printf
struct thread {uintptr_t kernel_stack=0;int refs=1;uint64_t wait_event=7;void(*continuation)()=nullptr;unsigned state=1;};
using thread_t=thread*;
struct task {bool active=true;unsigned thread_count=0;std::vector<thread_t>threads;};
using task_t=task*;
struct proc {unsigned p_stat=1,p_flag=0;int pid=1;};using proc_t=proc*;
using vm_offset_t=uintptr_t;
static uintptr_t kernel_stack_size=1024;
struct arm_kernel_saved_state_t{uint64_t fp,lr;};
struct thread_kernel_state{struct{arm_kernel_saved_state_t ss;}machine;};
alignas(16) static unsigned char kernelStack[1024];
#define thread_get_kernel_state(t) ((thread_kernel_state*)((t)->kernel_stack+kernel_stack_size-sizeof(thread_kernel_state)))
enum {TH_WAIT=1,TH_RUN=4};
static size_t ml_nofault_copy(uintptr_t src,uintptr_t dst,size_t n){assert(threadDepth&&splDepth&&!taskDepth&&!procDepth);memcpy((void*)dst,(void*)src,n);return n;}
static task activeTask;static proc process;
#define PROC_NULL nullptr
#define TASK_NULL nullptr
enum {PROC_RETURNED,PROC_RETURNED_DONE,PROC_ALLPROCLIST=1,PROC_NOWAITTRANS=2};
static proc_t proc_find(int pid){assert(pid==1);if(mode==1)return nullptr;procRefs++;return &process;}
static void proc_rele(proc_t p){assert(p==&process&&procRefs);procRefs--;}
static task_t proc_task(proc_t p){assert(p==&process&&procRefs);return mode==2?nullptr:&activeTask;}
static void task_reference(task_t t){assert(t==&activeTask);taskRefs++;}
static void task_deallocate(task_t t){assert(t==&activeTask&&taskRefs>1);taskRefs--;}
static int proc_pid(proc_t p){return p->pid;}
static int proc_ppid(proc_t){return 0;}
static void proc_name(int pid,char*name,int size){snprintf(name,size,"test-%d",pid);}
static void proc_lock(proc_t){assert(!procDepth++);}
static void proc_unlock(proc_t){assert(procDepth--==1);}
static void proc_iterate(unsigned flags,int(*callback)(proc_t,void*),void*arg,void*,void*){
 assert(flags==(PROC_ALLPROCLIST|PROC_NOWAITTRANS));
 for(int i=0;i<100;i++){listCalls++;int result=callback(&process,arg);assert(result==PROC_RETURNED||result==PROC_RETURNED_DONE);if(result==PROC_RETURNED_DONE)break;}
}
static void task_lock(task_t){assert(!taskDepth++);}
static void task_unlock(task_t){assert(taskDepth--==1);}
static void thread_reference(thread_t t){assert(taskDepth&&t->refs==1);t->refs++;}
static void thread_deallocate(thread_t t){assert(!taskDepth&&t->refs==2);t->refs--;threadDrops++;}
#define queue_iterate(head,var,type,field) for(unsigned qi=0;qi<(head)->size()&&((var)=(*(head))[qi],true);qi++)
using spl_t=int;
static spl_t splsched(){assert(!splDepth++);return 1;}
static void splx(spl_t s){assert(s==1&&splDepth--==1);}
static void thread_lock(thread_t){assert(splDepth&&!threadDepth++);}
static void thread_unlock(thread_t){assert(threadDepth--==1);}
static uint64_t thread_tid(thread_t){return 101;}
using kern_return_t=int;using mach_msg_type_number_t=unsigned;
struct timeval{int seconds,microseconds;};
struct thread_basic_info_data_t{int run_state=0,flags=0,suspend_count=0,sleep_time=0;timeval user_time{},system_time{};};
using thread_info_t=thread_basic_info_data_t*;
enum {THREAD_BASIC_INFO=1,THREAD_BASIC_INFO_COUNT=10};
static kern_return_t thread_info_internal(thread_t t,int flavor,thread_info_t info,mach_msg_type_number_t*count){
 assert(t->refs==2&&!taskDepth&&flavor==THREAD_BASIC_INFO&&*count==THREAD_BASIC_INFO_COUNT);threadReads++;
 if(mode==5)return 5;info->run_state=3;return 0;
}
'''.replace('struct timeval','struct test_timeval').replace(';timeval user_time',';test_timeval user_time')
main=r'''
int main(){
 thread store[40];for(auto&t:store)activeTask.threads.push_back(&t);activeTask.thread_count=40;store[0].kernel_stack=(uintptr_t)kernelStack;
 for(mode=1;mode<=5;mode++){
   output.clear();threadReads=threadDrops=0;activeTask.active=mode!=3;kebab_pid1_report();
   assert(procRefs==0&&taskRefs==1&&!procDepth&&!taskDepth&&!threadDepth&&!splDepth);
   for(auto&t:store)assert(t.refs==1);
   if(mode==1)assert(output.find("process absent")!=std::string::npos);
   if(mode==2)assert(output.find("task absent")!=std::string::npos);
   if(mode==3)assert(threadReads==0&&threadDrops==0);
   if(mode>=4)assert(threadReads==32&&threadDrops==32&&output.find("count=32 thread_total=40")!=std::string::npos);
 }
 mode=0;listCalls=0;kebab_process_report();assert(listCalls==64);
 fprintf(stdout,"actual PID1/proc snapshots: missing/inactive/task errors, cap32/64, all references and locks balanced, no printing under locks PASS\n");
}
'''
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('')
    p.write_text(prefix+strip(mach)+strip(bsd).replace('unsigned *count = arg;','unsigned *count = (unsigned *)arg;')+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

