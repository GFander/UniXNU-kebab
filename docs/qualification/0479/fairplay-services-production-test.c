
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#define SYS_ptrace 26
#define P_LTRACED 1u
#define P_LNOATTACH 2u
#define LCK_GRP_DECLARE(var,name) int var
#define LCK_SPIN_DECLARE(var,grp) pthread_mutex_t var=PTHREAD_MUTEX_INITIALIZER
struct Proc{char p_comm[20];unsigned uid;pid_t pid;unsigned char uuid[16];uint32_t p_lflag;pthread_mutex_t lock;};typedef struct Proc*proc_t;
static struct Proc original={.p_comm="fairplayd.H2",.uid=501,.pid=87,.uuid={0x30,0x68,0x88,0xad,0xfb,0xc4,0x39,0xc4,0x96,0x1c,0x72,0x1c,0x7f,0xf2,0x30,0xd1},.lock=PTHREAD_MUTEX_INITIALIZER};
static _Thread_local proc_t self;
static _Thread_local unsigned proc_depth,spin_depth;
static _Thread_local uint64_t tid=1;
static unsigned debugger,prints,cases;
static char line[224],output[16384];
static proc_t current_proc(void){return self;}
static pid_t proc_pid(proc_t p){return p->pid;}
static unsigned proc_getuid(proc_t p){assert(!spin_depth);return p->uid;}
static void proc_getexecutableuuid(proc_t p,unsigned char*out,unsigned long n){assert(n==16&&!spin_depth&&!proc_depth);memcpy(out,p->uuid,n);}
static void proc_lock(proc_t p){assert(!spin_depth&&!proc_depth);assert(pthread_mutex_lock(&p->lock)==0);proc_depth++;}
static void proc_unlock(proc_t p){assert(proc_depth==1&&!spin_depth);proc_depth--;assert(pthread_mutex_unlock(&p->lock)==0);}
static void lck_spin_lock(pthread_mutex_t*p){assert(!spin_depth&&!proc_depth);assert(pthread_mutex_lock(p)==0);spin_depth++;}
static void lck_spin_unlock(pthread_mutex_t*p){assert(spin_depth==1&&!proc_depth);spin_depth--;assert(pthread_mutex_unlock(p)==0);}
static void*current_thread(void){return (void*)(uintptr_t)tid;}
static uint64_t thread_tid(void*t){return(uintptr_t)t;}
static unsigned PE_i_can_has_debugger(void*arg){assert(arg==NULL&&!spin_depth&&!proc_depth);return debugger;}
static int observed_printf(const char*fmt,...){assert(!spin_depth&&!proc_depth);prints++;va_list args;va_start(args,fmt);int n=vsnprintf(line,sizeof(line),fmt,args);va_end(args);assert(n>0&&(unsigned)n<sizeof(line)&&line[n-1]=='\n');assert(strlen(output)+(size_t)n<sizeof(output));strcat(output,line);return n;}
#define printf observed_printf

#include <errno.h>
typedef uintptr_t user_addr_t;
#define SYS_open 5
#define SYS_openat 463
static unsigned copy_calls;
static const char *model_path;
static int model_copy_error;
static int copyinstr(user_addr_t address,void *out,size_t n,size_t *done){
 assert(!spin_depth&&!proc_depth&&address==1);copy_calls++;
 if(model_copy_error){memcpy(out,"PARTIAL_PRIVATE",15);*done=SIZE_MAX;return model_copy_error;}
 size_t length=strlen(model_path)+1;assert(length<=n);memcpy(out,model_path,length);*done=length;return 0;
}
/* Read-only, first exact original fairplayd.H2 process only. This ring
 * records completed BSD call metadata, never argument buffers or results.
 * Proc identity/flags and the ring are separate non-atomic observations.
 * No printing, proc lock, user access or sleeping under our ring spinlock.
 * Original syscall/ptrace/MACF/exit behavior is unchanged. */
#define KEBAB_FP_CALLS 32
struct kebab_fp_call {
 uint64_t sequence,tid,pc,lr;
 uint32_t code,flags,debugger;
 int32_t error,request;
};
static LCK_GRP_DECLARE(kebab_fp_group,"kebab-fairplay-evidence");
static LCK_SPIN_DECLARE(kebab_fp_lock,&kebab_fp_group);
static pid_t kebab_fp_pid;
static uint64_t kebab_fp_total;
static bool kebab_fp_sealed;
static struct kebab_fp_call kebab_fp_calls[KEBAB_FP_CALLS],kebab_fp_first_deny;

