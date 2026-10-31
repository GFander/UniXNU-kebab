
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <array>
using vm_offset_t=uintptr_t;using vm_size_t=uintptr_t;
using proc_t=void *;static proc_t initproc=(void *)1;
#define PROC_NULL nullptr
#define _Static_assert static_assert
static bool panic_flag,irq,preempt,interrupts=true,locked;
static unsigned maps,barriers,clocks,cases;
static std::array<uint32_t,4096> se;
static std::array<uint32_t,1024> pll,root,vote;
static int panic_active(){return panic_flag;}
static bool ml_at_interrupt_context(){return irq;}
static int get_preemption_level(){return preempt;}
static bool ml_get_interrupts_enabled(){return interrupts;}
static void kebab_geni_transaction_lock(){assert(!locked);locked=true;}
static void kebab_geni_transaction_unlock(){assert(locked);locked=false;}
static void kebab_bluetooth_clock_prepare(){++clocks;}
static void barrier(){assert(locked);++barriers;}
static void delay(unsigned us){assert(locked&&us==1);}
static void clock_get_uptime(uint64_t *v){*v=42;}
static void absolutetime_to_nanoseconds(uint64_t v,uint64_t *ns){*ns=v*1000;}
static vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t size){
 assert(locked);++maps;
 if(pa==0x00998000&&size==0x4000)return (uintptr_t)se.data();
 if(pa==0x00100000&&size==0x1000)return (uintptr_t)pll.data();
 if(pa==0x00117000&&size==0x1000)return (uintptr_t)root.data();
 if(pa==0x00152000&&size==0x1000)return (uintptr_t)vote.data();
 assert(false);return 0;
}
static void host_log(const char *,...){}
/* Fixed SM8250 SE6 bootstrap byte transport; independently implemented from
 * register facts in LineageOS sm8250 98a7970 (GENI/UART/GCC/LUCID). No Linux
 * implementation is linked. Internal loopback is not controller/RF proof. */
enum kbu_space { KBU_SE, KBU_GCC };
/* Per exclusive SE6 owner, across transactions, invalidated only by a real
 * hardware reset. 0=not applied,1=applied,2=delivery uncertain (quarantined). */
struct kbu_owner { unsigned default_state; };
struct kbu_io {
 int (*read)(void *,enum kbu_space,uint32_t,uint32_t *);
 int (*write)(void *,enum kbu_space,uint32_t,uint32_t);
 void (*wait)(void *,uint32_t);
 uint64_t (*now)(void *);
 void *ctx;
 struct kbu_owner *owner;
};
struct kbu_result {
 uint32_t phase,frames,tx,rx,mp,sp,pll[6],route,gsi,dma,blocked_offset,blocked_value;
 int error,cleanup_error;
 bool changed,restored;
};
/* Only ordinary RW configuration is saved. Never restore FIFO, command,
 * W1C IRQ or status values as if they were configuration. */
