
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <map>
#include <deque>
#include <vector>
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

struct Model {
 kbu_owner owner={0};unsigned default_writes=0,default_position=0;
 std::map<unsigned,uint32_t> se,gcc,original_se,original_gcc;
 std::deque<uint8_t> tx,rx;
 uint64_t time=0;unsigned tx_left=0,reads=0,writes=0,waits=0,fail_read=0,fail_write=0;
 unsigned cancel_delay=0,cancel_pending=0;bool stuck_irq=false,cancel_retains=false,apply_failed=false,stuck_root=false,stuck_tx=false,stuck_stop=false,frozen=false,corrupt=false,bad_rx=false,irq_error=false;
 Model(){
  gcc={{0,0x80000001},{4,31},{0xc,0x100},{0x38,1},{0x40,0x4000},{0x52018,1},
       {0x52008,0x100c0},{0x17004,0},{0x17008,0x80000002},{0x1772c,0},
       {0x17730,0},{0x17734,0x100000},{0x17738,0},{0x1773c,0},{0x17740,0}};
  for(unsigned off:kbu_config)se[off]=0;
  se[0x28]=0x15;se[0xe30]=0x80;se[0x48]=0x11;
  se[0x68]=se[0x6c]=0x203;se[0xe24]=0x20102864;se[0xe28]=0x20204800;
  original_se=se;original_gcc=gcc;
 }
 void progress(){
  if(stuck_tx||tx.empty())return;
  // Explicitly model UART internal loopback, not a Bluetooth controller.
  assert(se[0x22c]==1&&se[0x25c]==2&&se[0x268]==8&&se[0x28c]==8);
  assert(se[0x260]==0x4380e&&se[0x264]==0xc3e0e&&se[0x284]==0x4380e&&se[0x288]==0xc3e0e);
  assert(se[0x48]==0x41&&se[0x4c]==0x41&&se[0xe1c]==0&&se[0xe18]==0);
  assert((gcc[0x17734]&0x10371f)==0x102601&&gcc[0x17738]==384&&gcc[0x1773c]==0xc476&&gcc[0x17740]==0xc2f6);
  assert(se[0x40]&(1u<<12));
  rx.push_back(tx.front()^(corrupt?1:0));tx.pop_front();assert(rx.size()<=128);
  if(tx.empty()&&!tx_left){se[0x40]&=~1u;se[0x610]|=1;}
 }
 bool restored(){
  for(unsigned off:kbu_config)if(se[off]!=original_se[off])return false;
  for(unsigned off:{0x17734u,0x17738u,0x1773cu,0x17740u})if(gcc[off]!=original_gcc[off])return false;
  return owner.default_state!=2&&!(se[0x40]&0x1001)&&tx.empty()&&rx.empty()&&!se[0x610]&&!se[0x640];
 }
};
static int rd(void *ctx,kbu_space space,uint32_t off,uint32_t *v){
 auto &m=*(Model *)ctx;++m.reads;assert(kbu_allowed(space,off,false));
 if(m.reads==m.fail_read)return EIO;
 if(space==KBU_GCC){assert(m.gcc.count(off));if(off==0x17730&&!m.stuck_root)m.gcc[off]&=~1u;*v=m.gcc[off];return 0;}
 if(off==0x800){*v=(m.tx.size()+3)/4;return 0;}
 if(off==0x804){*v=(m.rx.size()+3)/4;if(m.rx.size()%4)*v|=0x80000000u|((m.rx.size()%4)<<28);if(m.bad_rx)*v=33;return 0;}
 if(off==0x780){assert(!m.rx.empty());*v=0;for(unsigned i=0;i<4&&!m.rx.empty();++i){*v|=(uint32_t)m.rx.front()<<(8*i);m.rx.pop_front();}return 0;}
 *v=m.se[off];if(m.irq_error&&off==0x640&&(m.se[0x40]&1))*v|=1u<<10;return 0;
}
static int wr(void *ctx,kbu_space space,uint32_t off,uint32_t v){
 auto &m=*(Model *)ctx;++m.writes;assert(kbu_allowed(space,off,true));
 bool failed=m.writes==m.fail_write;if(failed&&!m.apply_failed)return EIO;
 if(space==KBU_GCC){assert(off>=0x17730&&off<=0x17740);m.gcc[off]=v;return failed?EIO:0;}
 switch(off){
 case 0x618:m.se[0x610]&=~v;if((m.se[0x80c]||m.stuck_irq)&&m.tx.empty())m.se[0x610]|=1u<<30;break;
 case 0x648:m.se[0x640]&=~v;break;
 case 0x600:assert(v==(1u<<27));assert(!(m.se[0x40]&1));m.se[0x40]|=1;m.tx_left=m.se[0x270];break;
 case 0x630:assert(v==((1u<<27)|0x80u));m.se[0x40]|=1u<<12;break;
 case 0x604:if(!m.stuck_stop){m.se[0x40]&=~1u;if(!m.cancel_retains)m.tx.clear();m.tx_left=0;m.se[0x610]|=0x10;}break;
 case 0x634:if(!m.stuck_stop){m.se[0x40]&=~(1u<<12);if(v==4&&m.cancel_delay)m.cancel_pending=m.cancel_delay;else {m.cancel_pending=0;m.se[0x640]|=v==4?0x10:0x20;}}break;
 case 0x700:{assert(m.tx.size()<=60&&m.tx_left);unsigned n=m.tx_left<4?m.tx_left:4;for(unsigned i=0;i<n;++i)m.tx.push_back(v>>(8*i));m.tx_left-=n;break;}
 case 0x20:assert(v==1&&++m.default_writes==1&&m.owner.default_state==2);m.default_position=m.writes;break;
 default:if((off==0x48||off==0x4c)&&v==0)assert(!m.cancel_pending&&!(m.se[0x640]&0x30));m.se[off]=v;break;
 }
 return failed?EIO:0;
}
static void wait_us(void *ctx,uint32_t us){auto &m=*(Model *)ctx;assert(us==1);++m.waits;if(m.cancel_pending&&!--m.cancel_pending)m.se[0x640]|=0x10;if(!m.frozen)++m.time;if(m.waits%5==0)m.progress();}
static uint64_t now_us(void *ctx){return ((Model *)ctx)->time;}
static kbu_io ops(Model &m){return {rd,wr,wait_us,now_us,&m,&m.owner};}
static unsigned cases;
int main(){
 Model good;auto io=ops(good);kbu_result out;
 // Actual bootloader FSM state: ACTIVE+LOCK with OPMODE STANDBY.
 Model fsm;fsm.gcc[0]=0xd810003d;fsm.gcc[0xc]=0x103;fsm.gcc[0x38]=0;auto f=ops(fsm);
 assert(!kbu_loopback(&f,&out)&&out.frames==9&&fsm.restored());++cases;
 fsm=Model();fsm.gcc[0]=0x9810003d;fsm.gcc[0x38]=0;f=ops(fsm);
 assert(kbu_loopback(&f,&out)==EPROTONOSUPPORT&&!fsm.writes);++cases;

 assert(kbu_loopback(&io,&out)==0);assert(out.frames==9&&out.tx==1225&&out.rx==1225&&out.changed&&out.restored&&good.restored());++cases;
 unsigned writes=good.writes,reads=good.reads;
 assert(kbu_loopback(&io,&out)==0&&out.frames==9&&good.restored());++cases;
 assert(good.owner.default_state==1&&good.default_writes==1);++cases;
 // Actual parent validation: each unrelated/mismatched PLL must cause no write.
 for(unsigned off:{0u,4u,0xcu,0x38u,0x40u,0x52018u}){Model m;m.gcc[off]=0;auto x=ops(m);assert(kbu_loopback(&x,&out)==EPROTONOSUPPORT&&!m.writes&&!out.changed);++cases;}
 for(unsigned off:{0x52008u,0x17004u,0x17008u,0x1772cu}){Model m;m.gcc[off]=off==0x52008?0:0x80000000;auto x=ops(m);assert(kbu_loopback(&x,&out)==EAGAIN&&!m.writes);++cases;}
 for(unsigned off:{0x40u,0x800u,0x804u,0x610u,0x640u,0xe18u,0x258u}){
  Model m;m.se[off]=1;if(off==0x800)m.tx={1};if(off==0x804)m.rx={1};auto x=ops(m);assert(kbu_loopback(&x,&out)==EBUSY&&!m.writes);++cases;
 }
 for(unsigned off:{0x68u,0x6cu,0xe24u,0xe28u}){Model m;m.se[off]=0;auto x=ops(m);assert(kbu_loopback(&x,&out)==EPROTONOSUPPORT&&!m.writes);++cases;}
 for(unsigned applied=0;applied<2;++applied)for(unsigned failure=1;failure<=writes;++failure){
  Model m;m.fail_write=failure;m.apply_failed=applied;auto x=ops(m);assert(kbu_loopback(&x,&out)!=0);
  if(out.restored)assert(m.restored());assert(!out.changed||out.restored||out.cleanup_error);++cases;
 }
 // Reads are injected across acquisition, transfer and restoration. A few
 // status polls may recover only if the failed operation is not ignored.
 for(unsigned failure=1;failure<=reads;failure+=13){Model m;m.fail_read=failure;auto x=ops(m);assert(kbu_loopback(&x,&out)!=0);if(out.restored)assert(m.restored());++cases;}
 for(unsigned mode=0;mode<6;++mode){
  Model m;m.stuck_root=mode==0;m.stuck_tx=mode==1||mode==2||mode==3;m.frozen=mode==2;m.stuck_stop=mode==3;m.corrupt=mode==4;m.irq_error=mode==5;
  auto x=ops(m);assert(kbu_loopback(&x,&out)!=0);if(out.restored)assert(m.restored());if(mode==3)assert(!out.restored&&out.cleanup_error);assert(m.waits<=104000);++cases;
 }
 Model routed;routed.se[0xe1c]=0xf;routed.original_se=routed.se;auto rt=ops(routed);
 assert(!kbu_loopback(&rt,&out)&&routed.restored()&&routed.se[0xe1c]==0xf);++cases;
 Model unknown;unknown.se[0xe1c]=0x10;auto un=ops(unknown);
 assert(kbu_loopback(&un,&out)==EBUSY&&!unknown.writes&&out.blocked_offset==0xe1c);++cases;
 Model pending;pending.stuck_irq=true;auto pi=ops(pending);
 assert(kbu_loopback(&pi,&out)!=0&&!out.restored&&out.cleanup_error);++cases;
 Model delayed;delayed.cancel_delay=3;auto dl=ops(delayed);
 assert(!kbu_loopback(&dl,&out)&&delayed.restored()&&!delayed.cancel_pending);++cases;
 for(unsigned applied=0;applied<2;++applied){
  Model uncertain;uncertain.fail_write=good.default_position;uncertain.apply_failed=applied;auto un=ops(uncertain);
  assert(kbu_loopback(&un,&out)!=0&&!out.restored&&uncertain.owner.default_state==2);
  unsigned rr=uncertain.reads,ww=uncertain.writes;
  assert(kbu_loopback(&un,&out)==EBUSY&&uncertain.reads==rr&&uncertain.writes==ww);++cases;
 }
 Model unsafe;unsafe.stuck_tx=true;unsafe.cancel_retains=true;auto u=ops(unsafe);
 assert(kbu_loopback(&u,&out)!=0&&!out.restored&&out.cleanup_error&&!unsafe.tx.empty());++cases;
 Model disabled;disabled.se[0x64]=1;auto dd=ops(disabled);assert(kbu_loopback(&dd,&out)==EPROTONOSUPPORT&&!disabled.writes);++cases;
 Model m;auto x=ops(m);m.bad_rx=true;assert(kbu_loopback(&x,&out)!=0);++cases;
 // The private byte reader must retain the rest of a FIFO word across calls.
 m=Model();m.rx={0,1,2,3,4,5,6};kbu_rx stash={0,0};uint8_t dst[7];
 for(unsigned i=0;i<7;++i){unsigned n;assert(!kbu_receive(&x,&stash,dst+i,1,&n)&&n==1&&dst[i]==i);}assert(!stash.remaining&&m.rx.empty());++cases;
 assert(!kbu_allowed(KBU_GCC,0x40,true)&&!kbu_allowed(KBU_GCC,0x52018,true)&&!kbu_allowed(KBU_SE,0x780,true));++cases;
 printf("PASS %u production UART cases; host MMIO/FIFO model, no physical/controller/RF claim\n",cases);
}
