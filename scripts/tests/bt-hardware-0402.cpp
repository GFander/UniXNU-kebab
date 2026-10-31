
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <array>
#include <vector>
using vm_offset_t=uintptr_t;using vm_size_t=uintptr_t;
static void host_log(const char *,...){}
#define SE_HW_PARAM_0 0xe24u
static uint32_t touch_parameter;static uint32_t kt_se_rd(uint32_t off){assert(off==0xe24);return touch_parameter;}
static bool panicking,irq,preempt,interrupts=true,locked;
static unsigned maps,locks,barriers,fail_map,cases,delays,clock_on_after,extra_vote;
static std::array<uint32_t,0x6000/4> gpio_low;
static std::array<uint32_t,0x1000/4> gpio_high,gcc_vote,gcc_branch;
static std::array<uint32_t,0x4000/4> se;
static unsigned panic_active(){return panicking;}
static bool ml_at_interrupt_context(){return irq;}
static unsigned get_preemption_level(){return preempt;}
static bool ml_get_interrupts_enabled(){return interrupts;}
static void kebab_geni_transaction_lock(){assert(!locked);locked=true;++locks;}
static void kebab_geni_transaction_unlock(){assert(locked);locked=false;}
static void host_barrier(){assert(locked);++barriers;}
static void delay(unsigned us){assert(locked&&us==1);++delays;gcc_vote[2]|=extra_vote;if(clock_on_after&&delays>=clock_on_after)gcc_branch[0x72c/4]&=~0x80000000u;}
static vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t bytes){
 assert(locked);++maps;if(maps==fail_map)return 0;
 if(pa==0x0f910000&&bytes==0x6000)return (uintptr_t)gpio_low.data();
 if(pa==0x0f97c000&&bytes==0x1000)return (uintptr_t)gpio_high.data();
 if(pa==0x00152000&&bytes==0x1000)return (uintptr_t)gcc_vote.data();
 if(pa==0x00117000&&bytes==0x1000)return (uintptr_t)gcc_branch.data();
 if(pa==0x00998000&&bytes==0x4000)return (uintptr_t)se.data();
 assert(false&&"unexpected physical resource or mapping length");return 0;
}
/* Shared GENI register field units, not a transport or userspace ABI.
 * qcom-geni-se.h at LineageOS SM8250 98a7970: PARAM0 TX / PARAM1 RX use
 * bits21:16 for FIFO depth in words and bits29:24 for word width in bits. */
#ifndef KEBAB_GENI_REGISTERS_H
#define KEBAB_GENI_REGISTERS_H
#include <stdint.h>
static inline uint32_t kebab_geni_fifo_depth_words(uint32_t param)
{
 return (param>>16)&0x3f;
}
static inline uint32_t kebab_geni_fifo_width_bits(uint32_t param)
{
 return (param>>24)&0x3f;
}
#endif

/* Read-only board resource acquisition shared by hardware bring-up/tests.
 * Register facts: LineageOS SM8250 98a7970af93b7e01ee77156e7fa0eb7e059a57f0
 * kona-qupv3.dtsi, kona-pinctrl.dtsi, gcc-kona.c, qcom-geni-se.h,
 * clk-branch.c. This is an independently written fixed-resource adapter. */