static const uint32_t kbu_config[]={
 0xe1c,0xe18,0x614,0x644,0x48,0x4c,0x24,0x28,0xe30,0x7c,
 0x22c,0x254,0x258,0x25c,0x260,0x264,0x268,0x26c,0x270,
 0x280,0x284,0x288,0x28c,0x294,0x2a4,0x2a8,0x2ac,0x80c,0x810,0x814
};
#define KBU_CONFIG_COUNT (sizeof(kbu_config)/sizeof(kbu_config[0]))
static bool
kbu_allowed(enum kbu_space space,uint32_t off,bool write)
{
 if(space==KBU_GCC){
  if(off>=0x17730&&off<=0x17740&&!(off&3))return true;
  if(!write&&(off==0||off==4||off==0xc||off==0x38||off==0x40||off==0x52018||off==0x52008||off==0x17004||off==0x17008||off==0x1772c))return true;
  return false;
 }
 if(space!=KBU_SE)return false;
 for(unsigned i=0;i<KBU_CONFIG_COUNT;++i)if(off==kbu_config[i])return true;
 if(write)return off==0x20||off==0x600||off==0x604||off==0x618||off==0x630||off==0x634||off==0x648||off==0x700;
 return off==0x40||off==0x64||off==0x68||off==0x6c||off==0x610||off==0x640||off==0x780||off==0x800||off==0x804||off==0xe24||off==0xe28;
}
static int
kbu_read(const struct kbu_io *io,enum kbu_space space,uint32_t off,uint32_t *v)
{return io->read(io->ctx,space,off,v);}
static int
kbu_write(const struct kbu_io *io,enum kbu_space space,uint32_t off,uint32_t v)
{return io->write(io->ctx,space,off,v);}
static int
kbu_set(const struct kbu_io *io,enum kbu_space space,uint32_t off,uint32_t v)
{
 uint32_t got;int e=kbu_write(io,space,off,v);if(e)return e;
 e=kbu_read(io,space,off,&got);return e?e:got==v?0:EIO;
}
static int
kbu_poll_clear(const struct kbu_io *io,enum kbu_space space,uint32_t off,uint32_t bits,unsigned count)
{
 for(unsigned i=0;i<count;++i){uint32_t v;int e=kbu_read(io,space,off,&v);if(e)return e;if(!(v&bits))return 0;io->wait(io->ctx,1);}
 return ETIMEDOUT;
}
static int
kbu_update_root(const struct kbu_io *io)
{
 uint32_t cmd;int e=kbu_read(io,KBU_GCC,0x17730,&cmd);if(e)return e;
 e=kbu_write(io,KBU_GCC,0x17730,cmd|1u);if(e)return e;
 return kbu_poll_clear(io,KBU_GCC,0x17730,1u,500);
}
/* Inactive status alone can precede the firmware CANCEL/ABORT response.
 * Match the vendor stop contract: observe its completion IRQ as well, while
 * serial clocks are still running, before acknowledging or gating clocks. */
static int
kbu_stop_done(const struct kbu_io *io,uint32_t active,uint32_t completed)
{
 for(unsigned i=0;i<1000;++i){
  uint32_t status,irq;int e=kbu_read(io,KBU_SE,0x40,&status);if(e)return e;
  e=kbu_read(io,KBU_SE,active==1?0x610:0x640,&irq);if(e)return e;
  if(!(status&active)&&(irq&completed))return 0;
  io->wait(io->ctx,1);
 }
 return ETIMEDOUT;
}
static int
kbu_stop(const struct kbu_io *io,uint32_t ctrl,uint32_t active)
{
 uint32_t v;int e=kbu_read(io,KBU_SE,0x40,&v);if(e||!(v&active))return e;
 e=kbu_write(io,KBU_SE,ctrl,4);if(e)return e;
 e=kbu_stop_done(io,active,1u<<4);
 if(e!=ETIMEDOUT)return e;
 e=kbu_write(io,KBU_SE,ctrl,2);if(e)return e;
 return kbu_stop_done(io,active,1u<<5);
}
/* Caller retains bytes from a consumed FIFO word when a consumer asks for
 * fewer than four. This is the same byte reader used by the loopback probe;
 * a future HCI parser must not depend on FIFO word boundaries. */
