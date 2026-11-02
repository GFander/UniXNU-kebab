
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>
#define MACH64_SEND_MSG 1ull
#define MACH64_RCV_MSG 2ull
#define MACH64_RCV_SYNC_WAIT 0x4000ull
#define MACH64_MSG_VECTOR 0x100000000ull
#define MACH_PORT_VALID(n) ((n)!=0u&&(n)!=UINT32_MAX)
#define KERN_SUCCESS 0
#define KERN_INVALID_NAME 15
#define IS_NULL NULL
#define IP_NULL NULL
#define IP_VALID(p) ((p)!=NULL&&(p)!=(void*)-1)
typedef int kern_return_t;
struct Space{bool active;};typedef struct Space*ipc_space_t;
static struct Space kernel_space,reply_space;static ipc_space_t ipc_space_kernel=&kernel_space,ipc_space_reply=&reply_space;
struct Port{uint32_t ip_receiver_name;bool active;ipc_space_t receiver_space;struct Port*destination;unsigned refs;pid_t pid;
 unsigned ip_specialreply,ip_service_port,ip_sync_bootstrap_checkin,ip_immovable_receive,ip_sync_link_state,kotype;
 struct{uint16_t imq_msgcount,imq_qlimit;}ip_messages;};typedef struct Port*ipc_port_t;
struct kebab_saved_user_site{uint64_t pc,lr,fp,trap,options,bits_size,ports,voucher_id,desc_rcv;};
static struct Port ports[12];static struct Space user_space={true};
static ipc_port_t locked;static unsigned lookups,unlocks,acquires,references,releases,prints,cases,walk_locks;
static uint32_t expected_name=0x11223344;static bool failure,race_dead,race_reassign;
static char line[224],output[8192];
static int observed_printf(const char*fmt,...){assert(!locked);prints++;va_list args;va_start(args,fmt);int n=vsnprintf(line,sizeof(line),fmt,args);va_end(args);assert(n>0&&(unsigned)n<sizeof(line)&&line[n-1]=='\n');assert(strlen(output)+(size_t)n<sizeof(output));strcat(output,line);return n;}
#define printf observed_printf
static void held(ipc_port_t p){assert(locked==p&&p>=ports&&p<ports+12&&p->refs>0);}
static int ipc_port_translate_send(ipc_space_t space,uint32_t name,ipc_port_t*out){assert(!locked);lookups++;assert(name==expected_name);*out=NULL;
 if(failure||!space->active)return KERN_INVALID_NAME;locked=&ports[0];acquires++;*out=locked;return KERN_SUCCESS;}
static bool ip_active(ipc_port_t p){held(p);return p->active;}
static bool ip_in_a_space(ipc_port_t p){held(p);return p->active&&p->ip_receiver_name!=0;}
static bool ip_in_space(ipc_port_t p,ipc_space_t space){held(p);return ip_in_a_space(p)&&p->receiver_space==space;}
static bool ip_in_transit(ipc_port_t p){held(p);assert(p->active);return !ip_in_a_space(p)&&p->destination!=NULL;}
static ipc_port_t ip_get_destination(ipc_port_t p){held(p);return p->active&&!ip_in_a_space(p)?p->destination:NULL;}
static unsigned ip_kotype(ipc_port_t p){held(p);assert(p->active);return p->kotype;}
static uint32_t ip_get_receiver_name(ipc_port_t p){held(p);return ip_in_a_space(p)?p->ip_receiver_name:0;}
static int ipc_port_get_receiver_task_locked(ipc_port_t p,uintptr_t*out){held(p);assert(out==NULL);return ip_in_a_space(p)&&!ip_in_space(p,ipc_space_kernel)&&!ip_in_space(p,ipc_space_reply)?p->pid:-1;}
static void ip_reference(ipc_port_t p){assert(locked&&locked->destination==p);assert(p->refs>0);p->refs++;references++;}
static void ip_release(ipc_port_t p){assert(!locked&&p->refs>1);p->refs--;releases++;}
static void ip_mq_unlock(ipc_port_t p){held(p);locked=NULL;unlocks++;
 if(p==ports&&p->destination&&(race_dead||race_reassign)){ipc_port_t n=p->destination;assert(n->refs>1);if(race_dead){n->active=false;n->destination=(void*)-1;}else{n->ip_receiver_name=0x5678;n->receiver_space=&user_space;n->pid=91;}race_dead=race_reassign=false;}}
