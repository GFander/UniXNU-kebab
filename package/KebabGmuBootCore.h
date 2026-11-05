/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_BOOT_CORE_H
#define KEBAB_GMU_BOOT_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabGpuCcCore.h"
enum {KGB_EMPTY=0,KGB_CLOCKS_READY=1,KGB_RESET_ASSERTED=2,KGB_RESET_HELD_TCM=3,
 KGB_FW_RUNNING=4,KGB_HFI_QUEUES_READY=5,KGB_QUARANTINED=6,KGB_FW_STARTING=7};
enum {KGB_NEED_RSC=1,KGB_NEED_BANDWIDTH=2,KGB_NEED_IRQ=4};
/* Explicit adaptation for the SHA-bound OTA HFI2.0.1 image: fixed vendor
 * kgsl_hfi.h uses bit15. Upstream's bit23 is a different firmware profile. */
enum {KGB_OTA_HFI_CM3_FAULT=0x8000};
enum {KGB_BW_REQUIRE_QUALIFIED=0,KGB_BW_RETAIN_INHERITED_NO_WRITE=1};
enum {KGB_GCC=0,KGB_CC=1,KGB_GMU=2,KGB_RSCC=3};
typedef struct {
 void *cookie;
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t,uint32_t,uint32_t);
 int (*barrier)(void *);int (*delay_us)(void *,uint32_t);uint64_t (*now_us)(void *);
 /* Concrete canonical helper, called under its existing globalbusy. */
 int (*mark_started)(void *);
 /* Concrete mapper opens the two finite TCM/CSR windows only after clocks. */
 int (*open_gmu)(void *);
 int (*source_check)(void *);
 int (*tcm_word)(void *,uint32_t,uint32_t *,uint32_t *);
} KebabGmuBootIo;
typedef struct {
 uint32_t phase,attempted,pinned,quarantined,started,reset_issued,tcm_verified,added_mask;
 uint32_t control[3],root_cfg,root_cmd,tcm_crc;
 uint64_t namespace_epoch;
} KebabGmuBootLedger;
typedef struct {
 uint32_t magic,schema,bytes,result,phase,action,primary_error,missing_owners;
 uint32_t reads,writes,barriers,polls,delay_us,failed_region,failed_offset,io_error;
 uint32_t before_valid,after_valid,before[3],after[3],added_mask,mark_started,reset_write_issued,reset_readback;
 uint32_t reset_physical_ack,tcm_written_words,tcm_verified_words,tcm_crc,read_crc,failed_word;
 uint32_t firmware_start_requested,fw_init_fresh_zero,fw_init_result,fw_init_ack,hfi_status,hfi_ctrl_ack;
 uint32_t rsc_ack,rsc_busy,irq_ao_status,irq_hfi_status,root_children,parent_refs,owner_dead,quarantined;
 uint32_t hfi_messages_initialized,gx_qualified,dma_proven,retained_bandwidth;
 uint32_t evidence_replayed,root_qualified,source_qualified,child_pinned;
 uint32_t bandwidth_qualified;
 uint64_t namespace_epoch,elapsed_us;
} KebabGmuBootRecord;
typedef struct {const KebabGmuBootIo *io;KebabGmuBootLedger *ledger;KebabGmuBootRecord *r;
 uint32_t gcc_cfg,gcc_reset,gdscr,ahb,gx_gmu,memnoc;uint64_t started_us;} KebabGmuBootContext;