struct kbu_rx { uint32_t word;unsigned remaining; };
static int
kbu_receive(const struct kbu_io *io,struct kbu_rx *rx,uint8_t *dst,unsigned capacity,unsigned *received)
{
 *received=0;
 while(*received<capacity){
  if(!rx->remaining){
   uint32_t status;int e=kbu_read(io,KBU_SE,0x804,&status);if(e)return e;
   unsigned words=status&0x1ffffffu;if(words>32)return EIO;if(!words)break;
   unsigned valid=(status>>28)&7u;
   if(valid>4)return EIO;
   rx->remaining=(words==1&&(status&0x80000000u)&&valid)?valid:4;
   e=kbu_read(io,KBU_SE,0x780,&rx->word);if(e){rx->remaining=0;return e;}
  }
  dst[(*received)++]=(uint8_t)rx->word;rx->word>>=8;--rx->remaining;
 }
 return 0;
}
static int
kbu_transmit(const struct kbu_io *io,const uint8_t *src,unsigned length,unsigned *sent)
{
 *sent=0;uint32_t status;int e=kbu_read(io,KBU_SE,0x800,&status);if(e)return e;
 unsigned words=status&0xfffffffu;if(words>16)return EIO;
 unsigned room=(16-words)*4;
 while(*sent<length&&room){
  uint32_t word=0;unsigned n=length-*sent;if(n>4)n=4;
  for(unsigned j=0;j<n;++j)word|=(uint32_t)src[*sent+j]<<(8*j);
  e=kbu_write(io,KBU_SE,0x700,word);if(e)return e;
  *sent+=n;room-=4;
 }
 return 0;
}
static int
kbu_frame(const struct kbu_io *io,unsigned length,struct kbu_result *out)
{
 uint8_t tx[513],rx[513];unsigned sent=0,received=0;struct kbu_rx stash={0,0};
 if(!length||length>sizeof(tx))return EINVAL;
 for(unsigned i=0;i<length;++i)tx[i]=(uint8_t)((i*73u+length*19u)^(i>>1));
 int e=kbu_set(io,KBU_SE,0x270,length);if(e)return e;
 e=kbu_write(io,KBU_SE,0x600,1u<<27);if(e)return e;
 uint64_t start=io->now(io->ctx);
 /* Monotonic deadline AND iteration bound: a broken timer cannot hang USB. */
 for(unsigned poll=0;poll<100000;++poll){
  uint32_t m,s,state;unsigned n;
  e=kbu_read(io,KBU_SE,0x610,&m);if(e)return e;out->mp=m;
  e=kbu_read(io,KBU_SE,0x640,&s);if(e)return e;out->sp=s;
  if((m&0x3300000eu)||(s&0x03007e0eu))return EIO;
  /* Deliberately exercise tiny consumer buffers, including partial words. */
  unsigned cap=length-received;if(cap>3)cap=3;
  e=kbu_receive(io,&stash,rx+received,cap,&n);received+=n;out->rx+=n;if(e)return e;
  if(s&0x0c000000u){e=kbu_write(io,KBU_SE,0x648,s&0x0c000000u);if(e)return e;}
  /* Loopback is its own peer: bound outstanding bytes to half the RX FIFO.
   * This pacing belongs to this test, not the generic transmit primitive. */
  if(received>sent||sent-received>64)return EIO;
  unsigned budget=64-(sent-received),want=length-sent;
  if(want>budget)want=budget;
  if(want>0&&want<length-sent)want&=~3u;
  e=kbu_transmit(io,tx+sent,want,&n);sent+=n;out->tx+=n;if(e)return e;
  e=kbu_read(io,KBU_SE,0x40,&state);if(e)return e;
  if((m&1u)&&!(state&1u)&&sent==length&&received==length){
   if(stash.remaining||memcmp(tx,rx,length))return EILSEQ;
   e=kbu_write(io,KBU_SE,0x618,1);if(e)return e;
   return 0;
  }
  if(io->now(io->ctx)-start>=100000)return ETIMEDOUT;
  io->wait(io->ctx,1);
 }
 return ETIMEDOUT;
}
static int
kbu_loopback(const struct kbu_io *io,struct kbu_result *out)
{
 if(!io||!out||!io->read||!io->write||!io->wait||!io->now||!io->owner)return EINVAL;
 memset(out,0,sizeof(*out));
 if(io->owner->default_state>1){out->error=EBUSY;return EBUSY;}
 uint32_t saved[KBU_CONFIG_COUNT],root[4],v;int e=0;unsigned saved_count=0;
 /* PLL is shared with USB/UFS: inspect LUCID layout, never tune/vote it. */
 static const uint32_t pll_offsets[]={0,4,0xc,0x38,0x40,0x52018};
 out->phase=1;
 {
  uint32_t c[4];const uint32_t off[]={0x52008,0x17004,0x17008,0x1772c};
  for(unsigned i=0;i<4;++i){e=kbu_read(io,KBU_GCC,off[i],&c[i]);if(e)goto done;}
  if((c[0]&0x100c0u)!=0x100c0u||((c[1]|c[3])&0x80000000u)||((c[2]&0x80000000u)&&!(c[2]&2u))){e=EAGAIN;goto done;}
 }
 for(unsigned i=0;i<6;++i){e=kbu_read(io,KBU_GCC,pll_offsets[i],&out->pll[i]);if(e)goto done;}
 /* In hardware FSM mode ACTIVE_FLAG is authoritative (vendor enable
  * waits for it); software OPMODE can remain STANDBY. Manual control
  * instead requires RUN and global OUTCTRL. Never change shared PLL. */
 if(!(out->pll[0]&0x80000000u)||
    ((out->pll[0]&(1u<<20))?!(out->pll[0]&(1u<<30)):(!(out->pll[0]&1)||out->pll[3]!=1))||
    out->pll[1]!=31||((out->pll[2]>>8)&15)!=1||out->pll[4]!=0x4000||!(out->pll[5]&1)){
  e=EPROTONOSUPPORT;goto done;
 }
 out->phase=2;
 e=kbu_read(io,KBU_SE,0x64,&v);if(e)goto done;if(v&1){e=EPROTONOSUPPORT;goto done;}
 e=kbu_read(io,KBU_SE,0x68,&v);if(e)goto done;if(((v>>8)&255)!=2){e=EPROTONOSUPPORT;goto done;}
 e=kbu_read(io,KBU_SE,0x6c,&v);if(e)goto done;if(((v>>8)&255)!=2){e=EPROTONOSUPPORT;goto done;}
 e=kbu_read(io,KBU_SE,0xe24,&v);if(e)goto done;if(((v>>16)&63)!=16||((v>>24)&63)!=32){e=EPROTONOSUPPORT;goto done;}
 e=kbu_read(io,KBU_SE,0xe28,&v);if(e)goto done;if(((v>>16)&63)!=32||((v>>24)&63)!=32){e=EPROTONOSUPPORT;goto done;}
 for(unsigned i=0;i<5;++i){
  const uint32_t offsets[]={0x40,0x800,0x804,0x610,0x640};
  e=kbu_read(io,KBU_SE,offsets[i],&v);if(e)goto done;if(v){out->blocked_offset=offsets[i];out->blocked_value=v;e=EBUSY;goto done;}
 }
 e=kbu_read(io,KBU_GCC,0x17730,&v);if(e)goto done;if(v&1){e=EBUSY;goto done;}
 for(unsigned i=0;i<4;++i){e=kbu_read(io,KBU_GCC,0x17734+4*i,&root[i]);if(e)goto done;}
 for(unsigned i=0;i<KBU_CONFIG_COUNT;++i){e=kbu_read(io,KBU_SE,kbu_config[i],&saved[i]);if(e)goto done;++saved_count;}
 /* SE6 has no other native driver/IRQ607 registration. Boot firmware
  * leaves the four documented aggregate IRQ gates enabled (0xf), while
  * both engines, FIFOs, pending IRQs and GSI/DMA are idle. Those gates
  * are configuration, not evidence of a live consumer. Mask them for
  * our exclusive polled transaction, then restore the exact saved word.
  * Unknown routing bits or active GSI/DMA still refuse before writes. */
 out->route=saved[0];out->gsi=saved[1];out->dma=saved[12];
 if((saved[0]&~0xfu)||saved[1]||(saved[12]&1)){out->blocked_offset=(saved[0]&~0xfu)?0xe1c:saved[1]?0xe18:0x258;out->blocked_value=(saved[0]&~0xfu)?saved[0]:saved[1]?saved[1]:saved[12];e=EBUSY;goto done;}
 out->changed=true;out->phase=3;
 /* Polled bootstrap: local status latches enabled, CPU IRQ routing stays0. */
 #define KBU_SET(space,off,val) do{e=kbu_set(io,space,off,val);if(e)goto cleanup;}while(0)
 KBU_SET(KBU_SE,0xe1c,0);
 KBU_SET(KBU_SE,0x48,0);KBU_SET(KBU_SE,0x4c,0);
 KBU_SET(KBU_GCC,0x17738,(root[1]&0xffff0000u)|384u);
 KBU_SET(KBU_GCC,0x1773c,(root[2]&0xffff0000u)|0xc476u);
 KBU_SET(KBU_GCC,0x17740,(root[3]&0xffff0000u)|0xc2f6u);
 KBU_SET(KBU_GCC,0x17734,(root[0]&~0x10371fu)|0x102601u);
 e=kbu_update_root(io);if(e)goto cleanup;
 KBU_SET(KBU_SE,0x28,saved[7]|0x7fu);KBU_SET(KBU_SE,0xe30,saved[8]|0xfu);
 KBU_SET(KBU_SE,0x24,0x7f);
 /* FORCE_DEFAULT is the one-time port initialization operation, not
  * ordinary transfer configuration. Repeating it on the live SE6 wedges
  * this device's bus (actual last-MMIO crash proof). The vendor driver
  * likewise runs port_setup once, then reuses the initialized engine. */
 if(io->owner->default_state==0){
  io->owner->default_state=2;
  e=kbu_write(io,KBU_SE,0x20,1);if(e)goto cleanup;
  io->owner->default_state=1;
 }
 KBU_SET(KBU_SE,0x7c,0);KBU_SET(KBU_SE,0x22c,1);
 KBU_SET(KBU_SE,0x254,0);KBU_SET(KBU_SE,0x258,0);
 KBU_SET(KBU_SE,0x25c,2);KBU_SET(KBU_SE,0x268,8);KBU_SET(KBU_SE,0x26c,0);
 KBU_SET(KBU_SE,0x280,0);KBU_SET(KBU_SE,0x28c,8);KBU_SET(KBU_SE,0x294,160);
 KBU_SET(KBU_SE,0x2a4,0);KBU_SET(KBU_SE,0x2a8,0);KBU_SET(KBU_SE,0x2ac,0);
 /* Four 8-bit symbols in each 32-bit FIFO word, low byte first. */
 KBU_SET(KBU_SE,0x260,0x4380e);KBU_SET(KBU_SE,0x264,0xc3e0e);
 KBU_SET(KBU_SE,0x284,0x4380e);KBU_SET(KBU_SE,0x288,0xc3e0e);
 KBU_SET(KBU_SE,0x80c,2);KBU_SET(KBU_SE,0x810,16);KBU_SET(KBU_SE,0x814,28);
 KBU_SET(KBU_SE,0x614,0x3300003fu);KBU_SET(KBU_SE,0x644,0x0f007e3fu);
 KBU_SET(KBU_SE,0x48,0x41);KBU_SET(KBU_SE,0x4c,0x41);
 out->phase=4;e=kbu_write(io,KBU_SE,0x630,(1u<<27)|0x80u);if(e)goto cleanup;
 {
  static const unsigned lengths[]={1,3,4,63,64,65,255,257,513};
  for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i){
   e=kbu_frame(io,lengths[i],out);if(e)goto cleanup;++out->frames;
  }
 }
 out->phase=5;
