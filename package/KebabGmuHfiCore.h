/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_HFI_CORE_H
#define KEBAB_GMU_HFI_CORE_H
#include <stdint.h>
#include <string.h>
typedef struct {
 void *cookie;int(*gate)(void *);
 int(*read_shared)(void *,uint32_t,uint32_t *);int(*write_shared)(void *,uint32_t,uint32_t);
 int(*read_reg)(void *,uint32_t,uint32_t *);int(*write_reg)(void *,uint32_t,uint32_t);
 int(*barrier)(void *);int(*delay_us)(void *,uint32_t);uint64_t(*now_us)(void *);
} KebabGmuHfiIo;
typedef struct {uint32_t attempted,quarantined,sequence,version_verified;} KebabGmuHfiSession;
typedef struct {
 uint32_t magic,schema,bytes,result,stage,io_error,failed_offset,shared_reads,shared_writes,register_reads,register_writes;
 uint32_t barriers,polls,delay_us,request_header,response_header,response_request,response_error,returned_version;
 uint32_t cmd_read,cmd_write,response_read,response_write,irq_info,ao_status,packet_published,doorbell_issued,ack_consumed;
 uint32_t version_verified,hfi_initialized,dma_observed,quarantined,evidence_replayed;
 uint64_t elapsed_us;
} KebabGmuHfiRecord;
typedef struct {const KebabGmuHfiIo *io;KebabGmuHfiSession *session;KebabGmuHfiRecord *record;uint64_t start;} KebabGmuHfiContext;
static inline void KebabGmuHfiInit(KebabGmuHfiRecord *r)
{memset(r,0,sizeof *r);r->magic=0x4946484b;r->schema=1;r->bytes=sizeof *r;r->result=95;}
static inline int KebabGmuHfiGate(KebabGmuHfiContext *c)
{int e=c->io->gate(c->io->cookie);if(e)c->record->io_error=e;return e?95:0;}
static inline int KebabGmuHfiRead(KebabGmuHfiContext *c,unsigned reg,uint32_t off,uint32_t *v)
{int e=KebabGmuHfiGate(c);if(e)return e;if(reg)c->record->register_reads++;else c->record->shared_reads++;
 e=reg?c->io->read_reg(c->io->cookie,off,v):c->io->read_shared(c->io->cookie,off,v);
 if(e){c->record->io_error=e;c->record->failed_offset=off;}return e?5:0;}
static inline int KebabGmuHfiWrite(KebabGmuHfiContext *c,unsigned reg,uint32_t off,uint32_t v)
{int e=KebabGmuHfiGate(c);if(e)return e;if(reg)c->record->register_writes++;else c->record->shared_writes++;
 e=reg?c->io->write_reg(c->io->cookie,off,v):c->io->write_shared(c->io->cookie,off,v);
 if(e){c->record->io_error=e;c->record->failed_offset=off;}return e?5:0;}
static inline int KebabGmuHfiBarrier(KebabGmuHfiContext *c)
{int e=KebabGmuHfiGate(c);if(e)return e;c->record->barriers++;e=c->io->barrier(c->io->cookie);if(e)c->record->io_error=e;return e?5:0;}
static inline int KebabGmuHfiFaults(KebabGmuHfiContext *c)
{int e;if((e=KebabGmuHfiRead(c,1,0x24c14,&c->record->ao_status)))return e;
 if((e=KebabGmuHfiRead(c,1,0x14648,&c->record->irq_info)))return e;
 return (c->record->ao_status&0x29) || (c->record->irq_info&0x8000)?5:0;}
static inline int KebabGmuHfiTable(KebabGmuHfiContext *c)
{
 const uint32_t table[6]={0,54,6,12,4,4};uint32_t v;int e;
 for(unsigned i=0;i<6;i++){if((e=KebabGmuHfiRead(c,0,i*4,&v)))return e;if(v!=table[i])return 11;}
 const uint32_t types[4]={0xa00,0xa01,0x2802,0x1403};
 for(unsigned q=0;q<4;q++){
  const uint32_t expect[5]={q<3?1u:0u,0x60001000u+224+4096*q,types[q],1024,0};
  for(unsigned i=0;i<5;i++){if((e=KebabGmuHfiRead(c,0,24+48*q+4*i,&v)))return e;if(v!=expect[i])return 11;}
 }return 0;
}
static inline int KebabGmuHfiIndices(KebabGmuHfiContext *c)
{
 KebabGmuHfiRecord *r=c->record;int e;
 if((e=KebabGmuHfiRead(c,0,64,&r->cmd_read)))return e;
 if((e=KebabGmuHfiRead(c,0,68,&r->cmd_write)))return e;
 if((e=KebabGmuHfiRead(c,0,112,&r->response_read)))return e;
 if((e=KebabGmuHfiRead(c,0,116,&r->response_write)))return e;
 if((r->cmd_read|r->cmd_write|r->response_read|r->response_write)&3)return 11;
 return r->cmd_read<1024 && r->cmd_write<1024 && r->response_read<1024 && r->response_write<1024?0:11;
}
/* One native bring-up probe: a real OTA HFI2 FW_VERSION command and matching
 * ACK. It is NOT the PERF/BW/CORE_FW_START initialization sequence. */