static inline void KebabGmuBootInit(KebabGmuBootRecord *r,unsigned action)
{memset(r,0,sizeof *r);r->magic=0x424d474b;r->schema=1;r->bytes=sizeof *r;r->action=action;r->result=95;r->failed_word=UINT32_MAX;}
static inline int KebabGmuBootRunning(uint32_t v)
{return !(v&0x80000000u) || (v&0x70000000u)==0x20000000u;}
static inline uint32_t KebabGmuBootCrcWord(uint32_t crc,uint32_t word)
{for(unsigned b=0;b<4;b++){crc^=(word>>(8*b))&255;for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1))&0xedb88320u);}return crc;}
static inline int KebabGmuBootRaw(KebabGmuBootContext *c,unsigned region,uint32_t off,uint32_t *v)
{int e;++c->r->reads;e=c->io->read32(c->io->cookie,region,off,v);if(e){c->r->failed_region=region;c->r->failed_offset=off;c->r->io_error=e;return 5;}return 0;}
static inline int KebabGmuBootBarrier(KebabGmuBootContext *c)
{int e;++c->r->barriers;e=c->io->barrier(c->io->cookie);if(e){c->r->io_error=e;return 5;}return 0;}
static inline int KebabGmuBootDelay(KebabGmuBootContext *c,uint32_t us)
{int e;if(us!=1 && us!=100)return 22;c->r->delay_us+=us;e=c->io->delay_us(c->io->cookie,us);if(e){c->r->io_error=e;return 5;}return 0;}
static inline int KebabGmuBootClockGates(KebabGmuBootContext *c,unsigned branches)
{
 uint32_t v;int e;
 if((e=KebabGmuBootRaw(c,0,0x71004,&v)))return e;if(v!=c->gcc_cfg || !(v&1) || (v&0x80000000u))return 11;
 if((e=KebabGmuBootRaw(c,0,0x71000,&v)))return e;if(v!=c->gcc_reset || (v&1))return 11;
 if((e=KebabGmuBootRaw(c,1,0x106c,&v)))return e;if((v&KebabGpuCcControlMask(2))!=c->gdscr)return 11;
 if((e=KebabGmuBootRaw(c,1,0x1540,&v)))return e;if(!(v&0x80000000u))return 11;
 if((e=KebabGmuBootRaw(c,1,0x1078,&v)))return e;if((v&0x0fffffffu)!=c->ahb || !(v&1))return 11;
 if((e=KebabGmuBootRaw(c,0,0x7100c,&v)))return e;if((v&0x0fffffffu)!=c->memnoc || !(v&1) || !KebabGmuBootRunning(v))return 11;
 if((e=KebabGmuBootRaw(c,1,0x1064,&v)))return e;
 if((v&0x0fffffffu)!=c->gx_gmu || (!c->r->firmware_start_requested && (v&0x80000001u)!=0x80000000u))return 11;
 if((e=KebabGmuBootRaw(c,1,0x1120,&v)))return e;if((v&0x800000f3u)!=2)return 11;
 if((e=KebabGmuBootRaw(c,1,0x1124,&v)))return e;if((v&0x10371fu)!=0x100602u || (v&0x3000u))return 11;
 if(branches){
  const unsigned reg[3]={0,1,1};const uint32_t off[3]={0x71154,0x109c,0x1098};
  for(unsigned i=0;i<3;i++){if((e=KebabGmuBootRaw(c,reg[i],off[i],&v)))return e;
   if((v&0x0fffffffu)!=c->ledger->control[i] || !(v&1) || (!KebabGmuBootRunning(v) && !(i==2 && c->r->firmware_start_requested)))return 11;}
 }
 return KebabGmuBootBarrier(c);
}
static inline int KebabGmuBootWrite(KebabGmuBootContext *c,unsigned region,uint32_t off,uint32_t mask,uint32_t bits,uint32_t expected)
{
 int e;/* All successful writes have already passed same-owner gate. */
 ++c->r->writes;e=c->io->write32(c->io->cookie,region,off,mask,bits,expected);
 if(e){c->r->failed_region=region;c->r->failed_offset=off;c->r->io_error=e;return 5;}return KebabGmuBootBarrier(c);
}
static inline int KebabGmuBootEnable(KebabGmuBootContext *c,unsigned which)
{
 const unsigned reg[3]={0,1,1};const uint32_t off[3]={0x71154,0x109c,0x1098};uint32_t v;int e;
 if(which>2)return 22;if((e=KebabGmuBootClockGates(c,0)))return e;
 if((e=KebabGmuBootRaw(c,reg[which],off[which],&v)))return e;
 if(((v^c->r->before[which])&0x0fffffffu)!=0)return 11;
 c->ledger->control[which]=(v|1)&0x0fffffffu;
 if(which==2){e=c->io->mark_started(c->io->cookie);if(e){c->r->io_error=e;return 95;}c->ledger->started=1;c->r->mark_started=1;}
 if(!(v&1)){
  c->ledger->added_mask|=1u<<which;c->r->added_mask=c->ledger->added_mask;
  if((e=KebabGmuBootWrite(c,reg[which],off[which],1,1,v)))return e;
 }
 for(unsigned n=0;n<200;n++){
  if((e=KebabGmuBootClockGates(c,0)))return e;
  if((e=KebabGmuBootRaw(c,reg[which],off[which],&v)))return e;c->r->polls++;
  if((v&0x0fffffffu)!=c->ledger->control[which])return 11;
  if((v&1) && KebabGmuBootRunning(v)){c->r->after[which]=v;c->r->after_valid|=1u<<which;return 0;}
  if((e=KebabGmuBootDelay(c,1)))return e;
 }
 return 110;
}
static inline int KebabGmuBootLoadTcm(KebabGmuBootContext *c)
{
 uint32_t off,value,actual,crc=~0u,read_crc=~0u;int e;
 for(unsigned pass=0;pass<2;pass++)for(uint32_t i=0;i<8192;i++){
  if(!(i&63)){
   if((e=KebabGmuBootClockGates(c,1)))return e;
   if((e=KebabGmuBootRaw(c,2,0x14000,&actual)))return e;
   if(!(actual&1))return 11;
   uint64_t now=c->io->now_us(c->io->cookie);if(now<c->started_us || now-c->started_us>500000)return 110;
  }
  e=c->io->tcm_word(c->io->cookie,i,&off,&value);if(e){c->r->io_error=e;c->r->failed_word=i;return 5;}
  if(off!=(i<4096?0x3000+4*i:0x7000+4*(i-4096))){c->r->failed_word=i;return 22;}
  if(!pass){
   ++c->r->writes;e=c->io->write32(c->io->cookie,2,off,UINT32_MAX,value,0);
   if(e){c->r->io_error=e;c->r->failed_word=i;return 5;}c->r->tcm_written_words++;crc=KebabGmuBootCrcWord(crc,value);
   if((i&63)==63 && (e=KebabGmuBootBarrier(c)))return e;
  }else{
   if((e=KebabGmuBootRaw(c,2,off,&actual))){c->r->failed_word=i;return e;}
   if(actual!=value){c->r->failed_word=i;return 11;}c->r->tcm_verified_words++;read_crc=KebabGmuBootCrcWord(read_crc,actual);
  }
 }
 c->r->tcm_crc=~crc;c->r->read_crc=~read_crc;if(crc!=read_crc)return 11;
 c->ledger->tcm_verified=1;c->ledger->tcm_crc=~crc;return KebabGmuBootBarrier(c);
}
static inline int KebabGmuBootResetAndLoad(KebabGmuBootContext *c)
{
 const unsigned reg[3]={0,1,1};const uint32_t off[3]={0x71154,0x109c,0x1098};uint32_t v;int e;
 if(c->ledger->attempted)return 95;
 for(unsigned i=0;i<3;i++){if((e=KebabGmuBootRaw(c,reg[i],off[i],&c->r->before[i])))return e;c->r->before_valid|=1u<<i;}
 if((c->r->before[2]&0x80000001u)!=0x80000000u)return 95;
 if((e=KebabGmuBootClockGates(c,0)))return e;c->ledger->attempted=1;
 /* AHB and MEMNOC are already genuinely held by CC/bus; AXI/CXO next,
  * GMU1098 is the last producer clock. No optional trace vote or111c. */
 for(unsigned i=0;i<3;i++)if((e=KebabGmuBootEnable(c,i)))return e;
 c->ledger->phase=KGB_CLOCKS_READY;if((e=KebabGmuBootClockGates(c,1)))return e;
 if((e=c->io->open_gmu(c->io->cookie))){c->r->io_error=e;return 5;}
 if((e=KebabGmuBootWrite(c,2,0x14000,UINT32_MAX,1,0)))return e;
 c->ledger->reset_issued=1;c->r->reset_write_issued=1;
 if((e=KebabGmuBootRaw(c,2,0x14000,&v)))return e;c->r->reset_readback=v;
 if(!(v&1))return 11;/* controller readback, not a fictional physical ACK */
 c->ledger->phase=KGB_RESET_ASSERTED;
 if((e=KebabGmuBootWrite(c,2,0x14730,UINT32_MAX,1,0)))return e;
 if((e=KebabGmuBootRaw(c,2,0x14730,&v)))return e;if(v!=1)return 11;
 if((e=KebabGmuBootLoadTcm(c)))return e;c->ledger->phase=KGB_RESET_HELD_TCM;return 0;
}
/* Next launch stage is fully bounded, but native entry remains closed until
 * actual GPU-local RSC, bandwidth and IRQ owners are integrated. No CAP bool
 * grants them. Queues-ready is NOT HFI messages initialized or GPU ready. */
