
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
typedef int kern_return_t;
struct Space{bool active;};typedef struct Space*ipc_space_t;
struct Port{uint32_t ip_receiver_name;};typedef struct Port*ipc_port_t;
struct kebab_saved_user_site{uint64_t pc,lr,fp,trap,options,bits_size,ports,voucher_id,desc_rcv;};
static unsigned lock_depth,lookups,unlocks,prints,cases;
static uint32_t expected_name;static int failure,owner=42;static struct Port real_port={0x12345678};
static char line[224],output[1024];
static int observed_printf(const char*fmt,...){assert(!lock_depth);prints++;va_list args;va_start(args,fmt);int n=vsnprintf(line,sizeof(line),fmt,args);va_end(args);assert(n>0&&(unsigned)n<sizeof(line)&&line[n-1]=='\n');assert(strlen(output)+(size_t)n<sizeof(output));strcat(output,line);return n;}
#define printf observed_printf
static int ipc_port_translate_send(ipc_space_t space,uint32_t name,ipc_port_t*out){assert(!lock_depth);lookups++;assert(name==expected_name);*out=NULL;
 if(failure||!space->active)return KERN_INVALID_NAME;lock_depth=1;*out=&real_port;return KERN_SUCCESS;
}
static int ipc_port_get_receiver_task_locked(ipc_port_t port,uintptr_t*out){assert(lock_depth==1&&port==&real_port&&out==NULL);return owner;}
static void ip_mq_unlock(ipc_port_t port){assert(lock_depth==1&&port==&real_port);lock_depth=0;unlocks++;}
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