static bool
kebab_fp_original_self(proc_t p)
{
 static const unsigned char expected[16]={0x30,0x68,0x88,0xad,0xfb,0xc4,0x39,0xc4,0x96,0x1c,0x72,0x1c,0x7f,0xf2,0x30,0xd1};
 unsigned char uuid[16]={};
 if(p!=current_proc()||strcmp(p->p_comm,"fairplayd.H2")||proc_getuid(p)!=501)return false;
 proc_getexecutableuuid(p,uuid,sizeof(uuid));
 return memcmp(uuid,expected,sizeof(uuid))==0;
}

void
kebab_fairplay_syscall_return_evidence(proc_t p,unsigned short code,int error,uint64_t pc,uint64_t lr,int32_t request)
{
 if(!kebab_fp_original_self(p))return;
 pid_t pid=proc_pid(p);uint32_t flags;
 proc_lock(p);flags=((p->p_lflag&P_LTRACED)?1u:0u)|((p->p_lflag&P_LNOATTACH)?2u:0u);proc_unlock(p);
 struct kebab_fp_call event={.tid=thread_tid(current_thread()),.pc=pc,.lr=lr,
     .code=code,.flags=flags,.debugger=PE_i_can_has_debugger(NULL)!=0,.error=error,
     .request=code==SYS_ptrace?request:-1};
 lck_spin_lock(&kebab_fp_lock);
 if(kebab_fp_pid==0)kebab_fp_pid=pid;
 if(kebab_fp_pid!=pid||kebab_fp_sealed||kebab_fp_total==~(uint64_t)0){
  lck_spin_unlock(&kebab_fp_lock);return;
 }
 event.sequence=++kebab_fp_total;
 kebab_fp_calls[(event.sequence-1)%KEBAB_FP_CALLS]=event;
 if(code==SYS_ptrace&&request==31&&kebab_fp_first_deny.sequence==0)kebab_fp_first_deny=event;
 lck_spin_unlock(&kebab_fp_lock);
}

static void
kebab_fp_print_call(const char *kind,pid_t pid,const struct kebab_fp_call *event)
{
 printf("KEBAB FP-%s: n=%llu pid=%d tid=%llu call=%u error=%d pc=%llx lr=%llx req=%d flags=%x dbg=%u\n",
     kind,(unsigned long long)event->sequence,pid,(unsigned long long)event->tid,event->code,event->error,
     (unsigned long long)event->pc,(unsigned long long)event->lr,event->request,event->flags,event->debugger);
}

__attribute__((noinline)) static void
kebab_fairplay_exit_evidence(proc_t p,int rv)
{
 if(!kebab_fp_original_self(p))return;
 struct kebab_fp_call snapshot[KEBAB_FP_CALLS],deny;uint64_t total;unsigned kept;pid_t pid=proc_pid(p);
 lck_spin_lock(&kebab_fp_lock);
 if(kebab_fp_pid!=pid||kebab_fp_sealed){lck_spin_unlock(&kebab_fp_lock);return;}
 kebab_fp_sealed=true;total=kebab_fp_total;kept=total<KEBAB_FP_CALLS?(unsigned)total:KEBAB_FP_CALLS;
 uint64_t first=total-kept+1;
 for(unsigned i=0;i<kept;i++)snapshot[i]=kebab_fp_calls[(first+i-1)%KEBAB_FP_CALLS];
 deny=kebab_fp_first_deny;lck_spin_unlock(&kebab_fp_lock);
 printf("KEBAB FP-BEGIN: pid=%d total=%llu kept=%u first=%llu deny-seen=%u\n",pid,(unsigned long long)total,kept,(unsigned long long)first,deny.sequence!=0);
 if(deny.sequence)kebab_fp_print_call("DENY",pid,&deny);
 for(unsigned i=0;i<kept;i++)kebab_fp_print_call("SC",pid,&snapshot[i]);
 printf("KEBAB FP-END: pid=%d signal=%d status=%d; original exit preserved\n",pid,WTERMSIG(rv),WEXITSTATUS(rv));
}

/* Bounded name-only observations. User paths are post-return best-effort
 * snapshots; registry names are the live MIG request, never property values.
 * Reserve under the ring lock; copy/sanitize/print only after unlocking. */
static unsigned kebab_fp_resource_count;