enum kbh_space { KBH_GPIO, KBH_GCC, KBH_SE };
static const uint32_t kbh_pins[6]={16,17,18,19,21,124};
static const uint32_t kbh_clock_offsets[9]={
 0x52008,0x17004,0x17008,0x1772c,0x17730,0x17734,0x17738,0x1773c,0x17740
};
/* Status only: never read a FIFO data register, consume a byte or clear IRQs. */
static const uint32_t kbh_se_offsets[12]={
 0x68,0x6c,0x40,0x48,0x4c,0x908,0xe24,0xe28,0x800,0x804,0x610,0x640
};
#define KBH_REQUIRED_VOTES ((1u<<16)|(1u<<6)|(1u<<7))
#define KBH_CLOCK_OFF (1u<<31)
static bool
kbh_clocks_running(uint32_t vote,uint32_t master,uint32_t slave,uint32_t serial)
{
 if((vote&KBH_REQUIRED_VOTES)!=KBH_REQUIRED_VOTES)return false;
 if((master|serial)&KBH_CLOCK_OFF)return false;
 /* gcc-kona.c declares HWCG only for wrap0 slave AHB, bit1. In that
  * mode clk-branch.c intentionally does not wait for its CLK_OFF bit:
  * the bus ungates on demand. Do not apply this exception to SE6. */
 if((slave&KBH_CLOCK_OFF)&&!(slave&(1u<<1)))return false;
 return true;
}
struct kbh_snapshot {
 uint32_t gpio[6][2],clock[9],se[12];
 uint32_t gpio_valid,clock_valid,se_valid;
};
typedef int (*kbh_reader)(void *,enum kbh_space,uint32_t,uint32_t *);
static bool
kbh_allowed(enum kbh_space space,uint32_t offset)
{
 if(space==KBH_GPIO){
  for(unsigned i=0;i<6;++i)if(offset==kbh_pins[i]*0x1000||offset==kbh_pins[i]*0x1000+4)return true;
 }else if(space==KBH_GCC){
  for(unsigned i=0;i<9;++i)if(offset==kbh_clock_offsets[i])return true;
 }else if(space==KBH_SE){
  for(unsigned i=0;i<12;++i)if(offset==kbh_se_offsets[i])return true;
 }
 return false;
}
static int
kbh_snapshot_read(kbh_reader read,void *ctx,struct kbh_snapshot *out)
{
 if(!read||!out)return EINVAL;
 memset(out,0,sizeof(*out));
 for(unsigned i=0;i<6;++i){
  for(unsigned j=0;j<2;++j){
   int error=read(ctx,KBH_GPIO,kbh_pins[i]*0x1000+j*4,&out->gpio[i][j]);
   if(error)return error;
   out->gpio_valid|=1u<<(i*2+j);
  }
 }
 for(unsigned i=0;i<9;++i){
  int error=read(ctx,KBH_GCC,kbh_clock_offsets[i],&out->clock[i]);
  if(error)return error;
  out->clock_valid|=1u<<i;
 }
 if(!kbh_clocks_running(out->clock[0],out->clock[1],out->clock[2],out->clock[3]))return EAGAIN;
 for(unsigned i=0;i<12;++i){
  int error=read(ctx,KBH_SE,kbh_se_offsets[i],&out->se[i]);
  if(error)return error;
  out->se_valid|=1u<<i;
 }
 if(((out->se[0]>>8)&0xff)!=2)return EPROTONOSUPPORT;
 return 0;
}

/* Bounded fixed SE6 clock vote, no rail/GPIO/UART programming. The board
 * lock serializes the shared GCC register. Roll back only newly added votes,
 * preserving all pre-existing and unrelated clock consumers. */
struct kbh_clock_ops {
 kbh_reader read;
 int (*vote)(void *,uint32_t,uint32_t);
 void (*wait_us)(void *,uint32_t);
 void *ctx;
};
struct kbh_clock_result {
 uint32_t before,after,added,halt[3],polls;
 bool attempted,restored;
};
static int
kbh_clock_prepare(const struct kbh_clock_ops *io,struct kbh_clock_result *out)
{
 if(!io||!io->read||!io->vote||!io->wait_us||!out)return EINVAL;
 memset(out,0,sizeof(*out));
 int error=io->read(io->ctx,KBH_GCC,0x52008,&out->before);
 if(error)return error;
 out->after=out->before;out->added=KBH_REQUIRED_VOTES&~out->before;
 if(out->added){
  out->attempted=true;
  error=io->vote(io->ctx,out->before,out->before|KBH_REQUIRED_VOTES);
  if(error)goto restore;
 }
 for(unsigned i=0;i<200;++i){
  error=io->read(io->ctx,KBH_GCC,0x52008,&out->after);if(error)goto restore;
  for(unsigned j=0;j<3;++j){
   error=io->read(io->ctx,KBH_GCC,kbh_clock_offsets[1+j],&out->halt[j]);
   if(error)goto restore;
  }
  out->polls=i+1;
  if(kbh_clocks_running(out->after,out->halt[0],out->halt[1],out->halt[2]))return 0;
  io->wait_us(io->ctx,1);
 }
 error=ETIMEDOUT;
restore:
 if(!out->added){out->restored=true;return error;}
 /* A failed write can still have reached hardware. Read its real state,
  * then remove only the votes this call attempted to acquire. */
 uint32_t current;
 if(io->read(io->ctx,KBH_GCC,0x52008,&current))return error;
 out->after=current;
 if(current&out->added){
  if(io->vote(io->ctx,current,current&~out->added))return error;
  if(io->read(io->ctx,KBH_GCC,0x52008,&out->after)||(out->after&out->added))return error;
 }
 out->restored=true;return error;
}
static uint32_t touch_geometry(){uint32_t depth;
	depth = kebab_geni_fifo_depth_words(kt_se_rd(SE_HW_PARAM_0));
	if (depth == 0 || depth > 32) {
		depth = 16;
	}
return depth;}

