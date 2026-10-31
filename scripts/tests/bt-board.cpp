
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <array>
#include <vector>
using vm_offset_t=uintptr_t;using vm_size_t=uintptr_t;using boolean_t=bool;
#define FALSE false
#define VM_WIMG_WCOMB 6u
#define VM_WIMG_IO 7u
#define LCK_GRP_DECLARE(n,t) int n
#define LCK_MTX_DECLARE(n,g) int n=((void)(g),0)
#define PROC_NULL nullptr
alignas(16384) static std::array<uint32_t,0x200000/4> memory;
alignas(16384) static std::array<uint32_t,0x4000/4> mutex_space;
static bool interrupts=true,panicking,irq,preempt,local_locked,clock_stalled;
static void *initproc=(void*)1;
static unsigned maps,unmaps,fail_map,mem_reads,lock_reads,lock_writes,acquires,releases,barriers,cases,release_foreign,free_after;
static uint64_t usec;static unsigned read_cost=1;
static std::vector<unsigned> addresses;
static bool panic_active(){return panicking;}
static bool ml_at_interrupt_context(){return irq;}
static bool ml_get_interrupts_enabled(){return interrupts;}
static int get_preemption_level(){return preempt;}
static void lck_mtx_lock(int *){assert(!local_locked&&interrupts);local_locked=true;}
static void lck_mtx_unlock(int *){assert(local_locked&&interrupts);local_locked=false;}
static boolean_t ml_set_interrupts_enabled(bool v){assert(local_locked);bool old=interrupts;interrupts=v;return old;}
static void host_barrier(){assert(local_locked&&!interrupts);++barriers;}
static uint32_t host_read(uintptr_t address){
 assert(local_locked&&!interrupts);
 if(address==(uintptr_t)mutex_space.data()+0x3000){++lock_reads;return mutex_space[0x3000/4];}
 assert(mutex_space[0x3000/4]==1&&address>=(uintptr_t)memory.data()&&address<=(uintptr_t)memory.data()+0x200000-4&&!(address&3));
 ++mem_reads;unsigned offset=address-(uintptr_t)memory.data();addresses.push_back(offset);if(!clock_stalled)usec+=read_cost;return memory[offset/4];
}
static void host_write(uintptr_t address,uint32_t value){
 assert(local_locked&&!interrupts&&address==(uintptr_t)mutex_space.data()+0x3000&&value<=1);++lock_writes;
 auto &owner=mutex_space[0x3000/4];
 if(value){++acquires;if(!owner)owner=1;}
 else{assert(owner==1&&"must never clear another processor's lock");++releases;owner=release_foreign;}
}
static void clock_get_uptime(uint64_t *t){assert(local_locked&&!interrupts);*t=usec;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n){*n=t*1000;}
static void delay(unsigned us){assert(local_locked&&!interrupts&&us==1);if(!clock_stalled)++usec;if(free_after&&usec>=free_after)mutex_space[0x3000/4]=0;}
static vm_offset_t ml_io_map_unmappable(vm_offset_t pa,vm_size_t size,uint32_t flags){
 assert(local_locked&&interrupts);if(++maps==fail_map)return 0;
 if(pa==0x80900000&&size==0x200000&&flags==6)return (uintptr_t)memory.data();
 if(pa==0x1f40000&&size==0x4000&&flags==7)return (uintptr_t)mutex_space.data();
 assert(false&&"unexpected physical mapping");return 0;
}
static void ml_io_unmap(vm_offset_t address,vm_size_t size){assert(local_locked&&interrupts);assert((address==(uintptr_t)memory.data()&&size==0x200000)||(address==(uintptr_t)mutex_space.data()&&size==0x4000));++unmaps;}
static void host_log(const char *,...){}
/* Native board identity, read from firmware SMEM_PROJECT135. No user ABI. */
#ifndef KEBAB_BOARD_H
#define KEBAB_BOARD_H
#include <sys/types.h>
#include <stdbool.h>
struct kebab_board_info {
 uint32_t smem_version, ocdt_version, project, dtsi, record_bytes;
 bool cached;
};
#ifdef __cplusplus
extern "C" {
#endif
int kebab_board_read_info(struct kebab_board_info *);
void kebab_board_report(void);
#ifdef __cplusplus
}
#endif
#endif
/* Fixed SMEM_PROJECT query and TCSR lock3, per LineageOS98a7970
 * smem.c, qcom_hwspinlock.c and oplus_project_data_ocdt.h.
 * Only metadata and the first12 bytes of item135 are read. No allocation,
 * SMEM writes, arbitrary item API, serial numbers or calibration dumping.
 */
