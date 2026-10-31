
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <thread>
#include <vector>
#include <array>
using u_int=unsigned;using int32_t=std::int32_t;
#define KEBAB 1
#define __unused
#define AUDIT_ARG(...) ((void)0)
static constexpr int SIGKILL=9,NSIG=32,MAXCOMLEN=16;
struct proc{int pid;const char *name;std::atomic<int>refs{1};};using proc_t=proc *;using kauth_cred_t=int;
struct kill_args{int pid,signum,posix;};
static proc sender{51,"runningboardd"},springboard{27,"SpringBoard"},preboard{131,"PreBoard"},other{85,"other"},zombie{888,"SpringBoard"};
static bool allow=true;static std::atomic<unsigned>signals{},finds{},releases{},checks{},groups{};
static std::array<int,4>groupArguments{};
static kauth_cred_t kauth_cred_get(){return 1;}
static proc_t lookup(int pid){for(auto p:{&sender,&springboard,&preboard,&other,&zombie})if(p->pid==pid)return p;return nullptr;}
static proc_t proc_find(int pid){finds++;auto p=lookup(pid);if(p&&p!=&zombie){p->refs++;return p;}return nullptr;}
static proc_t pzfind(int pid){return pid==zombie.pid?&zombie:nullptr;}
static void proc_rele(proc_t p){assert(p&&p->refs>1);p->refs--;releases++;}
static bool cansignal(proc_t cp,kauth_cred_t c,proc_t p,int){assert(cp==&sender&&c==1&&p->refs>1);checks++;return allow;}
static void psignal(proc_t p,int signum){assert(p&&p->refs>1&&signum>0&&signum<NSIG);signals++;}
static int killpg1(proc_t cp,int sig,int pg,int all,int posix){assert(cp==&sender);groups++;groupArguments={sig,pg,all,posix};return 77;}
static int proc_pid(proc_t p){assert(p);return p->pid;}
static void proc_name(int pid,char *to,unsigned n){auto p=lookup(pid);assert(p);assert(p==&sender||p->refs>1);snprintf(to,n,"%s",p->name);}
struct Event{unsigned slot;int from,to,signal;};static std::mutex eventLock;static std::vector<Event>events;
static void event_print(const char *format,unsigned slot,const char *from,int frompid,const char *to,int topid,int sig){
 assert(strstr(format,"permission passed, delivery not proven"));assert(!strcmp(from,sender.name));assert(topid==springboard.pid||topid==preboard.pid);assert(!strcmp(to,lookup(topid)->name));assert(lookup(topid)->refs>1);
 std::lock_guard<std::mutex>l(eventLock);events.push_back({slot,frompid,topid,sig});
}
#define printf event_print
static void
kebab_ui_kill_request(proc_t sender, proc_t target, int signum)
{
    if (signum != SIGKILL) return;
    char target_name[MAXCOMLEN + 1] = {};
    proc_name(proc_pid(target), target_name, sizeof(target_name));
    if (strcmp(target_name, "SpringBoard") && strcmp(target_name, "PreBoard")) return;
    static unsigned reported;
    unsigned slot = __atomic_load_n(&reported, __ATOMIC_RELAXED);
    do {
        if (slot >= 16) return;
    } while (!__atomic_compare_exchange_n(&reported, &slot, slot + 1, false,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    char sender_name[MAXCOMLEN + 1] = {};
    proc_name(proc_pid(sender), sender_name, sizeof(sender_name));
    printf("KEBAB UI KILL: accepted kill(2) request=%u sender=%s pid=%d target=%s pid=%d signal=%d; permission passed, delivery not proven\n",
        slot + 1, sender_name, proc_pid(sender), target_name, proc_pid(target), signum);
}
int
kill_before(proc_t cp, struct kill_args *uap, __unused int32_t *retval)
{
	proc_t p;
	kauth_cred_t uc = kauth_cred_get();
	int posix = uap->posix;         /* !0 if posix behaviour desired */

	AUDIT_ARG(pid, uap->pid);
	AUDIT_ARG(signum, uap->signum);

	if ((u_int)uap->signum >= NSIG) {
		return EINVAL;
	}
	if (uap->pid > 0) {
		/* kill single process */
		if ((p = proc_find(uap->pid)) == NULL) {
			if ((p = pzfind(uap->pid)) != NULL) {
				/*
				 * POSIX 1003.1-2001 requires returning success when killing a
				 * zombie; see Rationale for kill(2).
				 */
				return 0;
			}
			return ESRCH;
		}
		AUDIT_ARG(process, p);
		if (!cansignal(cp, uc, p, uap->signum)) {
			proc_rele(p);
			return EPERM;
		}
		if (uap->signum) {
			psignal(p, uap->signum);
		}
		proc_rele(p);
		return 0;
	}
	switch (uap->pid) {
	case -1: /* broadcast signal */
		return killpg1(cp, uap->signum, 0, 1, posix);
	case 0: /* signal own process group */
		return killpg1(cp, uap->signum, 0, 0, posix);
	default: /* negative explicit process group */
		return killpg1(cp, uap->signum, -(uap->pid), 0, posix);
	}
	/* NOTREACHED */
}

int
kill_after(proc_t cp, struct kill_args *uap, __unused int32_t *retval)
{
	proc_t p;
	kauth_cred_t uc = kauth_cred_get();
	int posix = uap->posix;         /* !0 if posix behaviour desired */

	AUDIT_ARG(pid, uap->pid);
	AUDIT_ARG(signum, uap->signum);

	if ((u_int)uap->signum >= NSIG) {
		return EINVAL;
	}
	if (uap->pid > 0) {
		/* kill single process */
		if ((p = proc_find(uap->pid)) == NULL) {
			if ((p = pzfind(uap->pid)) != NULL) {
				/*
				 * POSIX 1003.1-2001 requires returning success when killing a
				 * zombie; see Rationale for kill(2).
				 */
				return 0;
			}
			return ESRCH;
		}
		AUDIT_ARG(process, p);
		if (!cansignal(cp, uc, p, uap->signum)) {
			proc_rele(p);
			return EPERM;
		}
		if (uap->signum) {
#if defined(KEBAB)
			kebab_ui_kill_request(cp, p, uap->signum);
#endif
			psignal(p, uap->signum);
		}
		proc_rele(p);
		return 0;
	}
	switch (uap->pid) {
	case -1: /* broadcast signal */
		return killpg1(cp, uap->signum, 0, 1, posix);
	case 0: /* signal own process group */
		return killpg1(cp, uap->signum, 0, 0, posix);
	default: /* negative explicit process group */
		return killpg1(cp, uap->signum, -(uap->pid), 0, posix);
	}
	/* NOTREACHED */
}

#undef printf
static void balance(){for(auto p:{&sender,&springboard,&preboard,&other,&zombie})assert(p->refs==1);}
static auto snapshot(){return std::array<unsigned,5>{signals.load(),finds.load(),releases.load(),checks.load(),groups.load()};}
static void reset(){signals=finds=releases=checks=groups=0;groupArguments={};balance();}
int main(){
 int32_t value=123;kill_args k{27,SIGKILL,1};allow=false;assert(kill_after(&sender,&k,&value)==EPERM&&events.empty()&&!signals);balance();
 allow=true;assert(!kill_after(&sender,&k,&value)&&signals==1&&events.size()==1&&events[0].from==51&&events[0].to==27&&events[0].signal==9&&value==123);balance();
 k.pid=131;assert(!kill_after(&sender,&k,&value)&&signals==2&&events.size()==2);balance();
 k.pid=85;assert(!kill_after(&sender,&k,&value)&&signals==3&&events.size()==2);balance();
 // Compare complete original/current syscall behavior and all delegated groups.
 for(bool permission:{false,true})for(int pid:{27,131,85,999,888,0,-1,-7})for(int sig=-2;sig<=NSIG+2;sig++){
  allow=permission;kill_args a{pid,sig,1};reset();int expected=kill_before(&sender,&a,&value);auto counters=snapshot();auto args=groupArguments;balance();
  reset();int actual=kill_after(&sender,&a,&value);assert(actual==expected&&snapshot()==counters&&groupArguments==args&&value==123);balance();
 }
 allow=true;reset();std::vector<std::thread>workers;
 for(unsigned i=0;i<8;i++)workers.emplace_back([&]{kill_args a{27,9,1};for(unsigned j=0;j<128;j++){int32_t rv=17;assert(!kill_after(&sender,&a,&rv)&&rv==17);}});
 for(auto&t:workers)t.join();balance();assert(signals==1024&&events.size()==16);
 unsigned seen=0;for(auto e:events){assert(e.slot>=1&&e.slot<=16&&e.from==51&&e.signal==9);unsigned bit=1u<<(e.slot-1);assert(!(seen&bit));seen|=bit;}assert(seen==0xffff);
 for(unsigned i=0;i<32;i++){kill_args a{131,9,1};assert(!kill_after(&sender,&a,&value));}balance();assert(events.size()==16&&signals==1056);
 puts("PASS complete original-vs-current kill(2):592permission/signal/PID cases, reference/group/return preservation, selected accepted UI requests and concurrent16event cap; signal/credential/proc objects explicitly modeled");
}