static void ip_mq_lock(ipc_port_t p){assert(!locked&&p->refs>1);locked=p;acquires++;walk_locks++;}
/* Decode only mach_msg2 trap metadata, never the user message payload.
 * The waiting thread's register copy and this later IPC lookup are separate
 * best-effort observations: neither proves an atomic request/reply pairing. */
struct kebab_mach_wait_metadata {
 uint64_t options;
 uint32_t bits,send_size_or_count,remote,local,receive;
 int32_t message_id;
};

static bool
kebab_mach_wait_decode(const struct kebab_saved_user_site *site,struct kebab_mach_wait_metadata *out)
{
 /* XNU get_saved_state_svc_number uses only the low signed word. */
 if((int32_t)(uint32_t)site->trap!=-47||!(site->options&(MACH64_SEND_MSG|MACH64_RCV_MSG)))return false;
 struct kebab_mach_wait_metadata value={};
 value.options=site->options;value.bits=(uint32_t)site->bits_size;
 value.send_size_or_count=(uint32_t)(site->bits_size>>32);
 value.remote=(uint32_t)site->ports;value.local=(uint32_t)(site->ports>>32);
 value.receive=(uint32_t)(site->desc_rcv>>32);value.message_id=(int32_t)(uint32_t)(site->voucher_id>>32);
 *out=value;return true;
}


/* A non-atomic destination ancestry sample, not a message delivery trace.
 * The caller lends the first active locked port, without a new reference.
 * ip_destination holds a reference while its parent is locked (ipc_port.h).
 * Take our own next reference before dropping that lock; never lock two ports.
 * Thus dead/moved destinations remain safe, but can change between samples. */
#define KEBAB_IPC_PATH_MAX 8
struct kebab_mach_port_state {
 uint64_t object,destination;
 uint32_t kind,flags,link,kotype,queued,limit,receiver_name;
 pid_t receiver;
};

static unsigned
kebab_mach_wait_port_path_locked(ipc_port_t port,struct kebab_mach_port_state path[KEBAB_IPC_PATH_MAX])
{
 unsigned count=0;bool owned=false;
 while(count<KEBAB_IPC_PATH_MAX){
  struct kebab_mach_port_state *state=&path[count++];
  *state=(struct kebab_mach_port_state){.object=(uint64_t)(uintptr_t)port,.receiver=-1};
  ipc_port_t next=IP_NULL;
  if(ip_active(port)){
   bool in_space=ip_in_a_space(port);
   /* kind: dead0, task-space1, kernel-space2, reply-space3, transit4, limbo5. */
   state->kind=in_space?(ip_in_space(port,ipc_space_kernel)?2:
       (ip_in_space(port,ipc_space_reply)?3:1)):(ip_in_transit(port)?4:5);
   state->receiver=ipc_port_get_receiver_task_locked(port,NULL);
   state->receiver_name=ip_get_receiver_name(port);
   state->kotype=ip_kotype(port);state->link=port->ip_sync_link_state;
   state->flags=(port->ip_specialreply?1u:0u)|(port->ip_service_port?2u:0u)|
       (port->ip_sync_bootstrap_checkin?4u:0u)|(port->ip_immovable_receive?8u:0u);
   state->queued=port->ip_messages.imq_msgcount;state->limit=port->ip_messages.imq_qlimit;
   next=ip_get_destination(port);state->destination=(uint64_t)(uintptr_t)next;
  }
  bool follow=IP_VALID(next)&&count<KEBAB_IPC_PATH_MAX;
  for(unsigned i=0;i<count;i++)if(path[i].object==(uint64_t)(uintptr_t)next)follow=false;
  if(follow)ip_reference(next);
  ip_mq_unlock(port);
  if(owned)ip_release(port);
  if(!follow)break;
  port=next;owned=true;ip_mq_lock(port);
 }
 return count;
}