#ifndef KEBAB_BOARD_IMPL_H
#define KEBAB_BOARD_IMPL_H
#define KBB_SMEM_PA 0x80900000u
#define KBB_SMEM_SIZE 0x200000u
#define KBB_MUTEX_PA 0x01f40000u
#define KBB_MUTEX_SIZE 0x4000u
#define KBB_MUTEX3 0x3000u
struct kbb_owner { bool held, dead; };
struct kbb_status {
 uint32_t attempts, reads, last_offset, owner_before, owner_after, smem_version;
 int parse_error, cleanup_error;
 bool acquired, released, quarantined;
};
struct kbb_io {
 int (*memory)(void *,uint32_t,uint32_t *);
 int (*lock_read)(void *,uint32_t *);
 int (*lock_write)(void *,uint32_t);
 uint64_t (*now)(void *);
 void (*wait)(void *);
 void *context;
 struct kbb_owner *owner;
};
struct kbb_reader {
 const struct kbb_io *io;
 struct kbb_status *status;
 uint64_t started;
};
static int
kbb_word(struct kbb_reader *r,uint32_t off,uint32_t *out)
{
 if(!out||(off&3u)||off>KBB_SMEM_SIZE-4u)return EINVAL;
 if(++r->status->reads>16384u||r->io->now(r->io->context)-r->started>10000u)return ETIMEDOUT;
 r->status->last_offset=off;
 return r->io->memory(r->io->context,off,out);
}
static int
kbb_payload(struct kbb_reader *r,uint32_t off,uint32_t size,bool cached,
    struct kebab_board_info *out)
{
 if(size<12u||size>KBB_SMEM_SIZE||off>KBB_SMEM_SIZE-size)return EINVAL;
 struct kebab_board_info found;memset(&found,0,sizeof(found));int error;
 if((error=kbb_word(r,off,&found.ocdt_version))||
    (error=kbb_word(r,off+4u,&found.project))||
    (error=kbb_word(r,off+8u,&found.dtsi)))return error;
 if(found.ocdt_version!=1u&&found.ocdt_version!=2u)return ENOTSUP;
 found.smem_version=r->status->smem_version;found.record_bytes=size;found.cached=cached;
 *out=found;return 0;
}
static int
kbb_project(struct kbb_reader *r,struct kebab_board_info *out)
{
 uint32_t initialized,reserved,version;int error;
 if((error=kbb_word(r,192,&initialized))||(error=kbb_word(r,204,&reserved))||
    (error=kbb_word(r,92,&version)))return error;
 r->status->smem_version=version;
 if(initialized!=1u||reserved)return EINVAL;
 if((version>>16)==11u){
  /* Legacy global TOC: fixed item135, no allocation/free heap changes. */
  uint32_t allocated,offset,size,base,used,available,at=208u+135u*16u;
  if((error=kbb_word(r,196,&used))||(error=kbb_word(r,200,&available)))return error;
  if(used<208u+512u*16u||used>KBB_SMEM_SIZE||available>KBB_SMEM_SIZE-used)return EINVAL;
  if((error=kbb_word(r,at,&allocated))||(error=kbb_word(r,at+4u,&offset))||
     (error=kbb_word(r,at+8u,&size))||(error=kbb_word(r,at+12u,&base)))return error;
  if(!allocated)return ENOENT;
  if((base&0xfffffffcu)!=0u&&(base&0xfffffffcu)!=KBB_SMEM_PA)return ENOTSUP;
  if(offset<208u+512u*16u||size>used||offset>used-size)return EINVAL;
  return kbb_payload(r,offset,size,false,out);
 }
 if((version>>16)!=12u)return ENOTSUP;
 uint32_t table=KBB_SMEM_SIZE-4096u,magic,table_version,count;
 if((error=kbb_word(r,table,&magic))||(error=kbb_word(r,table+4u,&table_version))||
    (error=kbb_word(r,table+8u,&count)))return error;
 if(magic!=0x434f5424u||table_version!=1u||count>(4096u-32u)/48u)return EINVAL;
 uint32_t partition=0,size=0,cacheline=0;
 for(uint32_t i=0;i<count;++i){
  uint32_t at=table+32u+i*48u,hosts;
  if((error=kbb_word(r,at+12u,&hosts)))return error;
  if(hosts!=0xfffefffeu)continue;
  if(partition)return EEXIST;
  if((error=kbb_word(r,at,&partition))||(error=kbb_word(r,at+4u,&size))||
     (error=kbb_word(r,at+16u,&cacheline)))return error;
  if(partition<4096u||(partition&3u)||size<32u||(size&3u)||
      partition>table||size>table-partition)return EINVAL;
 }
 if(!partition)return ENOENT;
 uint32_t hosts,actual_size,free_uncached,free_cached;
 if((error=kbb_word(r,partition,&magic))||(error=kbb_word(r,partition+4u,&hosts))||
    (error=kbb_word(r,partition+8u,&actual_size))||
    (error=kbb_word(r,partition+12u,&free_uncached))||
    (error=kbb_word(r,partition+16u,&free_cached)))return error;
 if(magic!=0x54525024u||hosts!=0xfffefffeu||actual_size!=size||
     free_uncached<32u||free_uncached>free_cached||free_cached>size)return EINVAL;
 /* Prefer uncached item like the original getter; allocation holds the same
  * remote lock. Walk headers, skip payloads of every unrelated item. */
 uint32_t at=32u;
 for(unsigned n=0;at<free_uncached;++n){
  if(n>=1024u)return E2BIG;
  if(at>free_uncached||free_uncached-at<16u)return EINVAL;
  uint32_t tag,bytes,padding;
  if((error=kbb_word(r,partition+at,&tag))||
     (error=kbb_word(r,partition+at+4u,&bytes))||
     (error=kbb_word(r,partition+at+8u,&padding)))return error;
  uint32_t pad_data=padding&0xffffu,pad_header=padding>>16;
  if((tag&0xffffu)!=0xa5a5u||bytes<=pad_data||pad_header>free_uncached-at-16u)return EINVAL;
  uint32_t data=at+16u+pad_header;
  if(bytes>free_uncached-data)return EINVAL;
  if((tag>>16)==135u)return kbb_payload(r,partition+data,bytes-pad_data,false,out);
  at=data+bytes;
 }
 if(free_cached==size)return ENOENT;
 if(cacheline<16u||cacheline>4096u||(cacheline&(cacheline-1u))||size<cacheline)return EINVAL;
 at=size-cacheline;
 for(unsigned n=0;at>free_cached;++n){
  if(n>=1024u)return E2BIG;
  if(at>size-16u)return EINVAL;
  uint32_t tag,bytes,padding;
  if((error=kbb_word(r,partition+at,&tag))||
     (error=kbb_word(r,partition+at+4u,&bytes))||
     (error=kbb_word(r,partition+at+8u,&padding)))return error;
  uint32_t pad_data=padding&0xffffu;
  if((tag&0xffffu)!=0xa5a5u||bytes<=pad_data||bytes>at||bytes>at-free_cached)return EINVAL;
  uint32_t data=at-bytes;
  if((tag>>16)==135u)return kbb_payload(r,partition+data,bytes-pad_data,true,out);
  if(data<cacheline)return EINVAL;
  at=data-cacheline;
 }
 return ENOENT;
}
static int
kbb_query(const struct kbb_io *io,struct kebab_board_info *out,struct kbb_status *status)
{
 if(!out||!status)return EINVAL;
 memset(out,0,sizeof(*out));memset(status,0,sizeof(*status));
 if(!io||!io->memory||!io->lock_read||!io->lock_write||!io->now||!io->wait||!io->owner)return EINVAL;
 if(io->owner->dead||io->owner->held)return EBUSY;
 uint32_t owner;int error=io->lock_read(io->context,&owner);
 if(error)return error;
 status->owner_before=owner;
 /* APPS PID1 before our attempt is not ours: never inherit/clear it. */
 if(owner==1u)return EBUSY;
 uint64_t started=io->now(io->context);
 for(unsigned n=0;n<1000u;++n){
  ++status->attempts;
  error=io->lock_write(io->context,1u);
  if(error){io->owner->dead=true;break;}
  error=io->lock_read(io->context,&owner);
  if(error){io->owner->dead=true;break;}
  if(owner==1u){io->owner->held=true;status->acquired=true;break;}
  if(io->now(io->context)-started>=1000u)break;
  io->wait(io->context);
 }
 if(!status->acquired){
  /* A failed write may nevertheless have acquired the lock. Release only
   * when readback proves APPS ownership, preserving the delivery error. */
  if(io->owner->dead){uint32_t seen;if(!io->lock_read(io->context,&seen)&&seen==1u){io->owner->held=true;status->acquired=true;}}
  if(!status->acquired)return error?error:ETIMEDOUT;
 }
 struct kebab_board_info found;memset(&found,0,sizeof(found));
 if(!error){struct kbb_reader reader={io,status,io->now(io->context)};error=kbb_project(&reader,&found);}
 status->parse_error=error;
 int cleanup=io->lock_read(io->context,&owner);
 if(!cleanup&&owner!=1u)cleanup=EIO;
 if(!cleanup){
  cleanup=io->lock_write(io->context,0u);
  if(cleanup)io->owner->dead=true;
  uint32_t seen;int readback=io->lock_read(io->context,&seen);
  if(!readback&&seen!=1u){status->owner_after=seen;status->released=true;io->owner->held=false;}
  if(readback&&!cleanup)cleanup=readback;
  if(!status->released&&!cleanup)cleanup=EIO;
 }
 status->cleanup_error=cleanup;
 if(!status->released)io->owner->dead=true;
 if(cleanup&&!error)error=cleanup;
 if(!error)*out=found;
 return error;
}
#endif