#define printf(...) host_log(__VA_ARGS__)
static struct {
 vm_offset_t gpio_low,gpio_high,gcc_vote,gcc_branch,se;
 uint32_t clock_owned;
 bool clock_dead;
} kbh_native;

static int
kbh_native_read(void *ctx,enum kbh_space space,uint32_t offset,uint32_t *value)
{
 (void)ctx;
 if(!value||!kbh_allowed(space,offset))return EINVAL;
 vm_offset_t *mapping;uint32_t physical,base,size;
 if(space==KBH_GPIO){
  if(offset>=0x7c000){mapping=&kbh_native.gpio_high;base=0x7c000;size=0x1000;}
  else{mapping=&kbh_native.gpio_low;base=0x10000;size=0x6000;}
  physical=0x0f900000+base; /* NORTH; touch uses a separate SOUTH tile. */
 }else if(space==KBH_GCC){
  if(offset==0x52008){mapping=&kbh_native.gcc_vote;base=0x52000;}
  else{mapping=&kbh_native.gcc_branch;base=0x17000;}
  physical=0x00100000+base;size=0x1000;
 }else{
  mapping=&kbh_native.se;base=0;physical=0x00998000;size=0x4000;
 }
 if(offset<base||offset-base>size-4)return EINVAL;
 if(!*mapping)*mapping=ml_io_map(physical,size);
 if(!*mapping)return ENOMEM;
 host_barrier();
 *value=*(volatile uint32_t *)(*mapping+offset-base);
 host_barrier();
 return 0;
}

void
kebab_bluetooth_hardware_report(void)
{
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled()){
  printf("KEBAB BTHW: refused unsafe context; no mappings or register access\n");return;
 }
 struct kbh_snapshot status;
 /* The same mutex serializes touch's shared GCC vote register updates. */
 kebab_geni_transaction_lock();
 int error=kbh_snapshot_read(kbh_native_read,NULL,&status);
 kebab_geni_transaction_unlock();
 printf("KEBAB BTHW: fixed SE6/NORTH read-only snapshot error=%d valid=%x/%x/%x\n",error,status.gpio_valid,status.clock_valid,status.se_valid);
 for(unsigned i=0;i<6;++i)if((status.gpio_valid&(3u<<(i*2)))==(3u<<(i*2)))
  printf("KEBAB BTHW: GPIO%u ctl=%08x io=%08x mux=%u input=%u output=%u oe=%u\n",kbh_pins[i],status.gpio[i][0],status.gpio[i][1],(status.gpio[i][0]>>2)&15,status.gpio[i][1]&1,(status.gpio[i][1]>>1)&1,(status.gpio[i][0]>>9)&1);
 for(unsigned i=0;i<9;++i)if(status.clock_valid&(1u<<i))
  printf("KEBAB BTHW: GCC offset=%05x value=%08x\n",kbh_clock_offsets[i],status.clock[i]);
 for(unsigned i=0;i<12;++i)if(status.se_valid&(1u<<i))
  printf("KEBAB BTHW: SE6 offset=%03x value=%08x\n",kbh_se_offsets[i],status.se[i]);
 if((status.se_valid&0xc0)==0xc0)
  printf("KEBAB BTHW: TX FIFO depth=%u words width=%u bits; RX depth=%u words width=%u bits\n",kebab_geni_fifo_depth_words(status.se[6]),kebab_geni_fifo_width_bits(status.se[6]),kebab_geni_fifo_depth_words(status.se[7]),kebab_geni_fifo_width_bits(status.se[7]));
 if(error==EAGAIN)printf("KEBAB BTHW: SE6 registers deferred: required votes or running/valid HWCG clock state absent\n");
 printf("KEBAB BTHW: no GPIO/clock/power/controller writes; HCI and radio functionality not established\n");
}

/* Normal-context clock preparation. The only writable MMIO word in this
 * step is the fixed GCC vote register, and only the three SE6/wrap0 bits. */