static void
kebab_mach_wait_report(ipc_space_t space,const char *label,uint64_t tid,const struct kebab_saved_user_site *site)
{
 struct kebab_mach_wait_metadata value;
 if(space==IS_NULL||!kebab_mach_wait_decode(site,&value))return;
 kern_return_t status=KERN_INVALID_NAME;pid_t receiver=-1;uint32_t receiver_name=0;
 uint64_t object=0;bool attempted=false;
 struct kebab_mach_port_state path[KEBAB_IPC_PATH_MAX];unsigned path_count=0;
 /* In receive-only mach_msg2, XNU uses mr_lp's low word as the sync
  * send boosting port when RCV_SYNC_WAIT is set (mach_msg.c). This is
  * receiver context, not proof of delivery or the original message id. */
 bool send=(value.options&MACH64_SEND_MSG)!=0;
 bool sync=(value.options&MACH64_RCV_MSG)&&(value.options&MACH64_RCV_SYNC_WAIT);
 const char *role=send?"send":(sync?"sync":"none");
 if((send||sync)&&MACH_PORT_VALID(value.remote)){
  ipc_port_t port=IP_NULL;attempted=true;
  /* No preexisting locks. The helper consumes the borrowed initial lock
   * and releases only references that it explicitly owns. */
  status=ipc_port_translate_send(space,value.remote,&port);
  if(status==KERN_SUCCESS){
   path_count=kebab_mach_wait_port_path_locked(port,path);
   receiver=path[0].receiver;
   if(receiver>0)receiver_name=path[0].receiver_name;
   object=path[0].object;
  }
 }
 /* Keep each printf below the observed transport truncation boundary.
  * Separate bounded lines retain complete newlines even with max values. */
 printf("KEBAB %s IPC: tid=%llu opt=%llx b=%x sz=%u v=%u r=%x l=%x q=%x id=%d lookup=%u\n",
     label,tid,value.options,value.bits,value.send_size_or_count,!!(value.options&MACH64_MSG_VECTOR),
     value.remote,value.local,value.receive,value.message_id,attempted);
 printf("KEBAB %s IPC-OWNER: tid=%llu role=%s name=%x kr=%x pid=%d recv=%x object=%llx\n",
     label,tid,role,value.remote,status,receiver,receiver_name,object);
 for(unsigned i=0;i<path_count;i++){
  const struct kebab_mach_port_state *state=&path[i];
  printf("KEBAB %s IPC-STATE: tid=%llu hop=%u obj=%llx kind=%u pid=%d recv=%x dst=%llx f=%x link=%u kt=%u q=%u/%u\n",
      label,tid,i,state->object,state->kind,state->receiver,state->receiver_name,
      state->destination,state->flags,state->link,state->kotype,state->queued,state->limit);
 }
}

#undef printf
static void reset(void){assert(!locked);for(unsigned i=0;i<12;i++)assert(!ports[i].refs||ports[i].refs==1);
 memset(ports,0,sizeof(ports));for(unsigned i=0;i<12;i++){ports[i].refs=1;ports[i].active=true;ports[i].receiver_space=&user_space;ports[i].ip_receiver_name=0x12345678;ports[i].pid=42;ports[i].ip_messages.imq_qlimit=5;}
 lookups=unlocks=acquires=references=releases=prints=walk_locks=0;failure=race_dead=race_reassign=false;line[0]=output[0]=0;expected_name=0x11223344;}