cleanup:
 {
  int c=kbu_stop(io,0x604,1);int s=kbu_stop(io,0x634,1u<<12);
  if(!c)c=s;
  /* Never change clocks/config under an engine which failed to stop. */
  if(!c){
   /* Acknowledge the completed sequencers before disabling serial clocks.
    * The later acknowledgement still handles level watermark reassertion. */
   for(unsigned i=0;i<2&&!c;++i){uint32_t status;c=kbu_read(io,KBU_SE,i?0x640:0x610,&status);if(!c)c=kbu_write(io,KBU_SE,i?0x648:0x618,status);}
   uint32_t status;int x=kbu_read(io,KBU_SE,0x804,&status);if(!x&&(status&0x1ffffffu))x=EIO;if(!c)c=x;
   x=kbu_read(io,KBU_SE,0x800,&status);if(!x&&(status&0xfffffffu))x=EIO;if(!c)c=x;
   x=kbu_read(io,KBU_SE,0x40,&status);if(!x&&(status&0x1001u))x=EIO;if(!c)c=x;
  }
  if(!c){
   c=kbu_set(io,KBU_SE,0x48,0);if(!c)c=kbu_set(io,KBU_SE,0x4c,0);
   for(unsigned i=0;i<4&&!c;++i)c=kbu_set(io,KBU_GCC,0x17734+i*4,root[i]);
   if(!c)c=kbu_update_root(io);
   /* Clocks restored last, after every other ordinary config word. */
   for(unsigned i=saved_count;i>0&&!c;){--i;if(i==0||i==4||i==5)continue;c=kbu_set(io,KBU_SE,kbu_config[i],saved[i]);}
   if(!c)c=kbu_set(io,KBU_SE,0x4c,saved[5]);if(!c)c=kbu_set(io,KBU_SE,0x48,saved[4]);
   /* TX watermark is level-sensitive while its temporary threshold is2.
    * Restore ordinary config/clocks BEFORE the final acknowledgement.
    * Clear secondary before its primary summary, verify both pending
    * words, then restore aggregate CPU routing last. Do not report a
    * clean restore with an interrupt that immediately reasserts. */
   for(unsigned i=0;i<2&&!c;++i){uint32_t status;uint32_t off=i?0x610:0x640;
    c=kbu_read(io,KBU_SE,off,&status);if(!c)c=kbu_write(io,KBU_SE,i?0x618:0x648,status);
   }
   for(unsigned i=0;i<2&&!c;++i){uint32_t status;c=kbu_read(io,KBU_SE,i?0x610:0x640,&status);if(!c&&status)c=EIO;}
   if(!c)c=kbu_set(io,KBU_SE,0xe1c,saved[0]);
  }
  if(io->owner->default_state==2&&!c)c=EIO;
  out->cleanup_error=c;out->restored=!c;if(!e)e=c;
 }
 #undef KBU_SET