static inline int KebabGmuHfiVersion(KebabGmuHfiContext *c)
{
 KebabGmuHfiRecord *r=c->record;KebabGmuHfiSession *s=c->session;uint32_t v,packet[19],size,used,idx;int e=95;
 if(s->attempted || s->quarantined)return 95;
 if(!c->io || !c->io->gate || !c->io->read_shared || !c->io->write_shared || !c->io->read_reg || !c->io->write_reg ||
    !c->io->barrier || !c->io->delay_us || !c->io->now_us)return 22;
 if((e=KebabGmuHfiTable(c)))goto done;if((e=KebabGmuHfiIndices(c)))goto done;
 if(r->cmd_read!=r->cmd_write || r->response_read!=r->response_write){e=16;goto done;}
 if((e=KebabGmuHfiFaults(c)))goto done;if(r->irq_info&1){e=16;goto done;}
 if(s->sequence || s->version_verified){e=95;goto done;}
 s->attempted=1;s->sequence=1;c->start=c->io->now_us(c->io->cookie);
 r->request_header=(1u<<20)|(2u<<8)|1;r->stage=1;idx=r->cmd_write;
 if((e=KebabGmuHfiWrite(c,0,224+4*idx,r->request_header)))goto done;idx=(idx+1)&1023;
 if((e=KebabGmuHfiWrite(c,0,224+4*idx,0x20000000)))goto done;idx=(idx+1)&1023;
 for(unsigned n=0;n<2;n++){if((e=KebabGmuHfiWrite(c,0,224+4*idx,0xfafafafa)))goto done;idx=(idx+1)&1023;}
 if((e=KebabGmuHfiBarrier(c)))goto done;
 if((e=KebabGmuHfiWrite(c,0,68,idx)))goto done;r->packet_published=1;
 if((e=KebabGmuHfiBarrier(c)))goto done;
 if((e=KebabGmuHfiWrite(c,1,0x14650,1)))goto done;r->doorbell_issued=1;r->stage=2;
 for(unsigned n=0;n<50;n++){
  if((e=KebabGmuHfiFaults(c)))goto done;r->polls++;
  uint64_t now=c->io->now_us(c->io->cookie);if(now<c->start || now-c->start>=5000){e=110;goto done;}
  if(r->irq_info&1)break;
  if((e=KebabGmuHfiGate(c)))goto done;r->delay_us+=100;e=c->io->delay_us(c->io->cookie,100);if(e){r->io_error=e;e=5;goto done;}
 }
 if(!(r->irq_info&1)){e=110;goto done;}
 if((e=KebabGmuHfiBarrier(c)))goto done;
 if((e=KebabGmuHfiIndices(c)))goto done;
 if(r->cmd_read!=idx || r->cmd_write!=idx){e=11;goto done;}
 used=(r->response_write-r->response_read)&1023;if(!used){e=11;goto done;}
 if((e=KebabGmuHfiRead(c,0,224+4096+4*r->response_read,&v)))goto done;
 size=(v>>8)&255;r->response_header=v;
 if(size<4 || size>19 || used<((size+3)&~3u) || ((v>>16)&15)!=1){e=11;goto done;}
 idx=r->response_read;memset(packet,0,sizeof packet);
 for(unsigned i=0;i<size;i++){if((e=KebabGmuHfiRead(c,0,224+4096+4*idx,&packet[i])))goto done;idx=(idx+1)&1023;}
 r->response_request=packet[1];r->response_error=packet[2];r->returned_version=packet[3];
 if(packet[1]!=r->request_header || packet[2] || packet[3]!=0x20010008){e=11;goto done;}
 {uint64_t now=c->io->now_us(c->io->cookie);if(now<c->start || now-c->start>=5000){e=110;goto done;}}
 idx=(idx+3)&~3u;idx&=1023;
 if((e=KebabGmuHfiWrite(c,1,0x14644,1)))goto done;/* Only owned MSGQ0; OOB/fault bits preserved. */
 if((e=KebabGmuHfiWrite(c,0,112,idx)))goto done;r->ack_consumed=1;
 if((e=KebabGmuHfiBarrier(c)))goto done;
 s->version_verified=1;r->version_verified=1;r->dma_observed=1;r->stage=3;e=0;
done:
 if(s->attempted && e){s->quarantined=1;r->quarantined=1;}
 if(s->attempted)r->elapsed_us=c->io->now_us(c->io->cookie)-c->start;
 r->result=e;return e;
}
#endif