static int
kbh_native_vote(void *ctx,uint32_t before,uint32_t after)
{
 if((before^after)&~KBH_REQUIRED_VOTES)return EINVAL;
 uint32_t current;int error=kbh_native_read(ctx,KBH_GCC,0x52008,&current);
 if(error)return error;
 if(current!=before)return EBUSY;
 host_barrier();
 *(volatile uint32_t *)(kbh_native.gcc_vote+8)=after;
 host_barrier();
 error=kbh_native_read(ctx,KBH_GCC,0x52008,&current);
 return error?error:current==after?0:EIO;
}
static void
kbh_native_wait_us(void *ctx,uint32_t us)
{
 (void)ctx;delay(us);
}
void
kebab_bluetooth_clock_prepare(void)
{
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled()){
  printf("KEBAB BTCLK: end error=%d unsafe-context; no register access\n",EBUSY);return;
 }
 const struct kbh_clock_ops io={kbh_native_read,kbh_native_vote,kbh_native_wait_us,NULL};
 struct kbh_clock_result status;memset(&status,0,sizeof(status));
 kebab_geni_transaction_lock();
 int error=kbh_native.clock_dead?EBUSY:kbh_clock_prepare(&io,&status);
 if(!error)kbh_native.clock_owned|=status.added;
 else if(status.attempted&&!status.restored)kbh_native.clock_dead=true;
 uint32_t owned=kbh_native.clock_owned;bool dead=kbh_native.clock_dead;
 kebab_geni_transaction_unlock();
 printf("KEBAB BTCLK: error=%d before=%08x after=%08x added=%08x owned=%08x polls=%u attempted=%u restored=%u quarantined=%u\n",error,status.before,status.after,status.added,owned,status.polls,status.attempted,status.restored,dead);
 if(!error)kebab_bluetooth_hardware_report();
 printf("KEBAB BTCLK: end error=%d clocks-only; no GPIO/rail/RCG/UART/controller programming\n",error);
}

#undef printf