#define printf(...) host_log(__VA_ARGS__)
static LCK_GRP_DECLARE(kbb_group,"kebab-board");
static LCK_MTX_DECLARE(kbb_mutex,&kbb_group);
static struct { vm_offset_t memory,mutex;struct kbb_owner owner; } kbb_native;
static int
kbb_native_memory(void *context,uint32_t off,uint32_t *value)
{
 (void)context;
 if(!value||!kbb_native.memory||!kbb_native.owner.held||ml_get_interrupts_enabled()||
    (off&3u)||off>KBB_SMEM_SIZE-4u)return EINVAL;
 host_barrier();
 *value=host_read(kbb_native.memory+off);
 host_barrier();return 0;
}
static int
kbb_native_lock_read(void *context,uint32_t *value)
{
 (void)context;
 if(!value||!kbb_native.mutex||ml_get_interrupts_enabled())return EINVAL;
 host_barrier();
 *value=host_read(kbb_native.mutex+KBB_MUTEX3);
 host_barrier();return 0;
}
static int
kbb_native_lock_write(void *context,uint32_t value)
{
 (void)context;
 if(!kbb_native.mutex||ml_get_interrupts_enabled()||value>1u||
    (!value&&!kbb_native.owner.held)||(value&&kbb_native.owner.dead))return EINVAL;
 host_barrier();
 host_write(kbb_native.mutex+KBB_MUTEX3,value);
 host_barrier();return 0;
}
static uint64_t
kbb_native_now(void *context)
{
 (void)context;uint64_t ticks,ns;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns/1000u;
}
static void kbb_native_wait(void *context){(void)context;delay(1);}
static int
kbb_native_query(struct kebab_board_info *info,struct kbb_status *status)
{
 if(!info||!status)return EINVAL;
 memset(info,0,sizeof(*info));memset(status,0,sizeof(*status));
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled())return EBUSY;
 if(__atomic_load_n(&initproc,__ATOMIC_ACQUIRE)==PROC_NULL)return EAGAIN;
 lck_mtx_lock(&kbb_mutex);
 int error=EBUSY;
 if(kbb_native.owner.dead||kbb_native.owner.held)goto done;
 /* The permanent I/O submap is only8MiB. Temporary normal-context mappings
  * use kernel_map and are removed after every query, including errors.
  * Both resources are16KiB aligned/sized for this kernel's actual page size. */
 kbb_native.memory=ml_io_map_unmappable(KBB_SMEM_PA,KBB_SMEM_SIZE,VM_WIMG_WCOMB);
 if(!kbb_native.memory){error=ENOMEM;goto done;}
 kbb_native.mutex=ml_io_map_unmappable(KBB_MUTEX_PA,KBB_MUTEX_SIZE,VM_WIMG_IO);
 if(!kbb_native.mutex){error=ENOMEM;goto unmap;}
 {
  const struct kbb_io io={kbb_native_memory,kbb_native_lock_read,kbb_native_lock_write,
      kbb_native_now,kbb_native_wait,NULL,&kbb_native.owner};
  boolean_t saved=ml_set_interrupts_enabled(FALSE);
  error=kbb_query(&io,info,status);
  ml_set_interrupts_enabled(saved);
 }
 ml_io_unmap(kbb_native.mutex,KBB_MUTEX_SIZE);kbb_native.mutex=0;