static inline int KebabGmuBootPoll(KebabGmuBootContext *c,unsigned region,uint32_t off,uint32_t mask,uint32_t desired,uint32_t *last)
{
 int e;uint32_t v;uint64_t poll_start=c->io->now_us(c->io->cookie);
 for(unsigned i=0;i<100;i++){
  uint64_t poll_now=c->io->now_us(c->io->cookie);if(poll_now<poll_start || poll_now-poll_start>=10000)return 110;
  if((e=KebabGmuBootClockGates(c,1)))return e;
  if((e=KebabGmuBootRaw(c,2,0x24c14,&v)))return e;c->r->irq_ao_status=v;if(v&0x29)return 5;
  if((e=KebabGmuBootRaw(c,2,0x14648,&v)))return e;c->r->irq_hfi_status=v;if(v&KGB_OTA_HFI_CM3_FAULT)return 5;
  if((e=KebabGmuBootRaw(c,region,off,&v)))return e;*last=v;c->r->polls++;
  poll_now=c->io->now_us(c->io->cookie);if(poll_now<poll_start || poll_now-poll_start>=10000)return 110;
  if((v&mask)==desired)return 0;if((e=KebabGmuBootDelay(c,100)))return e;
 }return 110;
}
static inline int KebabGmuBootLaunchReady(uint32_t owned_mask,uint32_t chipid,unsigned bw_mode)
{
 return bw_mode<=KGB_BW_RETAIN_INHERITED_NO_WRITE && chipid==0x06050200u &&
  (owned_mask&(KGB_NEED_RSC|KGB_NEED_IRQ))==(KGB_NEED_RSC|KGB_NEED_IRQ) &&
  ((owned_mask&KGB_NEED_BANDWIDTH) || bw_mode==KGB_BW_RETAIN_INHERITED_NO_WRITE);
}
static inline int KebabGmuBootStartFirmwareWithBandwidth(KebabGmuBootContext *c,uint32_t owned_mask,uint32_t chipid,unsigned bw_mode)
{
 uint32_t v;int e;c->r->missing_owners=(KGB_NEED_RSC|KGB_NEED_BANDWIDTH|KGB_NEED_IRQ)&~owned_mask;
 if(!KebabGmuBootLaunchReady(owned_mask,chipid,bw_mode))return 95;
 c->r->bandwidth_qualified=!!(owned_mask&KGB_NEED_BANDWIDTH);
 c->r->retained_bandwidth=!c->r->bandwidth_qualified;
 if(c->ledger->phase!=KGB_RESET_HELD_TCM || !c->ledger->tcm_verified)return 95;
 if((e=KebabGmuBootClockGates(c,1)))return e;
 if((e=KebabGmuBootRaw(c,2,0x14000,&v)))return e;if(!(v&1))return 11;
 if((e=KebabGmuBootRaw(c,2,0x14600,&v)))return e;if(v&1)return 16; /* reject stale queues-ready */
 if((e=KebabGmuBootRaw(c,2,0x24c20,&v)))return e;if(v&2)return 16; /* require fresh RSC ack */
 if((e=KebabGmuBootWrite(c,2,0x24c1c,UINT32_MAX,2,0)))return e;
 if((e=KebabGmuBootPoll(c,2,0x24c20,2,2,&c->r->rsc_ack)))return e;
 if((e=KebabGmuBootPoll(c,3,0x404,UINT32_MAX,0,&c->r->rsc_busy)))return e;
 if((e=KebabGmuBootWrite(c,2,0x24c1c,UINT32_MAX,0,0)))return e;
 const uint32_t init[][2]={{0x143c4,1},{0x143c0,1},{0x14070,0},{0x14004,2},{0x14614,0x60001000},{0x14610,1},
  {0x24c44,0x802800a0},{0x140b4,0x4052},{0x14608,0x06050200},{0x14400,0x60006000},
  {0x1303c,1},{0x13000,1},{0x13004,1},{0x14300,0x09c40400}};
 for(unsigned i=0;i<sizeof(init)/sizeof(init[0]);i++)if((e=KebabGmuBootWrite(c,2,init[i][0],UINT32_MAX,init[i][1],0)))return e;
 if((e=KebabGmuBootRaw(c,2,0x14070,&v)))return e;if(v)return 11;c->r->fw_init_fresh_zero=1;
 if((e=KebabGmuBootRaw(c,2,0x143a0,&v)))return e;
 if((e=KebabGmuBootWrite(c,2,0x143a0,0xf11,0xf11,v)))return e;
 if((e=KebabGmuBootWrite(c,2,0x14404,UINT32_MAX,0,0)))return e;
 c->r->firmware_start_requested=1;c->ledger->phase=KGB_FW_STARTING;
 if((e=KebabGmuBootWrite(c,2,0x14000,UINT32_MAX,0,0)))return e;
 if((e=KebabGmuBootPoll(c,2,0x14070,0x1ff,0x100,&c->r->fw_init_result)))return e;
 c->r->fw_init_ack=1;c->ledger->phase=KGB_FW_RUNNING;
 if((e=KebabGmuBootWrite(c,2,0x14618,UINT32_MAX,1,0)))return e;
 if((e=KebabGmuBootPoll(c,2,0x14600,1,1,&c->r->hfi_status)))return e;
 c->r->hfi_ctrl_ack=1;c->ledger->phase=KGB_HFI_QUEUES_READY;return 0;
}
static inline int KebabGmuBootStartFirmware(KebabGmuBootContext *c,uint32_t owned_mask,uint32_t chipid)
{return KebabGmuBootStartFirmwareWithBandwidth(c,owned_mask,chipid,KGB_BW_REQUIRE_QUALIFIED);}
#endif
