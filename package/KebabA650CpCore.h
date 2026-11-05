/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_A650_CP_CORE_H
#define KEBAB_A650_CP_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "KebabA650CpRegisters.h"
/* Kernel-owned first execution only: one private ring, one immutable IB,
 * fixed NC fence/scratch offsets. No user packets, context switch, IRQ
 * unmask/ACK, reset, TSB/TRUST_CNTL or SMMU write; no guessed recovery.
 * PM4 parity/header and A6xx fence facts follow AdrenoPM4/NativeAdreno (MIT).
 * A separately reviewed actual owner must retain the existing OOB/VM/BOs. */
enum {KCP_EMPTY=0,KCP_FAILED=1,KCP_CPU_READY=2,KCP_HW_INIT=3,KCP_SQE_READY=4,KCP_INIT_DONE=5,KCP_EXECUTED=6,KCP_QUARANTINED=7};
enum {KCP_RING_IOVA=0x90000000u,KCP_IB_IOVA=0x90004000u,KCP_SQE_IOVA=0x90008000u,KCP_FENCE_IOVA=0x90020000u,
 KCP_MARKER=0xa6500c0du,KCP_SENTINEL=0x13579bdfu,KCP_INIT_WPTR=16,KCP_FINAL_WPTR=27};
typedef struct {
 uint32_t magic,schema,bytes,result,phase,primary_error,io_error,touched;
 uint32_t reads,writes,barriers,polls,failed_region,failed_offset,quarantined,held;
 uint32_t firmware_bytes,payload_bytes,firmware_version,firmware_verified,split_copy_verified,commands_cleaned,NC_verified,namespace_unchanged;
 uint32_t SQE_start_requested,CP_ME_INIT_completed,IB_marker_observed,GPU_DMA_observed,init_fence,final_fence,init_rptr,final_rptr;
 uint32_t initial_marker,final_marker,initial_sqe,irq_mask_before,irq_mask_after,SPT_control_before,SPT_control_after,clock_control_after;
 uint32_t raw_before[22],raw_after[22];
 uint32_t ring_init[16],ib[4],ring_second[11],padding;
 uint64_t epoch,elapsed_us;
} KebabA650CpRecord;
#ifdef __cplusplus
static_assert(sizeof(KebabA650CpRecord)==480,"CP event record480");
#endif
typedef struct {uint32_t attempted,quarantined,complete;} KebabA650CpBook;
typedef struct {
 void *cookie;
 int (*gate)(void*),(*read32)(void*,unsigned,uint32_t,uint32_t*),(*write32)(void*,unsigned,uint32_t,uint32_t);
 void (*barrier)(void*),(*delay_us)(void*,unsigned);
 uint64_t (*now_us)(void*);
 /* prepare_cpu validates exact measured firmware and existing private CPU
  * views, loads both RX segments, cleans them, and primes NC memory only.
  * copy_words accepts only ring/IB bounded offsets; publish cleans both.
  * read_nc accepts offsets0(RPTR),4(fence),64(scratch), never table memory. */
 int (*prepare_cpu)(void*,KebabA650CpRecord*);
 int (*copy_words)(void*,unsigned,uint32_t,const uint32_t*,uint32_t);
 int (*publish_commands)(void*),(*read_nc)(void*,uint32_t,uint32_t*);
} KebabA650CpIo;
typedef struct {
 const KebabA650CpIo *io;KebabA650CpBook *book;KebabA650CpRecord *r;
 uint64_t begin,wait_begin;uint32_t waiting;
} KebabA650CpContext;
static inline void KebabA650CpInit(KebabA650CpRecord*r)
{memset(r,0,sizeof *r);r->magic=0x3150434bu;r->schema=2;r->bytes=sizeof *r;}
static inline uint32_t KCPOdd(uint32_t x)
{uint32_t bit=1;while(x){bit^=1;x&=x-1;}return bit;}
static inline uint32_t KCPType4(uint32_t reg,uint32_t count)
{return 0x40000000u|count|(KCPOdd(count)<<7)|(reg<<8)|(KCPOdd(reg)<<27);}
static inline uint32_t KCPType7(uint32_t op,uint32_t count)
{return 0x70000000u|count|(KCPOdd(count)<<15)|(op<<16)|(KCPOdd(op)<<23);}
static inline int KCPClock(KebabA650CpContext*c)
{
 uint64_t now=c->io->now_us(c->io->cookie);
 return now<c->begin || now-c->begin>=500000 || (c->waiting && (now<c->wait_begin || now-c->wait_begin>=100000))?110:0;
}
static inline int KCPGate(KebabA650CpContext*c)
{int e=KCPClock(c);if(e)return e;e=c->io->gate(c->io->cookie);if(e)c->r->io_error=(uint32_t)e;return e?e:KCPClock(c);}
static inline int KCPRead(KebabA650CpContext*c,unsigned space,uint32_t off,uint32_t*out)
{
 int e=KCPGate(c);if(e)return e;c->r->reads++;
 e=c->io->read32(c->io->cookie,space,off,out);
 if(e){c->r->io_error=(uint32_t)e;c->r->failed_region=space;c->r->failed_offset=off;return 5;}return KCPClock(c);
}
static inline int KCPGpuRead(KebabA650CpContext*c,uint32_t word,uint32_t*out)
{return KCPRead(c,0,word*4,out);}
static inline int KCPWrite(KebabA650CpContext*c,unsigned space,uint32_t off,uint32_t value)
{
 int e=KCPGate(c);if(e)return e;c->r->touched=1;c->r->writes++;
 e=c->io->write32(c->io->cookie,space,off,value);
 /* Every attempted store is ordered, even a late gate/IO error. */
 c->r->barriers++;c->io->barrier(c->io->cookie);
 if(e){c->r->io_error=(uint32_t)e;c->r->failed_region=space;c->r->failed_offset=off;return 5;}return KCPGate(c);
}
static inline int KCPGpuWrite(KebabA650CpContext*c,uint32_t word,uint32_t value)
{return KCPWrite(c,0,word*4,value);}
static inline int KCPReadNc(KebabA650CpContext*c,uint32_t off,uint32_t*out)
{
 int e=KCPGate(c);if(e)return e;e=c->io->read_nc(c->io->cookie,off,out);
 if(e){c->r->io_error=(uint32_t)e;return e;}return KCPClock(c);
}
static inline int KCPSnapshot(KebabA650CpContext*c,uint32_t*out)
{
 static const uint32_t words[22]={KCP_RBBM_STATUS,KCP_RBBM_STATUS3,KCP_RBBM_INT_0_STATUS,KCP_RBBM_INT_0_MASK,
 KCP_CP_SQE_CNTL,KCP_CP_RB_RPTR,KCP_CP_RB_WPTR,KCP_CP_HW_FAULT,KCP_CP_INTERRUPT_STATUS,KCP_CP_PROTECT_STATUS,
 KCP_CP_SQE_INSTR_BASE,KCP_CP_SQE_INSTR_BASE+1,KCP_CP_APRIV_CNTL,KCP_CP_RB_CNTL,
 KCP_CP_RB_BASE,KCP_CP_RB_BASE+1,KCP_CP_RB_RPTR_ADDR,KCP_CP_RB_RPTR_ADDR+1,
 KCP_GBIF_HALT,KCP_RBBM_GBIF_HALT,KCP_RBBM_SECVID_TSB_CNTL,KCP_RBBM_SECVID_TSB_TRUSTED_SIZE};
 for(unsigned n=0;n<22;n++){int e=KCPGpuRead(c,words[n],out+n);if(e)return e;}return 0;
}
static inline int KCPProgram(KebabA650CpContext*c,const KCPReg*program,size_t count)
{for(size_t n=0;n<count;n++){int e=KCPGpuWrite(c,program[n].word,program[n].value);if(e)return e;}return 0;}
static inline void KCPFenceWords(uint32_t*out,uint32_t sequence)
{
 out[0]=KCPType4(KCP_CP_SCRATCH2,1);out[1]=sequence;
 out[2]=KCPType7(0x46,4);out[3]=0x80000004u;
 out[4]=KCP_FENCE_IOVA+4;out[5]=0;out[6]=sequence;
}
static inline void KCPMakeCommands(KebabA650CpRecord*r)
{
 r->ring_init[0]=KCPType7(0x48,8);r->ring_init[1]=0x2f;r->ring_init[2]=3;r->ring_init[3]=0x20000000u;
 for(unsigned i=4;i<9;i++)r->ring_init[i]=0;KCPFenceWords(r->ring_init+9,1);
 r->ib[0]=KCPType7(0x3d,3);r->ib[1]=KCP_FENCE_IOVA+64;r->ib[2]=0;r->ib[3]=KCP_MARKER;
 r->ring_second[0]=KCPType7(0x3f,3);r->ring_second[1]=KCP_IB_IOVA;r->ring_second[2]=0;r->ring_second[3]=4;
 KCPFenceWords(r->ring_second+4,2);
}
static inline int KCPPublish(KebabA650CpContext*c)
{int e=KCPGate(c);if(e)return e;e=c->io->publish_commands(c->io->cookie);if(e){c->r->io_error=(uint32_t)e;return e;}c->r->commands_cleaned++;return KCPGate(c);}
static inline int KCPCopy(KebabA650CpContext*c,unsigned region,uint32_t offset,const uint32_t*data,uint32_t words)
{int e=KCPGate(c);if(e)return e;e=c->io->copy_words(c->io->cookie,region,offset,data,words);if(e)c->r->io_error=(uint32_t)e;return e?e:KCPGate(c);}
static inline int KCPWptr(KebabA650CpContext*c,uint32_t value)
{
 int e=KCPGpuWrite(c,KCP_CP_RB_WPTR,value);uint32_t fence=0;if(e)return e;
 /* Never replay a possibly dropped write or clear an unknown fence error. */
 e=KCPRead(c,1,0x24c48,&fence);return e?e:((fence&1u)?5:0);
}
static inline int KCPWait(KebabA650CpContext*c,uint32_t sequence,uint32_t wptr,uint32_t*fence,uint32_t*rptr)
{
 c->wait_begin=c->io->now_us(c->io->cookie);c->waiting=1;int e=110;
 for(unsigned n=0;n<1000;n++){
  uint32_t fault=0,irq=0,status=0;c->r->polls++;
  if((e=KCPReadNc(c,4,fence)) || (e=KCPReadNc(c,0,rptr)) ||
     (e=KCPGpuRead(c,KCP_CP_HW_FAULT,&fault)) || (e=KCPGpuRead(c,KCP_CP_INTERRUPT_STATUS,&irq)))break;
  if(fault || (irq&0xf7u) || *fence>sequence || *rptr>=1024){e=5;break;}
  if(*fence==sequence && *rptr==wptr){
   e=KCPGpuRead(c,KCP_RBBM_STATUS,&status);if(e)break;
   if(!(status&~1u)){e=0;break;}
  }
  c->io->delay_us(c->io->cookie,100);e=KCPClock(c);if(e)break;
  e=110;
 }
 c->waiting=0;return e;
}
#define KCP_TRY(x) do{e=(x);if(e)goto failed;}while(0)
static inline int KebabA650CpFirstExecution(KebabA650CpContext*c,uint32_t qualified,uint64_t epoch)
{
 if(!c || !c->r)return 22;KebabA650CpInit(c->r);KebabA650CpRecord*r=c->r;
 if(!c->book || !c->io || !c->io->gate || !c->io->read32 || !c->io->write32 || !c->io->barrier ||
 !c->io->delay_us || !c->io->now_us || !c->io->prepare_cpu || !c->io->copy_words || !c->io->publish_commands || !c->io->read_nc){r->result=22;r->phase=KCP_FAILED;return 22;}
 if(qualified!=1 || epoch!=1){r->result=95;r->phase=KCP_FAILED;return 95;}
 if(c->book->attempted || c->book->quarantined || c->book->complete){r->result=16;r->phase=KCP_FAILED;return 16;}
 c->book->attempted=1;c->begin=c->io->now_us(c->io->cookie);c->waiting=0;r->epoch=epoch;
 int e=0;uint32_t v=0,high=0,shadow=0,initial_fence=0;
 KCP_TRY(KCPSnapshot(c,r->raw_before));r->initial_sqe=r->raw_before[4];r->irq_mask_before=r->raw_before[3];
 if((r->raw_before[0]&~1u) || r->irq_mask_before || (r->initial_sqe!=0 && r->initial_sqe!=3) ||
 r->raw_before[5] || r->raw_before[6] || r->raw_before[7] || (r->raw_before[8]&0xf7u) ||
 r->raw_before[18] || r->raw_before[19] || r->raw_before[20] || r->raw_before[21]){e=13;goto failed;}
 KCP_TRY(KCPGpuRead(c,KCP_RBBM_SECVID_TSB_TRUSTED_BASE,&v));KCP_TRY(KCPGpuRead(c,KCP_RBBM_SECVID_TSB_TRUSTED_BASE+1,&high));
 if(v || high){e=13;goto failed;}
 KCP_TRY(KCPRead(c,1,0x14340,&v));if(v&0xc0u){e=11;goto failed;}
 KCP_TRY(KCPRead(c,1,0x24c48,&v));if(v&1u){e=5;goto failed;}
 KCP_TRY(KCPGate(c));KCP_TRY(c->io->prepare_cpu(c->io->cookie,r));KCP_TRY(KCPGate(c));
 if(r->firmware_bytes!=31964 || r->payload_bytes!=31960 || r->firmware_version!=0x112 ||
 r->firmware_verified!=1 || r->split_copy_verified!=1 || r->NC_verified!=1){e=13;goto failed;}
 KCP_TRY(KCPReadNc(c,0,&shadow));KCP_TRY(KCPReadNc(c,4,&initial_fence));KCP_TRY(KCPReadNc(c,64,&r->initial_marker));
 if(shadow || initial_fence || r->initial_marker!=KCP_SENTINEL){e=13;goto failed;}
 r->phase=KCP_CPU_READY;KCPMakeCommands(r);
 KCP_TRY(KCPCopy(c,1,0,r->ib,4));KCP_TRY(KCPCopy(c,0,0,r->ring_init,16));KCP_TRY(KCPPublish(c));
 r->phase=KCP_HW_INIT;KCP_TRY(KCPProgram(c,KCPHwPrefix,sizeof KCPHwPrefix/sizeof KCPHwPrefix[0]));
 KCP_TRY(KCPRead(c,1,0x200,&r->SPT_control_before));
 KCP_TRY(KCPWrite(c,1,0x200,r->SPT_control_before&~1u));
 KCP_TRY(KCPProgram(c,KCPHwcg,sizeof KCPHwcg/sizeof KCPHwcg[0]));
 KCP_TRY(KCPWrite(c,1,0x200,r->SPT_control_before|1u));
 /* Linux/vendor RMW bit0 without a sticky-one ACK. Fixed alioth f188
  * reports successful A650 CP with SP0->0 and CLOCK_CNTL0->8aa8aa82.
  * Keep raw SP diagnostics/other bits, require actual top-level readback,
  * fresh GX/power/fault gates and real fences/IB below. All148 stores stay. */
 KCP_TRY(KCPGpuWrite(c,KCP_RBBM_CLOCK_CNTL,0x8aa8aa82));
 KCP_TRY(KCPGpuRead(c,KCP_RBBM_CLOCK_CNTL,&r->clock_control_after));
 KCP_TRY(KCPRead(c,1,0x200,&r->SPT_control_after));
 if(r->clock_control_after!=0x8aa8aa82u){r->failed_offset=KCP_RBBM_CLOCK_CNTL*4;e=5;goto failed;}
 if((r->SPT_control_after^r->SPT_control_before)&~1u){r->failed_region=1;r->failed_offset=0x200;e=5;goto failed;}
 KCP_TRY(KCPRead(c,1,0x14340,&v));if(v&0xc0u){e=11;goto failed;}
 KCP_TRY(KCPProgram(c,KCPHwPostClock,sizeof KCPHwPostClock/sizeof KCPHwPostClock[0]));
 KCP_TRY(KCPGpuWrite(c,KCP_CP_SQE_INSTR_BASE,KCP_SQE_IOVA));KCP_TRY(KCPGpuWrite(c,KCP_CP_SQE_INSTR_BASE+1,0));
 KCP_TRY(KCPGpuRead(c,KCP_CP_SQE_INSTR_BASE,&v));KCP_TRY(KCPGpuRead(c,KCP_CP_SQE_INSTR_BASE+1,&high));
 if(v!=KCP_SQE_IOVA || high){e=5;goto failed;}
 KCP_TRY(KCPGpuWrite(c,KCP_CP_RB_BASE,KCP_RING_IOVA));KCP_TRY(KCPGpuWrite(c,KCP_CP_RB_BASE+1,0));
 KCP_TRY(KCPGpuWrite(c,KCP_CP_RB_CNTL,0x209));
 KCP_TRY(KCPGpuWrite(c,KCP_CP_RB_RPTR_ADDR,KCP_FENCE_IOVA));KCP_TRY(KCPGpuWrite(c,KCP_CP_RB_RPTR_ADDR+1,0));
 KCP_TRY(KCPGpuWrite(c,KCP_CP_SCRATCH2,0));
 KCP_TRY(KCPGpuRead(c,KCP_CP_RB_CNTL,&v));if(v!=0x209){e=5;goto failed;}
 KCP_TRY(KCPGpuWrite(c,KCP_CP_SQE_CNTL,1));r->SQE_start_requested=1;
 KCP_TRY(KCPGpuRead(c,KCP_CP_SQE_CNTL,&v));if((v&3)!=1){e=5;goto failed;}
 r->phase=KCP_SQE_READY;KCP_TRY(KCPWptr(c,KCP_INIT_WPTR));
 KCP_TRY(KCPWait(c,1,KCP_INIT_WPTR,&r->init_fence,&r->init_rptr));r->CP_ME_INIT_completed=1;r->phase=KCP_INIT_DONE;
 KCP_TRY(KCPReadNc(c,64,&v));if(v!=KCP_SENTINEL){e=13;goto failed;}
 KCP_TRY(KCPCopy(c,0,16,r->ring_second,11));KCP_TRY(KCPPublish(c));
 KCP_TRY(KCPWptr(c,KCP_FINAL_WPTR));KCP_TRY(KCPWait(c,2,KCP_FINAL_WPTR,&r->final_fence,&r->final_rptr));
 KCP_TRY(KCPReadNc(c,64,&r->final_marker));if(r->final_marker!=KCP_MARKER){e=5;goto failed;}
 KCP_TRY(KCPSnapshot(c,r->raw_after));r->irq_mask_after=r->raw_after[3];if(r->irq_mask_after!=r->irq_mask_before){e=13;goto failed;}
 KCP_TRY(KCPGate(c));
 {uint64_t end=c->io->now_us(c->io->cookie);r->elapsed_us=end>=c->begin?end-c->begin:UINT64_MAX;if(r->elapsed_us>=500000){e=110;goto failed;}}
 r->IB_marker_observed=1;r->GPU_DMA_observed=1;r->held=1;r->phase=KCP_EXECUTED;c->book->complete=1;
 return 0;
failed:
 r->result=r->primary_error=(uint32_t)e;r->phase=r->touched?KCP_QUARANTINED:KCP_FAILED;
 if(r->touched){r->quarantined=1;c->book->quarantined=1;}
 {uint64_t end=c->io->now_us(c->io->cookie);r->elapsed_us=end>=c->begin?end-c->begin:UINT64_MAX;}
 return e;
}
#undef KCP_TRY
#endif