done:
 out->error=e;return e;
}

#define printf(...) host_log(__VA_ARGS__)
static struct { vm_offset_t se,pll,root,vote;bool dead;struct kbu_owner owner;unsigned default_writes; } kbu_native;
/* Fixed, cache-line-contained last-transaction record. A NoC failure can
 * remove USB before its TX ring drains. Crashdump recovery reads only this
 * named record and the console ring after verifying the loaded image header;
 * it never needs a full RAM dump or key-bearing memory. */
static volatile struct {
 uint64_t sequence,completed,mapped_address;
 uint32_t space,offset,value,write,phase,frames,tx,rx;
 int32_t error;
} kbu_trace __attribute__((aligned(64)));
_Static_assert(sizeof(kbu_trace)==64,"last-MMIO trace must fit one cache line");
static void
kbu_trace_publish(void)
{
 /* SM8250's qualified data cache line is64 bytes, this record is64 aligned. */
 barrier();
 barrier();
}
static void
kbu_trace_begin(void *ctx,enum kbu_space space,uint32_t off,uint32_t value,bool write)
{
 const struct kbu_result *out=(const struct kbu_result *)ctx;
 ++kbu_trace.sequence;kbu_trace.mapped_address=0;kbu_trace.space=space;kbu_trace.offset=off;kbu_trace.value=value;kbu_trace.write=write;kbu_trace.error=0;
 kbu_trace.phase=out?out->phase:0;kbu_trace.frames=out?out->frames:0;kbu_trace.tx=out?out->tx:0;kbu_trace.rx=out?out->rx:0;
 kbu_trace_publish();
}
static void
kbu_trace_end(int error,uint32_t value)
{
 kbu_trace.error=error;kbu_trace.value=value;kbu_trace.completed=kbu_trace.sequence;kbu_trace_publish();
}