static void balanced(void){assert(!locked&&unlocks==acquires&&references==releases);for(unsigned i=0;i<12;i++)assert(ports[i].refs==1);}
static void report(struct kebab_saved_user_site*s){kebab_mach_wait_report(&user_space,"MIG-LONG",UINT64_MAX,s);balanced();unsigned states=0;for(char*p=output;(p=strstr(p," IPC-STATE:"));p++)states++;assert(states==(prints>2?prints-2:0));}
int main(void){struct kebab_saved_user_site site={.trap=(uint64_t)(int64_t)-47,.options=3,.ports=0xaabbccdd11223344ull,.bits_size=0x1234567887654321ull,.voucher_id=0xffffffc900000000ull,.desc_rcv=0x1234abcd00000000ull};struct kebab_mach_wait_metadata value;
 reset();assert(kebab_mach_wait_decode(&site,&value));assert(value.options==3&&value.bits==0x87654321&&value.send_size_or_count==0x12345678&&value.remote==0x11223344&&value.local==0xaabbccdd&&value.receive==0x1234abcd&&value.message_id==-55);cases++;
 for(unsigned n=0;n<64;n++){site.trap=((uint64_t)n<<32)|(uint32_t)-47;assert(kebab_mach_wait_decode(&site,&value));cases++;}
 for(unsigned n=0;n<128;n++){reset();site.trap=n;memset(&value,0xa5,sizeof(value));assert(!kebab_mach_wait_decode(&site,&value));for(unsigned i=0;i<sizeof(value);i++)assert(((uint8_t*)&value)[i]==0xa5);report(&site);assert(!lookups&&!prints);cases++;}
 site.trap=(uint64_t)(int64_t)-47;
 for(unsigned opts=0;opts<16;opts++){reset();site.options=(opts&3)|((opts&4)?MACH64_RCV_SYNC_WAIT:0)|((opts&8)?MACH64_MSG_VECTOR:0);report(&site);bool decoded=(opts&3)!=0;bool expected=(opts&1)||((opts&2)&&(opts&4));assert(lookups==expected&&prints==(decoded?(expected?3u:2u):0u));if(expected)assert(strstr(output,"kind=1 pid=42 recv=12345678 dst=0 "));cases++;}
 site.options=3;reset();report(&site);assert(strstr(output,"r=11223344 l=aabbccdd q=1234abcd id=-55 ")&&strstr(output,"role=send name=11223344 kr=0 pid=42 recv=12345678 "));cases++;
 for(unsigned n=0;n<3;n++){reset();site.options=MACH64_RCV_MSG|MACH64_RCV_SYNC_WAIT|(n==1?MACH64_SEND_MSG:0);if(n==2)site.options=MACH64_RCV_MSG;report(&site);assert(lookups==(n<2));assert(strstr(output,n==0?"role=sync ":n==1?"role=send ":"role=none "));cases++;}
 site.options=3;
 for(unsigned n=0;n<2;n++){reset();failure=n==0;user_space.active=n==0;report(&site);assert(lookups==1&&prints==2&&unlocks==0&&strstr(output,"kr=f pid=-1 recv=0 object=0"));cases++;}user_space.active=true;
 for(unsigned n=0;n<2;n++){reset();site.ports=n?UINT32_MAX:0;report(&site);assert(!lookups&&prints==2);cases++;}site.ports=0xaabbccdd11223344ull;
 reset();kebab_mach_wait_report(NULL,"MIG",1,&site);assert(!prints&&!lookups);cases++;
 for(unsigned n=0;n<3;n++){reset();ports[0].receiver_space=n==0?ipc_space_kernel:n==1?ipc_space_reply:&user_space;report(&site);assert(strstr(output,n==0?"kind=2 pid=-1 ":n==1?"kind=3 pid=-1 ":"kind=1 pid=42 "));cases++;}
 reset();ports[0].ip_receiver_name=0;report(&site);assert(strstr(output,"kind=5 pid=-1 recv=0 dst=0 ")&&walk_locks==0);cases++;
 reset();ports[0].ip_receiver_name=0;ports[0].destination=&ports[1];ports[1].pid=73;report(&site);assert(prints==4&&walk_locks==1&&references==1&&strstr(output,"kind=4 pid=-1 recv=0 ")&&strstr(output,"kind=1 pid=73 "));cases++;
 reset();ports[0].ip_receiver_name=0;ports[0].destination=&ports[1];race_dead=true;report(&site);assert(prints==4&&walk_locks==1&&references==1&&strstr(output,"kind=0 pid=-1 recv=0 dst=0 "));cases++;
 reset();ports[0].ip_receiver_name=0;ports[0].destination=&ports[1];race_reassign=true;report(&site);assert(strstr(output,"kind=1 pid=91 recv=5678 "));cases++;
 for(unsigned length=2;length<=12;length++){reset();for(unsigned i=0;i<length-1;i++){ports[i].ip_receiver_name=0;ports[i].destination=&ports[i+1];}report(&site);unsigned count=length<8?length:8;assert(prints==2+count&&walk_locks==count-1&&references==count-1);cases++;}
 reset();ports[0].ip_receiver_name=0;ports[0].destination=ports;report(&site);assert(prints==3&&!references&&!walk_locks);cases++;
 reset();ports[0].ip_receiver_name=0;ports[0].destination=&ports[1];ports[1].ip_receiver_name=0;ports[1].destination=ports;report(&site);assert(prints==4&&references==1&&walk_locks==1);cases++;
 reset();ports[0].ip_specialreply=ports[0].ip_service_port=ports[0].ip_sync_bootstrap_checkin=ports[0].ip_immovable_receive=1;ports[0].ip_sync_link_state=5;ports[0].kotype=55;ports[0].ip_messages.imq_msgcount=4;report(&site);assert(strstr(output,"f=f link=5 kt=55 q=4/5"));cases++;
 reset();expected_name=UINT32_MAX-1;site.ports=UINT64_MAX-1;site.options=UINT64_MAX;site.bits_size=site.desc_rcv=UINT64_MAX;site.voucher_id=(uint64_t)0x80000000<<32;ports[0].pid=INT32_MAX;ports[0].ip_receiver_name=UINT32_MAX;ports[0].kotype=UINT32_MAX;ports[0].ip_messages.imq_msgcount=ports[0].ip_messages.imq_qlimit=UINT16_MAX;report(&site);assert(prints==3&&strstr(output,"kt=4294967295 q=65535/65535"));cases++;
 printf("PASS %u actual IPC decode/state/reference/path cases; IPC task and scheduler are explicit models, not physical qualification\n",cases);return 0;}