unmap:
 ml_io_unmap(kbb_native.memory,KBB_SMEM_SIZE);kbb_native.memory=0;
done:
 status->quarantined=kbb_native.owner.dead;
 lck_mtx_unlock(&kbb_mutex);return error;
}
int
kebab_board_read_info(struct kebab_board_info *info)
{
 struct kbb_status status;return kbb_native_query(info,&status);
}
void
kebab_board_report(void)
{
 struct kebab_board_info info;struct kbb_status status;
 int error=kbb_native_query(&info,&status);
 printf("KEBAB BTBOARD: SMEM=%08x+%x item=135 version=%08x error=%d\n",KBB_SMEM_PA,KBB_SMEM_SIZE,status.smem_version,error);
 if(!error)printf("KEBAB BTBOARD: OCDT=%u project=%u dtsi=%u bytes=%u cached=%u\n",info.ocdt_version,info.project,info.dtsi,info.record_bytes,info.cached);
 printf("KEBAB BTBOARD: TCSR3 attempts=%u acquired=%u released=%u owner=%u/%u reads=%u last=%x parse=%d cleanup=%d quarantined=%u\n",status.attempts,status.acquired,status.released,status.owner_before,status.owner_after,status.reads,status.last_offset,status.parse_error,status.cleanup_error,status.quarantined);
 printf("KEBAB BTBOARD: end error=%d fixed board metadata only; no SMEM/rail/GPIO writes\n",error);
}