/* All mappings and I/O require the shared GENI transaction mutex. The
 * bounded explicit bootstrap test holds it through restore. No automatic
 * background poller/long-lived receive path or userspace API is published. */
static int
kbu_native_address(enum kbu_space space,uint32_t off,bool write,vm_offset_t *address)
{
 if(!kbu_allowed(space,off,write))return EINVAL;
 vm_offset_t *map;uint32_t pa,base,size;
 if(space==KBU_SE){map=&kbu_native.se;pa=0x00998000;base=0;size=0x4000;}
 else if(off<0x1000){map=&kbu_native.pll;pa=0x00100000;base=0;size=0x1000;}
 else if(off==0x52018||off==0x52008){map=&kbu_native.vote;pa=0x00152000;base=0x52000;size=0x1000;}
 else {map=&kbu_native.root;pa=0x00117000;base=0x17000;size=0x1000;}
 if(off<base||off-base>size-4)return EINVAL;
 if(!*map)*map=ml_io_map(pa,size);if(!*map)return ENOMEM;
 *address=*map+off-base;return 0;
}
static int
kbu_native_read(void *ctx,enum kbu_space space,uint32_t off,uint32_t *v)
{
 if(!v)return EINVAL;kbu_trace_begin(ctx,space,off,0,false);vm_offset_t addr;int e=kbu_native_address(space,off,false,&addr);if(e){kbu_trace_end(e,0);return e;}kbu_trace.mapped_address=addr;kbu_trace_publish();
 barrier();*v=*(volatile uint32_t *)addr;
 barrier();kbu_trace_end(0,*v);return 0;
}
static int
kbu_native_write(void *ctx,enum kbu_space space,uint32_t off,uint32_t v)
{
 kbu_trace_begin(ctx,space,off,v,true);
 /* Refuse an accidental repeat as a real error, never synthesize a write. */
 if(space==KBU_SE&&off==0x20&&kbu_native.default_writes){kbu_trace_end(EALREADY,v);return EALREADY;}
 vm_offset_t addr;int e=kbu_native_address(space,off,true,&addr);if(e){kbu_trace_end(e,v);return e;}kbu_trace.mapped_address=addr;kbu_trace_publish();
 barrier();*(volatile uint32_t *)addr=v;
 barrier();if(space==KBU_SE&&off==0x20)++kbu_native.default_writes;kbu_trace_end(0,v);return 0;
}
static void kbu_native_wait(void *ctx,uint32_t us){(void)ctx;delay(us);}
static uint64_t
kbu_native_now(void *ctx)
{
 (void)ctx;uint64_t ticks,ns;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns/1000;
}
void
kebab_bluetooth_uart_loopback(void)
{
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled()){
  printf("KEBAB BTUART: end error=%d unsafe-context; no register access\n",EBUSY);return;
 }
 /* This destructive diagnostic is a post-BSD console operation. USB can
  * enumerate while platform/autoconfiguration is still in progress. The
  * real PID1 pointer is published by bsd_utaskbootstrap after that setup;
  * reject before any clock, mapping or mutex work until it exists. The
  * transport primitives themselves remain usable by a future owned driver. */
 if(__atomic_load_n(&initproc,__ATOMIC_ACQUIRE)==PROC_NULL){
  printf("KEBAB BTUART: end error=%d BSD startup incomplete; no register access\n",EAGAIN);return;
 }
 /* Existing acquisition owns only SE6/AHB votes and reports its real result.
  * Recheck branch accessibility inside our transaction before touching SE6. */
 kebab_bluetooth_clock_prepare();
 struct kbu_result out;memset(&out,0,sizeof(out));int e=EBUSY;
 const struct kbu_io io={kbu_native_read,kbu_native_write,kbu_native_wait,kbu_native_now,&out,&kbu_native.owner};
 kebab_geni_transaction_lock();
 if(!kbu_native.dead)e=kbu_loopback(&io,&out);
 if(out.changed&&!out.restored)kbu_native.dead=true;
 bool dead=kbu_native.dead;kebab_geni_transaction_unlock();
 printf("KEBAB BTUART: phase=%u frames=%u tx=%u rx=%u irq=%08x/%08x changed=%u restored=%u cleanup=%d quarantined=%u\n",out.phase,out.frames,out.tx,out.rx,out.mp,out.sp,out.changed,out.restored,out.cleanup_error,dead);
 printf("KEBAB BTUART: port default-state=%u actual-default-writes=%u\n",kbu_native.owner.default_state,kbu_native.default_writes);
 printf("KEBAB BTUART: preflight route=%08x gsi=%08x dma=%08x blocked=%03x value=%08x\n",out.route,out.gsi,out.dma,out.blocked_offset,out.blocked_value);
 printf("KEBAB BTUART: LUCID mode=%08x L=%08x user=%08x opmode=%08x alpha=%08x vote=%08x\n",out.pll[0],out.pll[1],out.pll[2],out.pll[3],out.pll[4],out.pll[5]);
 printf("KEBAB BTUART: end error=%d internal-SE6-only; no controller/HCI/RF qualification\n",e);
}