static unsigned
kebab_fp_resource_reserve(proc_t p)
{
 if(!kebab_fp_original_self(p))return 0;
 pid_t pid=proc_pid(p);unsigned ticket=0;
 lck_spin_lock(&kebab_fp_lock);
 if(kebab_fp_pid==0)kebab_fp_pid=pid;
 if(kebab_fp_pid==pid&&!kebab_fp_sealed&&kebab_fp_resource_count<32)
  ticket=++kebab_fp_resource_count;
 lck_spin_unlock(&kebab_fp_lock);
 return ticket;
}

static void
kebab_fp_resource_print(proc_t p,unsigned ticket,unsigned kind,unsigned code,
    int result,int aux,char *name,unsigned copied,int copy_error,bool truncated)
{
 unsigned changed=0;
 for(unsigned i=0;i<copied;i++){
  unsigned char c=(unsigned char)name[i];
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='.'||c=='_'||c=='-'||c==':')){
   name[i]='?';changed=1;
  }
 }
 name[copied]=0;
 printf("KEBAB FP-RESOURCE: id=%u pid=%d kind=%u code=%u result=%d aux=%d copy=%d trunc=%u changed=%u\n",
     ticket,proc_pid(p),kind,code,result,aux,copy_error,truncated,changed);
 printf("KEBAB FP-NAME: id=%u name=%s\n",ticket,name);
}

void
kebab_fairplay_open_return_evidence(proc_t p,unsigned short code,int error,uint64_t address,int32_t dirfd)
{
 if(error!=ENOENT||(code!=SYS_open&&code!=SYS_openat))return;
 unsigned ticket=kebab_fp_resource_reserve(p);if(!ticket)return;
 char name[128]={};size_t copied=0;
 int copied_error=copyinstr((user_addr_t)address,name,sizeof(name),&copied);
 bool truncated=copied_error==ENAMETOOLONG;
 /* copyinstr's done count is valid on success, not on error. Do not print
  * partially copied user data on EFAULT or an overlong path. */
 unsigned length=0;
 if(!copied_error&&copied>0&&copied<=sizeof(name))length=(unsigned)copied-1;
 else name[0]=0;
 kebab_fp_resource_print(p,ticket,0,code,error,code==SYS_openat?dirfd:0,name,length,copied_error,truncated);
}

void
kebab_fairplay_registry_evidence(unsigned kind,const char *name,unsigned bound,int result,unsigned found)
{
 if((kind!=1&&kind!=2)||!name||!bound)return;
 proc_t p=current_proc();unsigned ticket=kebab_fp_resource_reserve(p);if(!ticket)return;
 char snapshot[128]={};unsigned length=0;
 while(length<sizeof(snapshot)-1&&length<bound&&name[length]){
  snapshot[length]=name[length];length++;
 }
 bool truncated=length==bound||(length==sizeof(snapshot)-1&&name[length]!=0);
 kebab_fp_resource_print(p,ticket,kind,0,result,found!=0,snapshot,length,0,truncated);
}

/* Only the exact original process may expose protocol/class-name metadata.
 * This eligibility snapshot does not reserve a ticket; reserve rechecks it. */
bool
kebab_fairplay_resource_observer_enabled(void)
{
 proc_t p=current_proc();if(!kebab_fp_original_self(p))return false;
 pid_t pid=proc_pid(p);bool enabled;
 lck_spin_lock(&kebab_fp_lock);
 enabled=(kebab_fp_pid==0||kebab_fp_pid==pid)&&!kebab_fp_sealed&&kebab_fp_resource_count<32;
 lck_spin_unlock(&kebab_fp_lock);
 return enabled;
}

/* kind3: singular matching result, aux=service present.
 * kind4: open result, aux=original connect_type (not a key selector).
 * kind5: plural matching result, aux=iterator created, NOT match count.
 * kind6: iterator-next result, aux=object present. No extra iteration. */
void
kebab_fairplay_service_evidence(unsigned kind,const char *name,unsigned bound,int result,int aux)
{
 if(kind<3||kind>6||!name||!bound)return;
 proc_t p=current_proc();unsigned ticket=kebab_fp_resource_reserve(p);if(!ticket)return;
 char snapshot[128]={};size_t available=strnlen(name,bound);
 unsigned length=available<sizeof(snapshot)?(unsigned)available:(unsigned)sizeof(snapshot)-1;
 memcpy(snapshot,name,length);
 kebab_fp_resource_print(p,ticket,kind,0,result,aux,snapshot,length,0,available>=sizeof(snapshot)||available==bound);
}

