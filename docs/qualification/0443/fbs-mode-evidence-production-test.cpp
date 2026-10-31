
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
struct proc{const char *p_comm;int pid;};using proc_t=proc*;using user_addr_t=uint64_t;
struct arm_thread_state64_t{uint64_t x[29],fp,lr,sp,pc;};using mach_msg_type_number_t=unsigned;using thread_state_t=unsigned*;using kern_return_t=int;
constexpr unsigned ARM_THREAD_STATE64_COUNT=68;constexpr int ARM_THREAD_STATE64=6,KERN_SUCCESS=0;
static proc ui{"SpringBoard",27},foreign{"SpringBoard",99},other{"other",57};static proc_t running=&ui;
static int scenario;static std::atomic<unsigned>state_calls{},copy_calls{};static std::mutex log_lock;static std::vector<std::string>logs;
[[maybe_unused]] static proc_t current_proc(){return running;}static int proc_getpid(proc_t p){return p->pid;}static int current_thread(){return 1234;}
static kern_return_t thread_get_state(int thread,int flavor,thread_state_t data,mach_msg_type_number_t *count){
 assert(thread==current_thread()&&flavor==ARM_THREAD_STATE64&&*count==ARM_THREAD_STATE64_COUNT);state_calls++;
 auto state=reinterpret_cast<arm_thread_state64_t*>(data);assert(!state->pc);
 state->pc=scenario==6?0x1234:0x19b91abd4ull;state->x[19]=0x100010000ull;
 if(scenario==4)return 5;if(scenario==5)*count=67;return 0;
}
static int copyin(user_addr_t object,void *out,size_t n){
 assert(object==0x100010000ull);copy_calls++;assert(n==8||n==73);
 uint64_t mode[10]={0x1a000000001ull|0x1e1dd3380ull,1125,2436,3,0,0x404e000000000000ull,1,2,0,1};
 if(scenario==7)return 14;
 if(scenario==8)mode[0]=0x1e1dd4400;
 if(scenario==9&&n==73)return 14;
 if(scenario==10&&n==73)mode[0]=0x1e1dd4400;
 if(scenario==11){mode[1]=0;mode[2]=~0ull;mode[3]=0;mode[5]=0;mode[7]=99;}
 memcpy(out,mode,n);return 0;
}
static void event_print(const char *fmt,...){
 char line[1200];va_list ap;va_start(ap,fmt);int n=vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);assert(n>0&&n<int(sizeof(line)));
 std::lock_guard<std::mutex>guard(log_lock);logs.emplace_back(line);
}
#define printf event_print
static void
kebab_fbs_mode_failure_evidence(proc_t p, int rv)
{
    if (p != current_proc() || !WTERMSIG(rv) ||
        (strcmp(p->p_comm, "SpringBoard") && strcmp(p->p_comm, "PreBoard"))) return;
    static unsigned reported;
    unsigned slot = __atomic_load_n(&reported, __ATOMIC_RELAXED);
    if (slot >= 8) return;
    arm_thread_state64_t saved = {};
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    kern_return_t result = thread_get_state(current_thread(), ARM_THREAD_STATE64,
        (thread_state_t)&saved, &count);
    if (result != KERN_SUCCESS || count != ARM_THREAD_STATE64_COUNT ||
        saved.pc != 0x19b91abd4ULL) return;
    do {
        if (slot >= 8) return;
    } while (!__atomic_compare_exchange_n(&reported, &slot, slot + 1, false,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    uint64_t isa = 0, mode[10] = {};
    user_addr_t object = saved.x[19];
    int error = copyin(object, &isa, sizeof(isa));
    if (error || (isa & 0x0000000ffffffff8ULL) != 0x1e1dd3380ULL) {
        printf("KEBAB FBS MODE: name=%s pid=%d event=%u object-read=%d class-match=0; payload not read\n",
            p->p_comm, proc_getpid(p), slot + 1, error);
        return;
    }
    error = copyin(object, mode, 73); /* Exact original instanceSize, not allocation padding. */
    if (error) {
        printf("KEBAB FBS MODE: name=%s pid=%d event=%u mode-read=%d; original exit preserved\n",
            p->p_comm, proc_getpid(p), slot + 1, error);
        return;
    }
    if ((mode[0] & 0x0000000ffffffff8ULL) != 0x1e1dd3380ULL) {
        printf("KEBAB FBS MODE: name=%s pid=%d event=%u class changed; payload not logged\n",
            p->p_comm, proc_getpid(p), slot + 1);
        return;
    }
    printf("KEBAB FBS MODE: name=%s pid=%d event=%u width=%llu height=%llu preferred-scale=%llu override-bits=0x%llx refresh-bits=0x%llx gamut=%llu hdr=%llu rotation=%llu virtual=%u; constructor object, original exit preserved\n",
        p->p_comm, proc_getpid(p), slot + 1, (unsigned long long)mode[1],
        (unsigned long long)mode[2], (unsigned long long)mode[3],
        (unsigned long long)mode[4], (unsigned long long)mode[5],
        (unsigned long long)mode[6], (unsigned long long)mode[7],
        (unsigned long long)mode[8], (unsigned)(mode[9] & 255));
}


#undef printf
static void run_case(int which){
 scenario=which;int signal=9;proc_t target=&ui;
 if(which==1)target=&foreign;if(which==2)signal=0;if(which==3){running=&other;target=&other;}
 if(which==12){
  std::vector<std::thread>workers;for(unsigned i=0;i<8;i++)workers.emplace_back([]{for(unsigned j=0;j<128;j++)kebab_fbs_mode_failure_evidence(&ui,9);});for(auto &t:workers)t.join();
  assert(logs.size()==8&&copy_calls==16&&state_calls>=8);return;
 }
 kebab_fbs_mode_failure_evidence(target,signal);
 if(which>=1&&which<=3){assert(logs.empty()&&!state_calls&&!copy_calls);return;}
 if(which>=4&&which<=6){assert(logs.empty()&&state_calls==1&&!copy_calls);return;}
 assert(logs.size()==1&&state_calls==1);
 if(which==7||which==8){assert(copy_calls==1&&logs[0].find("payload not read")!=std::string::npos);return;}
 assert(copy_calls==2);
 if(which==9){assert(logs[0].find("mode-read=14")!=std::string::npos);return;}
 if(which==10){assert(logs[0].find("class changed; payload not logged")!=std::string::npos);return;}
 assert(logs[0].find("constructor object, original exit preserved")!=std::string::npos);
 if(which==11){assert(logs[0].find("width=0 height=18446744073709551615 preferred-scale=0")!=std::string::npos&&logs[0].find("refresh-bits=0x0")!=std::string::npos&&logs[0].find("hdr=99")!=std::string::npos);return;}
 assert(logs[0].find("width=1125 height=2436 preferred-scale=3 override-bits=0x0 refresh-bits=0x404e000000000000 gamut=1 hdr=2 rotation=0 virtual=1")!=std::string::npos);
}
int main(){
 // Each child starts with a fresh per-boot diagnostic cap, not a reset API.
 for(int i=0;i<=12;i++){pid_t pid=fork();assert(pid>=0);if(!pid){run_case(i);_exit(0);}int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
 puts("PASS13isolated cases: current-task/signal/name/PC/Machstate/type/73Bbounds/readfail/changeguards, unchanged invalid values and concurrent8event cap; proc/Mach/copyin are explicit models");
}