static void fixture(){
 panicking=irq=preempt=locked=false;interrupts=true;maps=locks=barriers=fail_map=delays=clock_on_after=extra_vote=0;kbh_native={};
 gpio_low.fill(0);gpio_high.fill(0);gcc_vote.fill(0);gcc_branch.fill(0);se.fill(0);
 // Independent board fixtures; do not use production register arrays to populate them.
 for(unsigned pin:{16u,17u,18u,19u,21u}){gpio_low[(pin-16)*0x400]=0x200+pin;gpio_low[(pin-16)*0x400+1]=3;}
 gpio_high[0]=124;gpio_high[1]=1;
 gcc_vote[8/4]=(1<<16)|(1<<6)|(1<<7)|0x80000000u;
 gcc_branch[4/4]=1;gcc_branch[8/4]=3;gcc_branch[0x72c/4]=1;
 gcc_branch[0x730/4]=1;gcc_branch[0x734/4]=0x104;gcc_branch[0x738/4]=1;gcc_branch[0x73c/4]=2;gcc_branch[0x740/4]=3;
 se[0x68/4]=0x201;se[0x6c/4]=0x202;se[0xe24/4]=0x20102864;se[0xe28/4]=0x20204800;
}
static int collect(struct kbh_snapshot *out){kebab_geni_transaction_lock();int error=kbh_snapshot_read(kbh_native_read,nullptr,out);kebab_geni_transaction_unlock();return error;}
static unsigned accesses,fail_access;
static int failing_reader(void *ctx,enum kbh_space space,uint32_t off,uint32_t *out){
 if(++accesses==fail_access)return EIO;return kbh_native_read(ctx,space,off,out);
}
static unsigned votes,fail_vote;
static bool apply_failed_vote;
static int failing_vote(void *ctx,uint32_t before,uint32_t after){
 ++votes;
 if(votes==fail_vote){if(apply_failed_vote)assert(kbh_native_vote(ctx,before,after)==0);return EIO;}
 return kbh_native_vote(ctx,before,after);
}
int main(){
 for(auto row:std::vector<std::array<uint32_t,4>>{{0x20102864,16,32,16},{0x20204800,32,32,32},{0x20010000,1,32,1},{0x003f0000,63,0,16},{0,0,0,16},{0xffffffff,63,63,16},{0xc0c0ffff,0,0,16},{0x10102000,16,16,16}}){
  touch_parameter=row[0];assert(kebab_geni_fifo_depth_words(row[0])==row[1]&&kebab_geni_fifo_width_bits(row[0])==row[2]&&touch_geometry()==row[3]);++cases;
 }
 fixture();auto low=gpio_low;auto high=gpio_high;auto vote=gcc_vote;auto branch=gcc_branch;auto serial=se;
 struct kbh_snapshot out;assert(collect(&out)==0);assert(out.gpio_valid==0xfff&&out.clock_valid==0x1ff&&out.se_valid==0xfff);
 assert(out.gpio[0][0]==0x210&&out.gpio[4][0]==0x215&&out.gpio[5][0]==124&&out.se[0]==0x201);assert(maps==5&&barriers==66&&!locked);++cases;
 assert(gpio_low==low&&gpio_high==high&&gcc_vote==vote&&gcc_branch==branch&&se==serial);++cases;
 for(unsigned bit:{6u,7u,16u}){fixture();gcc_vote[2]&=~(1u<<bit);assert(collect(&out)==EAGAIN&&out.clock_valid==0x1ff&&!out.se_valid&&!kbh_native.se&&maps==4);++cases;}
 for(unsigned off:{4u,0x72cu}){fixture();gcc_branch[off/4]|=0x80000000u;assert(collect(&out)==EAGAIN&&!out.se_valid&&!kbh_native.se&&maps==4);++cases;}
 fixture();gcc_branch[8/4]=0x80000001u;assert(collect(&out)==EAGAIN&&!kbh_native.se);++cases;
 fixture();gcc_branch[8/4]=0x80000003u;assert(collect(&out)==0&&out.se_valid==0xfff);++cases;
 fixture();gcc_vote.fill(0);assert(collect(&out)==EAGAIN&&!kbh_native.se);++cases;
 for(unsigned protocol:{0u,1u,3u,255u}){fixture();se[0x68/4]=protocol<<8;assert(collect(&out)==EPROTONOSUPPORT&&out.se_valid==0xfff);++cases;}
 for(unsigned failure=1;failure<=5;++failure){fixture();fail_map=failure;assert(collect(&out)==ENOMEM&&maps==failure&&!locked);++cases;}
 for(unsigned failure=1;failure<=33;++failure){fixture();accesses=0;fail_access=failure;kebab_geni_transaction_lock();assert(kbh_snapshot_read(failing_reader,nullptr,&out)==EIO);kebab_geni_transaction_unlock();assert(accesses==failure&&!locked);++cases;}
 for(unsigned context=0;context<4;++context){fixture();panicking=context==0;irq=context==1;preempt=context==2;interrupts=context!=3;kebab_bluetooth_hardware_report();assert(!maps&&!locks&&!barriers&&!locked);++cases;}
 fixture();kebab_bluetooth_hardware_report();assert(maps==5&&locks==1&&barriers==66&&!locked);++cases;
 // Direct callbacks must not turn arbitrary console-supplied MMIO into reads.
 for(unsigned pin=0;pin<160;++pin)for(unsigned delta:{0u,4u}){
  if(pin==16||pin==17||pin==18||pin==19||pin==21||pin==124)continue;
  fixture();uint32_t value=0xa5;assert(kbh_native_read(nullptr,KBH_GPIO,pin*0x1000+delta,&value)==EINVAL&&value==0xa5&&!maps&&!barriers);++cases;
 }
 for(unsigned space=0;space<4;++space)for(unsigned off:{0u,1u,0x780u,0x700u,0x604u,0x618u,0x648u,0xfffffffcu}){
  fixture();uint32_t value=0xa5;assert(!kbh_allowed((enum kbh_space)space,off));assert(kbh_native_read(nullptr,(enum kbh_space)space,off,&value)==EINVAL&&value==0xa5&&!maps&&!barriers);++cases;
 }
 fixture();assert(kbh_snapshot_read(nullptr,nullptr,&out)==EINVAL&&kbh_snapshot_read(kbh_native_read,nullptr,nullptr)==EINVAL);++cases;
 assert(kbh_native_read(nullptr,KBH_GCC,0x52008,nullptr)==EINVAL&&!maps&&!barriers);++cases;
 // Real production clock policy plus native fixed-register writer. No RF claims.
 struct kbh_clock_result clock;
 const struct kbh_clock_ops io={kbh_native_read,kbh_native_vote,kbh_native_wait_us,nullptr};
 fixture();gcc_vote[2]&=~(1u<<16);uint32_t original=gcc_vote[2];
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==0);kebab_geni_transaction_unlock();
 assert(clock.added==(1u<<16)&&clock.attempted&&clock.polls==1&&gcc_vote[2]==(original|(1u<<16)));assert(gpio_low==low&&gpio_high==high&&se==serial);++cases;
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==0);kebab_geni_transaction_unlock();assert(!clock.added&&!clock.attempted);++cases;
 fixture();gcc_vote[2]&=~(1u<<16);gcc_branch[0x72c/4]|=0x80000000u;clock_on_after=3;
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==0);kebab_geni_transaction_unlock();assert(delays==3&&clock.polls==4);++cases;
 fixture();gcc_vote[2]&=~(1u<<16);gcc_branch[8/4]=0x80000002u;
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==0);kebab_geni_transaction_unlock();++cases;
 // Timeout rollback must retain votes acquired by an independent consumer.
 fixture();gcc_vote[2]&=~(1u<<16);original=gcc_vote[2];gcc_branch[0x72c/4]|=0x80000000u;extra_vote=1u<<24;
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==ETIMEDOUT);kebab_geni_transaction_unlock();assert(delays==200&&clock.polls==200&&clock.restored&&gcc_vote[2]==(original|extra_vote));++cases;
 // Pre-existing votes are not ours to undo, even if a branch stays halted.
 fixture();original=gcc_vote[2];gcc_branch[0x72c/4]|=0x80000000u;
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&io,&clock)==ETIMEDOUT);kebab_geni_transaction_unlock();assert(!clock.added&&!clock.attempted&&gcc_vote[2]==original);++cases;
 for(unsigned post=0;post<2;++post){fixture();gcc_vote[2]&=~(1u<<16);original=gcc_vote[2];votes=0;fail_vote=1;apply_failed_vote=post;
  struct kbh_clock_ops broken={kbh_native_read,failing_vote,kbh_native_wait_us,nullptr};
  kebab_geni_transaction_lock();assert(kbh_clock_prepare(&broken,&clock)==EIO);kebab_geni_transaction_unlock();assert(clock.restored&&gcc_vote[2]==original&&votes==1+post);++cases;
 }
 // An unsuccessful rollback is reported as such; it must not fabricate cleanup.
 fixture();gcc_vote[2]&=~(1u<<16);gcc_branch[0x72c/4]|=0x80000000u;votes=0;fail_vote=2;apply_failed_vote=false;
 struct kbh_clock_ops broken={kbh_native_read,failing_vote,kbh_native_wait_us,nullptr};
 kebab_geni_transaction_lock();assert(kbh_clock_prepare(&broken,&clock)==ETIMEDOUT);kebab_geni_transaction_unlock();assert(!clock.restored&&(gcc_vote[2]&(1u<<16)));++cases;
 for(unsigned failure=1;failure<=5;++failure){fixture();gcc_vote[2]&=~(1u<<16);original=gcc_vote[2];accesses=0;fail_access=failure;
  struct kbh_clock_ops faulty={failing_reader,kbh_native_vote,kbh_native_wait_us,nullptr};
  kebab_geni_transaction_lock();assert(kbh_clock_prepare(&faulty,&clock)==EIO);kebab_geni_transaction_unlock();assert(gcc_vote[2]==original);++cases;
 }
 fixture();kebab_geni_transaction_lock();assert(kbh_native_vote(nullptr,0,1u<<22)==EINVAL);kebab_geni_transaction_unlock();assert(!maps&&!barriers);++cases;
 for(unsigned context=0;context<4;++context){fixture();panicking=context==0;irq=context==1;preempt=context==2;interrupts=context!=3;kebab_bluetooth_clock_prepare();assert(!maps&&!locks&&!barriers&&!locked);++cases;}
 fixture();gcc_vote[2]&=~(1u<<16);kebab_bluetooth_clock_prepare();assert(kbh_native.clock_owned==(1u<<16)&&!kbh_native.clock_dead);original=gcc_vote[2];kebab_bluetooth_clock_prepare();assert(gcc_vote[2]==original&&kbh_native.clock_owned==(1u<<16));++cases;
 fixture();kbh_native.clock_dead=true;original=gcc_vote[2];kebab_bluetooth_clock_prepare();assert(!maps&&!barriers&&gcc_vote[2]==original);++cases;
 assert(kbh_clock_prepare(nullptr,&clock)==EINVAL&&kbh_clock_prepare(&io,nullptr)==EINVAL);++cases;
 std::printf("PASS %u actual collector/mapping/context/bounds cases; MMIO and barriers mocked, real hardware functionality unclaimed\n",cases);
}