#undef printf
static void reset(void){assert(!spin_depth&&!proc_depth);kebab_fp_resource_count=0;copy_calls=0;model_path="/var/test/name";model_copy_error=0;kebab_fp_pid=0;kebab_fp_total=0;kebab_fp_sealed=false;memset(kebab_fp_calls,0,sizeof(kebab_fp_calls));memset(&kebab_fp_first_deny,0,sizeof(kebab_fp_first_deny));original.p_lflag=0;debugger=prints=0;line[0]=output[0]=0;self=&original;tid=1;}
static void call(unsigned short n,int error,int32_t req){uint32_t flags=original.p_lflag;unsigned debug=debugger;kebab_fairplay_syscall_return_evidence(self,n,error,0x1009779bc,0x1009779bc,req);assert(original.p_lflag==flags&&debugger==debug&&!spin_depth&&!proc_depth);}
static unsigned occurrences(const char*text){unsigned n=0;for(char*p=output;(p=strstr(p,text));p++)n++;return n;}
static void*writer(void*arg){tid=(uintptr_t)arg+2;self=&original;for(unsigned i=0;i<100;i++)call((unsigned short)(100+tid),i&1,-1);return NULL;}
int main(void){
 (void)kebab_fp_group;
 for(unsigned n=0;n<4;n++){
  reset();struct Proc bad={.p_comm="fairplayd.H2",.uid=501,.pid=87,.lock=PTHREAD_MUTEX_INITIALIZER};memcpy(bad.uuid,original.uuid,16);
  if(n==0)strcpy(bad.p_comm,"securityd");if(n==1)bad.uid=0;if(n==2)bad.uuid[15]^=1;
  self=n==3?&original:&bad;proc_t p=n==3?&bad:self;
  kebab_fairplay_syscall_return_evidence(p,26,0,1,2,31);kebab_fairplay_exit_evidence(p,0);
  assert(!kebab_fp_pid&&!kebab_fp_total&&!kebab_fp_sealed&&!prints);pthread_mutex_destroy(&bad.lock);cases++;
 }
 reset();kebab_fairplay_exit_evidence(&original,0);assert(!prints&&!kebab_fp_sealed);cases++;
 for(unsigned flags=0;flags<4;flags++)for(unsigned debug=0;debug<2;debug++){
  reset();original.p_lflag=flags;debugger=debug;call(26,0,31);assert(!prints&&kebab_fp_total==1&&kebab_fp_first_deny.sequence==1);
  assert(kebab_fp_calls[0].flags==flags&&kebab_fp_calls[0].debugger==debug&&kebab_fp_calls[0].error==0&&kebab_fp_calls[0].request==31);
  kebab_fairplay_exit_evidence(&original,0);assert(kebab_fp_sealed&&occurrences("KEBAB FP-SC:")==1&&occurrences("KEBAB FP-DENY:")==1&&occurrences("KEBAB FP-END:")==1&&strstr(output,"signal=0 status=0; original exit preserved"));cases++;
 }
 reset();call(5,13,0x12345678);assert(kebab_fp_calls[0].error==13&&kebab_fp_calls[0].request==-1);call(26,1,30);assert(kebab_fp_first_deny.sequence==0);call(26,0,31);call(26,0,31);assert(kebab_fp_first_deny.sequence==3);cases++;
 for(unsigned length=1;length<=96;length++){
  reset();call(26,0,31);for(unsigned i=1;i<length;i++)call((unsigned short)(100+i),i%5,-1);
  assert(kebab_fp_total==length&&!prints);kebab_fairplay_exit_evidence(&original,W_EXITCODE(7,0));
  unsigned kept=length<32?length:32;unsigned first=length-kept+1;assert(occurrences("KEBAB FP-SC:")==kept&&occurrences("KEBAB FP-DENY:")==1&&prints==kept+3);
  char expected[80];snprintf(expected,sizeof(expected),"total=%u kept=%u first=%u ",length,kept,first);assert(strstr(output,expected)&&strstr(output,"signal=0 status=7; original exit preserved"));
  for(unsigned i=0;i<kept;i++){snprintf(expected,sizeof(expected),"KEBAB FP-SC: n=%u ",first+i);assert(strstr(output,expected));}
  unsigned before=prints;call(202,0,-1);kebab_fairplay_exit_evidence(&original,0);assert(kebab_fp_total==length&&prints==before);cases++;
 }
 reset();call(26,0,31);struct Proc other={.p_comm="fairplayd.H2",.uid=501,.pid=88,.lock=PTHREAD_MUTEX_INITIALIZER};memcpy(other.uuid,original.uuid,16);self=&other;call(202,0,-1);kebab_fairplay_exit_evidence(&other,0);assert(kebab_fp_pid==87&&kebab_fp_total==1&&!prints&&!kebab_fp_sealed);self=&original;kebab_fairplay_exit_evidence(&original,0);assert(kebab_fp_sealed);pthread_mutex_destroy(&other.lock);cases++;
 reset();call(26,0,31);self=&original;pthread_t workers[8];for(unsigned i=0;i<8;i++)assert(pthread_create(&workers[i],NULL,writer,(void*)(uintptr_t)i)==0);for(unsigned i=0;i<8;i++)assert(pthread_join(workers[i],NULL)==0);assert(kebab_fp_total==801&&!prints);kebab_fairplay_exit_evidence(&original,0);assert(occurrences("KEBAB FP-SC:")==32&&strstr(output,"total=801 kept=32 first=770 "));
 for(unsigned n=770;n<=801;n++){char expected[64];snprintf(expected,sizeof(expected),"KEBAB FP-SC: n=%u ",n);assert(strstr(output,expected));}cases++;
 reset();call(26,0,31);kebab_fp_total=UINT64_MAX;call(5,0,-1);assert(kebab_fp_total==UINT64_MAX);cases++;
 reset();struct kebab_fp_call max={.sequence=UINT64_MAX,.tid=UINT64_MAX,.pc=UINT64_MAX,.lr=UINT64_MAX,.code=UINT16_MAX,.flags=3,.debugger=1,.error=INT32_MIN,.request=INT32_MIN};kebab_fp_print_call("DENY",INT32_MAX,&max);assert(prints==1&&strstr(output,"n=18446744073709551615 ")&&strstr(output,"error=-2147483648 "));cases++;

 reset();kebab_fairplay_open_return_evidence(self,5,0,1,0);kebab_fairplay_open_return_evidence(self,463,EACCES,1,-2);kebab_fairplay_open_return_evidence(self,202,ENOENT,1,0);assert(!copy_calls&&!prints&&!kebab_fp_resource_count);cases++;
 reset();original.uid=0;kebab_fairplay_open_return_evidence(self,5,ENOENT,1,0);kebab_fairplay_registry_evidence(1,"IOService:/test",16,0,1);assert(!copy_calls&&!prints);original.uid=501;cases++;
 reset();kebab_fairplay_open_return_evidence(self,5,ENOENT,1,0);assert(copy_calls==1&&kebab_fp_resource_count==1&&prints==2&&strstr(output,"kind=0 code=5 result=2 aux=0 copy=0 trunc=0 changed=0")&&strstr(output,"name=/var/test/name\n"));cases++;
 reset();kebab_fairplay_open_return_evidence(self,463,ENOENT,1,-2);assert(copy_calls==1&&strstr(output,"code=463 result=2 aux=-2"));cases++;
 for(unsigned i=0;i<2;i++){reset();model_copy_error=i?ENAMETOOLONG:EFAULT;kebab_fairplay_open_return_evidence(self,5,ENOENT,1,0);assert(copy_calls==1&&prints==2&&!strstr(output,"PARTIAL_PRIVATE")&&strstr(output,"name=\n"));if(i)assert(strstr(output,"trunc=1"));cases++;}
 reset();model_path="/var/line\n\r\t%s\\test";kebab_fairplay_open_return_evidence(self,5,ENOENT,1,0);assert(strstr(output,"changed=1")&&strstr(output,"name=/var/line????s?test\n")&&!strstr(output,"\r"));cases++;
 for(unsigned length=0;length<=130;length++){
  reset();char name[132];memset(name,'a',length);name[length]=0;kebab_fairplay_registry_evidence(1,name,length+1,0,1);assert(prints==2&&!copy_calls);assert((strstr(output,"trunc=1")!=NULL)==(length>=128));assert(strstr(output,"kind=1 code=0 result=0 aux=1"));cases++;
 }
 reset();char exact[4]={'a','b','c','d'};kebab_fairplay_registry_evidence(2,exact,sizeof(exact),-123,0);assert(prints==2&&strstr(output,"name=abcd\n")&&strstr(output,"result=-123 aux=0 copy=0 trunc=1"));cases++;
 reset();kebab_fairplay_registry_evidence(3,"test",5,0,1);kebab_fairplay_registry_evidence(1,NULL,8,0,0);kebab_fairplay_registry_evidence(1,"test",0,0,0);assert(!prints&&!kebab_fp_resource_count);cases++;
 reset();for(unsigned i=0;i<40;i++)kebab_fairplay_registry_evidence(2,"missing-key",12,-1,0);assert(prints==64&&kebab_fp_resource_count==32);cases++;
 reset();call(26,0,31);kebab_fairplay_exit_evidence(self,0);unsigned was=prints;kebab_fairplay_open_return_evidence(self,5,ENOENT,1,0);kebab_fairplay_registry_evidence(1,"test",5,0,0);assert(!copy_calls&&prints==was&&!kebab_fp_resource_count);cases++;
 reset();call(26,0,31);struct Proc second={.p_comm="fairplayd.H2",.uid=501,.pid=88,.lock=PTHREAD_MUTEX_INITIALIZER};memcpy(second.uuid,original.uuid,16);self=&second;kebab_fairplay_registry_evidence(1,"test",5,0,0);assert(!prints&&!kebab_fp_resource_count);pthread_mutex_destroy(&second.lock);cases++;

 reset();assert(kebab_fairplay_resource_observer_enabled());assert(!kebab_fp_pid&&!kebab_fp_resource_count);cases++;
 reset();original.uid=0;assert(!kebab_fairplay_resource_observer_enabled());kebab_fairplay_service_evidence(3,(const char*)1,128,0,0);assert(!prints&&!kebab_fp_resource_count);original.uid=501;cases++;
 reset();call(26,0,31);kebab_fairplay_exit_evidence(self,0);assert(!kebab_fairplay_resource_observer_enabled());unsigned prior=prints;kebab_fairplay_service_evidence(4,(const char*)1,128,0,0);assert(prints==prior);cases++;
 for(unsigned kind=3;kind<=6;kind++)for(unsigned length=0;length<=130;length++){
  reset();char name[132];memset(name,'a',length);name[length]=0;kebab_fairplay_service_evidence(kind,name,length+1,-123,INT32_MIN);assert(prints==2&&kebab_fp_resource_count==1&&!copy_calls);assert((strstr(output,"trunc=1")!=NULL)==(length>=128));assert(strstr(output,"result=-123 aux=-2147483648"));char expect[40];snprintf(expect,sizeof(expect),"kind=%u code=0",kind);assert(strstr(output,expect));cases++;
 }
 reset();char name[4]={'t','e','s','t'};kebab_fairplay_service_evidence(3,name,sizeof(name),-1,0);assert(prints==2&&strstr(output,"name=test\n")&&strstr(output,"trunc=1"));cases++;
 reset();kebab_fairplay_service_evidence(4,"Name\n%s",8,-9,11);assert(strstr(output,"name=Name??s\n")&&strstr(output,"changed=1")&&strstr(output,"result=-9 aux=11"));cases++;
 reset();kebab_fairplay_service_evidence(2,"bad",4,0,0);kebab_fairplay_service_evidence(7,"bad",4,0,0);kebab_fairplay_service_evidence(3,NULL,4,0,0);kebab_fairplay_service_evidence(3,"bad",0,0,0);assert(!prints&&!kebab_fp_resource_count);cases++;
 reset();for(unsigned i=0;i<32;i++)kebab_fairplay_service_evidence(5,"service",8,0,1);assert(!kebab_fairplay_resource_observer_enabled());unsigned oldprints=prints;kebab_fairplay_service_evidence(6,(const char*)1,128,-1,0);assert(prints==oldprints&&kebab_fp_resource_count==32);cases++;
 reset();call(26,0,31);struct Proc other2={.p_comm="fairplayd.H2",.uid=501,.pid=88,.lock=PTHREAD_MUTEX_INITIALIZER};memcpy(other2.uuid,original.uuid,16);self=&other2;assert(!kebab_fairplay_resource_observer_enabled());kebab_fairplay_service_evidence(3,(const char*)1,128,0,0);assert(!prints&&!kebab_fp_resource_count);pthread_mutex_destroy(&other2.lock);cases++;
 printf("PASS %u original FairPlay ring/identity/return-metadata cases; kernel lifecycle/locks modeled, native and physical pending\n",cases);return 0;
}