static void
kebab_mach_wait_report(ipc_space_t space,const char *label,uint64_t tid,const struct kebab_saved_user_site *site)
{
 struct kebab_mach_wait_metadata value;
 if(space==IS_NULL||!kebab_mach_wait_decode(site,&value))return;
 kern_return_t status=KERN_INVALID_NAME;pid_t receiver=-1;uint32_t receiver_name=0;
 uint64_t object=0;bool attempted=false;
 /* In receive-only mach_msg2, XNU uses mr_lp's low word as the sync
  * send boosting port when RCV_SYNC_WAIT is set (mach_msg.c). This is
  * receiver context, not proof of delivery or the original message id. */
 bool send=(value.options&MACH64_SEND_MSG)!=0;
 bool sync=(value.options&MACH64_RCV_MSG)&&(value.options&MACH64_RCV_SYNC_WAIT);
 const char *role=send?"send":(sync?"sync":"none");
 if((send||sync)&&MACH_PORT_VALID(value.remote)){
  ipc_port_t port=IP_NULL;attempted=true;
  /* Takes no preexisting locks; success returns an active locked port,
   * without a new reference. Read metadata under that lock, then unlock.
   * No port dereference, proc lookup or printing after releasing it. */
  status=ipc_port_translate_send(space,value.remote,&port);
  if(status==KERN_SUCCESS){
   receiver=ipc_port_get_receiver_task_locked(port,NULL);
   if(receiver>0)receiver_name=port->ip_receiver_name;
   object=(uint64_t)(uintptr_t)port;ip_mq_unlock(port);
  }
 }
 /* Keep each printf below the observed transport truncation boundary.
  * Separate bounded lines retain complete newlines even with max values. */
 printf("KEBAB %s IPC: tid=%llu opt=%llx b=%x sz=%u v=%u r=%x l=%x q=%x id=%d lookup=%u\n",
     label,tid,value.options,value.bits,value.send_size_or_count,!!(value.options&MACH64_MSG_VECTOR),
     value.remote,value.local,value.receive,value.message_id,attempted);
 printf("KEBAB %s IPC-OWNER: tid=%llu role=%s name=%x kr=%x pid=%d recv=%x object=%llx\n",
     label,tid,role,value.remote,status,receiver,receiver_name,object);
}
#undef printf
static void reset(void){assert(!lock_depth);lookups=unlocks=prints=0;failure=0;owner=42;line[0]=output[0]=0;}
int main(void){
 (void)ip_mq_unlock;
 struct Space space={true};struct kebab_saved_user_site site={0};struct kebab_mach_wait_metadata value;
 site.trap=(uint64_t)(int64_t)-47;site.options=3;
 site.bits_size=((uint64_t)0x12345678<<32)|0x87654321;
 site.ports=((uint64_t)0xaabbccdd<<32)|0x11223344;expected_name=0x11223344;
 site.voucher_id=((uint64_t)(uint32_t)-55<<32)|0xdeadbeef;
 site.desc_rcv=((uint64_t)0x1234abcd<<32)|0x54321;
 assert(kebab_mach_wait_decode(&site,&value));assert(value.options==3&&value.bits==0x87654321&&value.send_size_or_count==0x12345678);
 assert(value.remote==0x11223344&&value.local==0xaabbccdd&&value.receive==0x1234abcd&&value.message_id==-55);cases++;
 reset();kebab_mach_wait_report(&space,"MIG-L",777,&site);assert(lookups==1&&unlocks==1&&prints==2&&!lock_depth);
 assert(strstr(output,"tid=777 ")&&strstr(output,"r=11223344 ")&&strstr(output,"l=aabbccdd ")&&strstr(output,"q=1234abcd ")&&strstr(output,"id=-55 "));
 assert(strstr(output,"lookup=1")&&strstr(output,"role=send name=11223344 kr=0 pid=42 recv=12345678 "));cases++;
 /* Sign/zero extension of x16 must match XNU's low signed svc word. */
 for(unsigned n=0;n<64;n++){
  site.trap=((uint64_t)n<<32)|(uint32_t)-47;assert(kebab_mach_wait_decode(&site,&value));cases++;
 }
 for(unsigned n=0;n<128;n++){
  site.trap=n;memset(&value,0xa5,sizeof(value));assert(!kebab_mach_wait_decode(&site,&value));
  for(unsigned i=0;i<sizeof(value);i++)assert(((uint8_t*)&value)[i]==0xa5);reset();kebab_mach_wait_report(&space,"MIG-L",777,&site);assert(!lookups&&!prints&&!unlocks);cases++;
 }
 site.trap=(uint64_t)(int64_t)-47;
 for(unsigned mask=0;mask<4;mask++){
  site.options=mask|MACH64_MSG_VECTOR;memset(&value,0xa5,sizeof(value));assert(kebab_mach_wait_decode(&site,&value)==(mask!=0));
  reset();kebab_mach_wait_report(&space,"MIG-L",777,&site);assert(prints==(mask?2u:0u));assert(lookups==((mask&1)?1u:0u)&&unlocks==lookups);
  if(mask)assert(strstr(output,"v=1 ")&&strstr(output,(mask&1)?"lookup=1\n":"lookup=0\n"));cases++;
 }
 site.options=3;
 for(unsigned n=0;n<3;n++){
  reset();failure=n==0;space.active=n!=1;owner=n==2?-1:42;kebab_mach_wait_report(&space,"BBOARD",778,&site);
  assert(lookups==1&&prints==2&&unlocks==(n==2?1u:0u));
  assert(strstr(output,n==2?"kr=0 pid=-1 recv=0 ":"kr=f pid=-1 recv=0 object=0"));cases++;
 }
 space.active=true;owner=42;
 for(unsigned n=0;n<2;n++){
  reset();site.ports=(site.ports&0xffffffff00000000ull)|(n?UINT32_MAX:0u);kebab_mach_wait_report(&space,"MIG-W",779,&site);
  assert(!lookups&&!unlocks&&prints==2&&strstr(output,"lookup=0\n"));cases++;
 }
 reset();site.ports=expected_name;kebab_mach_wait_report(NULL,"MIG-L",777,&site);assert(!lookups&&!unlocks&&!prints);cases++;

 /* Sync receive-only waits use the kernel's boosting-port context. */
 site.ports=((uint64_t)0x778899aa<<32)|expected_name;
 for(unsigned n=0;n<3;n++){
  reset();site.options=MACH64_RCV_MSG|MACH64_RCV_SYNC_WAIT|(n==1?MACH64_SEND_MSG:0);
  if(n==2)site.options=MACH64_RCV_SYNC_WAIT;
  kebab_mach_wait_report(&space,"MIG-L",780,&site);
  assert(lookups==(n==2?0u:1u)&&unlocks==lookups&&prints==(n==2?0u:2u));
  if(n<2)assert(strstr(output,n==1?"role=send ":"role=sync "));cases++;
 }
 /* Fixed labels fit under the conservative224B line budget at max values. */
 reset();expected_name=UINT32_MAX-1;site.options=UINT64_MAX;site.ports=UINT64_MAX-1;
 site.bits_size=UINT64_MAX;site.desc_rcv=UINT64_MAX;site.voucher_id=(uint64_t)0x80000000<<32;
 owner=INT32_MAX;real_port.ip_receiver_name=UINT32_MAX-1;
 kebab_mach_wait_report(&space,"MIG-LONG",UINT64_MAX,&site);
 assert(lookups==1&&unlocks==1&&prints==2&&strstr(output,"tid=18446744073709551615 ")&&strstr(output,"pid=2147483647 "));cases++;
 printf("PASS %u actual Mach metadata/receiver-call cases; native IPC/task/scheduler lifetime remains modeled and physically unqualified\n",cases);
}