#undef printf

static void word(unsigned at,uint32_t value){assert(!(at&3)&&at<0x200000);memory[at/4]=value;}
static constexpr unsigned part=0x4000,psize=0x10000,table=0x1ff000,project_at=part+96;
static void fixture(unsigned kind=0){
 memory.fill(0);mutex_space.fill(0);kbb_native={};
 interrupts=true;panicking=irq=preempt=local_locked=clock_stalled=false;initproc=(void*)1;
 maps=unmaps=fail_map=mem_reads=lock_reads=lock_writes=acquires=releases=barriers=release_foreign=free_after=0;usec=0;read_cost=1;addresses.clear();
 word(192,1);word(92,12u<<16);
 word(table,0x434f5424);word(table+4,1);word(table+8,2);
 word(table+32+12,0x10000); // unrelated APPS/modem partition, not scanned
 word(table+80,part);word(table+84,psize);word(table+92,0xfffefffe);word(table+96,64);
 word(part,0x54525024);word(part+4,0xfffefffe);word(part+8,psize);word(part+12,200);word(part+16,psize);
 word(part+32,(10u<<16)|0xa5a5);word(part+36,32);
 for(unsigned at=part+48;at<part+80;at+=4)word(at,0xdeadbeef); // unrelated payload must never be read
 word(part+80,(135u<<16)|0xa5a5);word(part+84,104);word(part+88,4);
 word(project_at,1);word(project_at+4,20809);word(project_at+8,20809);
 if(kind==1){ // original global TOC format
  word(92,11u<<16);word(196,0x3000);word(200,0x200000-0x3000);
  word(2368,1);word(2372,0x2800);word(2376,104);
  word(0x2800,2);word(0x2804,20828);word(0x2808,20828);
 }
 if(kind==2){ // cached item, header at top-cacheline, data precedes header
  word(part+12,32);unsigned h=part+psize-64,data=h-128;
  word(part+16,psize-64-128);word(h,(135u<<16)|0xa5a5);word(h+4,128);word(h+8,(48u<<16)|28);
  word(data,1);word(data+4,19805);word(data+8,19805);
 }
}
static bool empty(const kebab_board_info &i){kebab_board_info zero{};return !memcmp(&i,&zero,sizeof(i));}
static void normal(int expected){
 kebab_board_info info;memset(&info,0xa5,sizeof(info));kbb_status status;int actual=kbb_native_query(&info,&status);
 if(actual!=expected)fprintf(stderr,"case%u actual%d expected%d\n",cases,actual,expected);
 assert(actual==expected&&interrupts&&!local_locked&&!kbb_native.memory&&!kbb_native.mutex&&maps==unmaps+(fail_map?1u:0u));
 if(expected)assert(empty(info));else assert(status.acquired&&status.released&&!status.cleanup_error&&!kbb_native.owner.held&&!kbb_native.owner.dead&&mutex_space[0x3000/4]!=1);
 ++cases;
}
static unsigned fail_memory,fail_lock_read,fail_lock_write,read_call,write_call,force_foreign;
static bool apply_write_error;
static int memory_fault(void *c,uint32_t off,uint32_t *v){if(++read_call==fail_memory)return EIO;return kbb_native_memory(c,off,v);}
static int lock_read_fault(void *c,uint32_t *v){if(force_foreign&&lock_reads+1==force_foreign)mutex_space[0x3000/4]=2;if(lock_reads+1==fail_lock_read){++lock_reads;return EIO;}return kbb_native_lock_read(c,v);}
static int lock_write_fault(void *c,uint32_t v){if(++write_call==fail_lock_write){if(apply_write_error)assert(kbb_native_lock_write(c,v)==0);return EIO;}return kbb_native_lock_write(c,v);}
static void prepare_core(){local_locked=true;interrupts=false;kbb_native.memory=(uintptr_t)memory.data();kbb_native.mutex=(uintptr_t)mutex_space.data();read_call=write_call=0;}
static int fault_query(kebab_board_info *out,kbb_status *status){const kbb_io io={memory_fault,lock_read_fault,lock_write_fault,kbb_native_now,kbb_native_wait,nullptr,&kbb_native.owner};return kbb_query(&io,out,status);}
int main(){
 for(unsigned mode=0;mode<3;++mode){fixture(mode);auto before=std::vector<uint32_t>(memory.begin(),memory.end());kebab_board_info info;assert(kebab_board_read_info(&info)==0);assert(info.project==(mode==0?20809:mode==1?20828:19805)&&info.dtsi==info.project&&info.ocdt_version==(mode==1?2u:1u)&&info.record_bytes==(mode==1?104u:100u)&&info.cached==(mode==2));assert(interrupts&&!local_locked&&maps==2&&unmaps==2&&acquires==1&&releases==1&&!kbb_native.owner.dead&&!kbb_native.owner.held&&std::equal(before.begin(),before.end(),memory.begin()));
  for(unsigned at:addresses){assert(!(at>=part+48&&at<part+80));if(mode==0)assert(!(at>=project_at+12&&at<project_at+104));}++cases;
 }
 fixture();kbb_status baseline;kebab_board_info info;assert(kbb_native_query(&info,&baseline)==0);unsigned total_reads=mem_reads;++cases;
 // All unsafe entry contexts refuse before mappings, local lock and TCSR.
 for(unsigned c=0;c<5;++c){fixture();panicking=c==0;irq=c==1;preempt=c==2;interrupts=c!=3;if(c==4)initproc=nullptr;assert(kebab_board_read_info(&info)==(c==4?EAGAIN:EBUSY)&&empty(info)&&!maps&&!lock_reads&&!lock_writes&&!local_locked);++cases;}
 fixture();assert(kebab_board_read_info(nullptr)==EINVAL&&!maps);++cases;
 for(unsigned failure=1;failure<=2;++failure){fixture();fail_map=failure;normal(ENOMEM);assert(!lock_writes&&!mem_reads);}
 fixture();mutex_space[0x3000/4]=1;normal(EBUSY);assert(!acquires&&!releases&&!mem_reads); // no inherited APPS ownership
 fixture();mutex_space[0x3000/4]=2;normal(ETIMEDOUT);assert(!releases&&!mem_reads&&mutex_space[0x3000/4]==2&&acquires<=1000);
 fixture();mutex_space[0x3000/4]=2;clock_stalled=true;normal(ETIMEDOUT);assert(acquires==1000&&!releases&&!mem_reads);
 fixture();mutex_space[0x3000/4]=2;free_after=3;normal(0);assert(acquires>=3&&releases==1);
 fixture();release_foreign=2;normal(0);assert(mutex_space[0x3000/4]==2); // peer immediately takes released hardware lock
 fixture();read_cost=4000;normal(ETIMEDOUT);assert(releases==1&&!kbb_native.owner.dead);
 for(auto patch:std::vector<std::array<uint32_t,3>>{
 {192,0,EINVAL},{204,1,EINVAL},{92,10u<<16,ENOTSUP},{table,0,EINVAL},{table+4,2,EINVAL},{table+8,85,EINVAL},
 {table+80,0,EINVAL},{table+84,0xffffffffu,EINVAL},{part,0,EINVAL},{part+4,0xfffe0000,EINVAL},{part+8,0x2000,EINVAL},{part+12,psize+4,EINVAL},{part+16,100,EINVAL},
 {part+80,135u<<16,EINVAL},{part+84,0,EINVAL},{part+88,104,EINVAL},{part+88,0xffff0000,EINVAL},{part+84,0x100000,EINVAL},{project_at,3,ENOTSUP}}){fixture();word(patch[0],patch[1]);normal(patch[2]);assert(releases==1&&!kbb_native.owner.dead);}
 fixture();word(part+80,(136u<<16)|0xa5a5);normal(ENOENT);
 for(unsigned mode:{1u,2u}){fixture(mode);if(mode==1)word(2368,0);else word(table+96,3);normal(mode==1?ENOENT:EINVAL);}
 fixture(1);word(2380,0xdead0000);normal(ENOTSUP);
 fixture(1);word(2372,0x2ff8);normal(EINVAL);
 // Uncached project takes priority over a separately cached copy, like Linux.
 fixture();unsigned h=part+psize-64;word(part+16,psize-64-128);word(h,(135u<<16)|0xa5a5);word(h+4,128);word(h+8,28);word(h-128,1);word(h-124,19811);word(h-120,19811);assert(kebab_board_read_info(&info)==0&&info.project==20809&&!info.cached);++cases;
 fail_memory=fail_lock_read=fail_lock_write=force_foreign=0;apply_write_error=false;
 for(unsigned failure=1;failure<=total_reads;++failure){fixture();prepare_core();fail_memory=failure;kbb_status st;assert(fault_query(&info,&st)==EIO&&empty(info)&&st.released&&releases==1&&!kbb_native.owner.dead);++cases;}
 fail_memory=0;
 for(unsigned failure:{1u,2u,3u,4u}){fixture();prepare_core();fail_lock_read=failure;kbb_status st;assert(fault_query(&info,&st)==EIO&&empty(info));if(failure==1)assert(!acquires&&!releases&&!kbb_native.owner.dead);else assert(kbb_native.owner.dead);++cases;}
 fail_lock_read=0;
 for(unsigned failure:{1u,2u})for(bool apply:{false,true}){fixture();prepare_core();fail_lock_write=failure;apply_write_error=apply;kbb_status st;assert(fault_query(&info,&st)==EIO&&empty(info)&&kbb_native.owner.dead);unsigned written=lock_writes;assert(fault_query(&info,&st)==EBUSY&&lock_writes==written);++cases;}
 fail_lock_write=0;apply_write_error=false;fixture();prepare_core();force_foreign=3;kbb_status st;assert(fault_query(&info,&st)==EIO&&empty(info)&&!releases&&mutex_space[0x3000/4]==2&&kbb_native.owner.dead);++cases;force_foreign=0;
 fixture();prepare_core();uint32_t value=0xa5;for(unsigned off:{1u,0x200000u,0xfffffffcu}){assert(kbb_native_memory(nullptr,off,&value)==EINVAL&&value==0xa5&&!mem_reads);++cases;}assert(kbb_native_lock_write(nullptr,2)==EINVAL&&!lock_writes);assert(kbb_native_memory(nullptr,0,&value)==EINVAL&&!mem_reads);++cases;
 printf("PASS production SMEM board reader/TCSR/native lifecycle: %u cases; memory, hardware arbitration, maps, IRQ, barriers and time explicitly mocked; no physical board/RF proof\n",cases);
}