#undef printf

int main(){
 for(unsigned context=0;context<4;++context){
  panic_flag=context==0;irq=context==1;preempt=context==2;interrupts=context!=3;
  kebab_bluetooth_uart_loopback();assert(!clocks&&!maps&&!barriers&&!locked);++cases;
 }
 panic_flag=irq=preempt=false;interrupts=true;
 initproc=nullptr;kebab_bluetooth_uart_loopback();assert(!clocks&&!maps&&!barriers&&!locked);++cases;initproc=(void *)1;
 kebab_geni_transaction_lock();
 for(unsigned off=0;off<0x53000;off+=4){
  vm_offset_t addr=0;unsigned before=maps;
  // GCC writes limited to this child RCG; PLL and all vote words are RO.
  int e=kbu_native_address(KBU_GCC,off,true,&addr);
  assert((e==0)==(off>=0x17730&&off<=0x17740));if(e)assert(maps==before);++cases;
 }
 assert(!kbu_native_write(nullptr,KBU_SE,0x700,0x78563412)&&se[0x700/4]==0x78563412);++cases;
 assert(kbu_native_write(nullptr,KBU_SE,0x780,0)==EINVAL);++cases;
 assert(kbu_native_write(nullptr,KBU_GCC,0x40,0)==EINVAL);++cases;
 assert(kbu_native_write(nullptr,KBU_GCC,0x52008,0)==EINVAL);++cases;
 uint32_t v=0;assert(kbu_native_read(nullptr,KBU_SE,0x700,&v)==EINVAL);++cases;
 assert(kbu_native_read(nullptr,KBU_SE,0x40,nullptr)==EINVAL);++cases;
 for(unsigned off:{0u,4u,0xcu,0x38u,0x40u}){pll[off/4]=off+9;assert(!kbu_native_read(nullptr,KBU_GCC,off,&v)&&v==off+9);++cases;}
 vote[0x18/4]=1;assert(!kbu_native_read(nullptr,KBU_GCC,0x52018,&v)&&v==1);++cases;
 assert(sizeof(kbu_trace)==64&&((uintptr_t)&kbu_trace%64)==0);++cases;
 kbu_result out={};out.phase=4;out.frames=3;out.tx=7;out.rx=5;
 unsigned before_barrier=barriers;uint64_t sequence=kbu_trace.sequence;
 assert(!kbu_native_write(&out,KBU_SE,0x700,0x12345678));
 assert(kbu_trace.sequence==sequence+1&&kbu_trace.completed==kbu_trace.sequence&&kbu_trace.error==0);
 assert(kbu_trace.write==1&&kbu_trace.offset==0x700&&kbu_trace.value==0x12345678&&kbu_trace.mapped_address==(uintptr_t)se.data()+0x700&&barriers-before_barrier==8);
 assert(kbu_trace.phase==4&&kbu_trace.frames==3&&kbu_trace.tx==7&&kbu_trace.rx==5);++cases;
 assert(!kbu_native_read(&out,KBU_SE,0x40,&v)&&!kbu_trace.write&&kbu_trace.offset==0x40&&kbu_trace.completed==kbu_trace.sequence);++cases;
 assert(kbu_native_write(&out,KBU_GCC,0x40,1)==EINVAL&&kbu_trace.error==EINVAL&&kbu_trace.completed==kbu_trace.sequence&&!kbu_trace.mapped_address);++cases;
 assert(!kbu_native_write(nullptr,KBU_SE,0x20,1)&&kbu_native.default_writes==1);++cases;
 se[0x20/4]=0x55;
 assert(kbu_native_write(nullptr,KBU_SE,0x20,1)==EALREADY&&kbu_native.default_writes==1&&se[0x20/4]==0x55);++cases;
 assert(kbu_native_now(nullptr)==42);++cases;
 kebab_geni_transaction_unlock();
 printf("PASS %u native mapping/context cases; MMIO, clock and barrier stand-ins\n",cases);
}
