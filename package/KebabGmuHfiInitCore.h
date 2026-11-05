/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_HFI_INIT_CORE_H
#define KEBAB_GMU_HFI_INIT_CORE_H
#include "KebabGmuHfiCore.h"
#include "KebabGmuHfiInitTables.h"
typedef struct {uint32_t matches,result,slave,address,version,priority[2],aux_bytes,aux[8];} KebabGmuHfiInitResource;
typedef struct {uint32_t db_version;KebabGmuHfiInitResource resource[7];} KebabGmuHfiInitMetadata;
typedef struct {KebabGmuHfiIo channel;int(*metadata)(void *,KebabGmuHfiInitMetadata *);} KebabGmuHfiInitIo;
typedef struct {uint32_t header,response_header,response_request,response_error,published,acked,cmd_after,response_after;} KebabGmuHfiInitMessage;
typedef struct {
 KebabGmuHfiRecord io;
 uint32_t phase,policy,sequence_before,sequence_after,perf_ack,bw_ack,core_ack,dcvs_ack;
 uint32_t bandwidth_qualified,gx_qualified,cp_qualified,replayed;
 KebabGmuHfiInitMessage message[4];
} KebabGmuHfiInitRecord;
typedef struct {uint32_t attempted,quarantined,core_attempted,complete;} KebabGmuHfiInitBook;
typedef struct {
 const KebabGmuHfiInitIo *io;KebabGmuHfiSession *session;KebabGmuHfiInitBook *book;KebabGmuHfiInitRecord *r;
 uint64_t begin,ack_begin;uint32_t ack_active;
} KebabGmuHfiInitContext;
enum {KHI_EMPTY=0,KHI_PERF_ACCEPTED=1,KHI_BW_ACCEPTED=2,KHI_CORE_ACCEPTED=3,KHI_DCVS_ACCEPTED=4,KHI_QUARANTINED=5};
static inline void KebabGmuHfiInitRecordInit(KebabGmuHfiInitRecord *r)
{memset(r,0,sizeof *r);KebabGmuHfiInit(&r->io);r->io.magic=0x3149484b;r->io.bytes=sizeof *r;r->policy=15;}
/* Exact board metadata, read fresh by the native RAM adapter. Priority is
 * retained raw but is neither a selector nor a CPU/RSC ownership proof. */
static inline int KebabGmuHfiInitMetadataCheck(const KebabGmuHfiInitMetadata *m)
{
 static const uint32_t address[7]={0x30050,0x30000,0x30010,0x50000,0x50004,0x5007c,0x500a4};
 static const uint32_t aux[7][8]={
  {3145728,8388672,12583056,16777440,25166144,416,0,0},
  {1048576,8388672,16777408,25166144,416,0,0,0},
  {1048576,12583040,16777440,25166144,416,0,0,0},
  {2700000,16,0,0,0,0,0,0},{4000000,65600,0,0,0,0,0,0},
  {1,720896,0,0,0,0,0,0},{530000,196624,0,0,0,0,0,0}};
 if(!m || m->db_version!=1)return 11;
 for(unsigned i=0;i<7;i++){
  const KebabGmuHfiInitResource *v=&m->resource[i];
  if(v->matches!=1 || v->result || v->slave!=(i<3?3u:5u) || v->address!=address[i] || v->version!=0x1000 || v->aux_bytes!=(i<3?32u:8u) || memcmp(v->aux,aux[i],sizeof v->aux))return 11;
 }return 0;
}
static inline int KHIClock(KebabGmuHfiInitContext *c)
{
 uint64_t n=c->io->channel.now_us(c->io->channel.cookie);
 if(n<c->begin || n-c->begin>=100000 || (c->ack_active && (n<c->ack_begin || n-c->ack_begin>=5000)))return 110;
 return 0;
}
/* Every requested I/O is preceded by the real owner/control gate AND fresh
 * fault CSR reads. The CSR callbacks themselves recheck the owner, and all
 * callback cost is included in the wall-clock budget. No recursive gate. */
static inline int KHIGate(KebabGmuHfiInitContext *c)
{
 const KebabGmuHfiIo *i=&c->io->channel;KebabGmuHfiRecord *r=&c->r->io;int e=KHIClock(c);if(e)return e;
 e=i->gate(i->cookie);if(e){r->io_error=e;return 95;}if((e=KHIClock(c)))return e;
 r->register_reads++;e=i->read_reg(i->cookie,0x24c14,&r->ao_status);if(e){r->io_error=e;r->failed_offset=0x24c14;return 5;}if((e=KHIClock(c)))return e;
 r->register_reads++;e=i->read_reg(i->cookie,0x14648,&r->irq_info);if(e){r->io_error=e;r->failed_offset=0x14648;return 5;}if((e=KHIClock(c)))return e;
 if((r->ao_status&0x29) || (r->irq_info&0x8000))return 5;
 /* No side/debug consumer is granted by this four-message transaction. */
 return r->irq_info&6?11:0;
}
static inline int KHIRead(KebabGmuHfiInitContext *c,unsigned reg,uint32_t off,uint32_t *v)
{
 int e=KHIGate(c);if(e)return e;const KebabGmuHfiIo *i=&c->io->channel;
 if(reg)c->r->io.register_reads++;else c->r->io.shared_reads++;
 e=reg?i->read_reg(i->cookie,off,v):i->read_shared(i->cookie,off,v);
 if(e){c->r->io.io_error=e;c->r->io.failed_offset=off;return 5;}return KHIClock(c);
}
static inline int KHIWrite(KebabGmuHfiInitContext *c,unsigned reg,uint32_t off,uint32_t value)
{
 int e=KHIGate(c);if(e)return e;const KebabGmuHfiIo *i=&c->io->channel;
 if(reg)c->r->io.register_writes++;else c->r->io.shared_writes++;
 e=reg?i->write_reg(i->cookie,off,value):i->write_shared(i->cookie,off,value);
 if(e){c->r->io.io_error=e;c->r->io.failed_offset=off;return 5;}return KHIClock(c);
}
static inline int KHIBarrier(KebabGmuHfiInitContext *c)
{
 int e=KHIGate(c);if(e)return e;c->r->io.barriers++;e=c->io->channel.barrier(c->io->channel.cookie);
 if(e){c->r->io.io_error=e;return 5;}return KHIClock(c);
}
static inline int KHITable(KebabGmuHfiInitContext *c)
{
 const uint32_t t[6]={0,54,6,12,4,4},types[4]={0xa00,0xa01,0x2802,0x1403};uint32_t v;int e;
 for(unsigned n=0;n<6;n++){if((e=KHIRead(c,0,n*4,&v)))return e;if(v!=t[n])return 11;}
 for(unsigned q=0;q<4;q++){
  const uint32_t a[5]={q<3?1u:0u,0x60001000u+224+4096*q,types[q],1024,0};
  for(unsigned n=0;n<5;n++){if((e=KHIRead(c,0,24+48*q+4*n,&v)))return e;if(v!=a[n])return 11;}
 }return 0;
}
static inline int KHIIndices(KebabGmuHfiInitContext *c)
{
 KebabGmuHfiRecord *r=&c->r->io;int e;
 if((e=KHIRead(c,0,64,&r->cmd_read)))return e;if((e=KHIRead(c,0,68,&r->cmd_write)))return e;
 if((e=KHIRead(c,0,112,&r->response_read)))return e;if((e=KHIRead(c,0,116,&r->response_write)))return e;
 if((r->cmd_read|r->cmd_write|r->response_read|r->response_write)&3)return 11;
 return r->cmd_read<1024 && r->cmd_write<1024 && r->response_read<1024 && r->response_write<1024?0:11;
}
static inline int KHISend(KebabGmuHfiInitContext *c,unsigned which,const uint32_t *words,unsigned size)
{
 KebabGmuHfiInitMessage *m=&c->r->message[which];KebabGmuHfiRecord *r=&c->r->io;uint32_t packet[19],idx,used,v;int e;
 if((e=KHITable(c)))return e;if((e=KHIIndices(c)))return e;
 if(r->cmd_read!=r->cmd_write || r->response_read!=r->response_write)return 16;
 if((e=KHIGate(c)))return e;if(r->irq_info&1)return 16;
 uint32_t old_cmd=r->cmd_write,old_response=r->response_read;
 unsigned padded=(size+3)&~3u,space=(r->cmd_read-r->cmd_write-1)&1023;
 if(size>160 || padded>space || c->session->sequence>=4095)return 16;
 ++c->session->sequence;m->header=(c->session->sequence<<20)|words[0];r->request_header=m->header;idx=r->cmd_write;
 for(unsigned n=0;n<padded;n++){
  v=n==0?m->header:n<size?words[n]:0xfafafafa;
  if((e=KHIWrite(c,0,224+4*idx,v)))return e;idx=(idx+1)&1023;
 }
 if((e=KHIBarrier(c)))return e;
 if((e=KHIIndices(c)))return e;
 if(r->cmd_read!=old_cmd || r->cmd_write!=old_cmd || r->response_read!=old_response || r->response_write!=old_response)return 16;
 if((e=KHIGate(c)))return e;if(r->irq_info&1)return 16;
 /* Publication may be observed before doorbell. Never start after it. */
 c->ack_begin=c->io->channel.now_us(c->io->channel.cookie);c->ack_active=1;
 if(which==2)c->book->core_attempted=1;
 /* An attempted index store is uncertain even if callback/readback fails. */
 m->published=1;r->packet_published=1;
 if((e=KHIWrite(c,0,68,idx)))return e;if((e=KHIBarrier(c)))return e;
 if((e=KHIWrite(c,1,0x14650,1)))return e;r->doorbell_issued=1;
 for(unsigned n=0;n<50;n++){
  if((e=KHIGate(c)))return e;r->polls++;if(r->irq_info&1)break;
  r->delay_us+=100;e=c->io->channel.delay_us(c->io->channel.cookie,100);
  if(e){r->io_error=e;return 5;}if((e=KHIClock(c)))return e;
 }
 if(!(r->irq_info&1))return 110;
 if((e=KHIBarrier(c)))return e;if((e=KHIIndices(c)))return e;
 if(r->cmd_read!=idx || r->cmd_write!=idx)return 11;m->cmd_after=idx;
 used=(r->response_write-r->response_read)&1023;if(!used)return 11;
 if((e=KHIRead(c,0,224+4096+4*r->response_read,&v)))return e;
 unsigned len=(v>>8)&255;m->response_header=v;r->response_header=v;
 if(len<3 || len>19 || used!=((len+3)&~3u) || ((v>>16)&15)!=1)return 11;
 idx=r->response_read;memset(packet,0,sizeof packet);
 for(unsigned n=0;n<len;n++){if((e=KHIRead(c,0,224+4096+4*idx,&packet[n])))return e;idx=(idx+1)&1023;}
 m->response_request=packet[1];m->response_error=packet[2];r->response_request=packet[1];r->response_error=packet[2];
 if(packet[1]!=m->header || packet[2])return 11;
 idx=(idx+3)&~3u;idx&=1023;
 if((e=KHIWrite(c,1,0x14644,1)))return e;if((e=KHIWrite(c,0,112,idx)))return e;
 if((e=KHIBarrier(c)))return e;if((e=KHIGate(c)))return e;
 m->acked=1;m->response_after=idx;r->ack_consumed=which+1;c->ack_active=0;return 0;
}
static inline int KebabGmuHfiInitialize(KebabGmuHfiInitContext *c)
{
 const KebabGmuHfiIo *i=&c->io->channel;KebabGmuHfiInitMetadata m;int e=95;uint64_t final_now;
 if(!c->session || !c->book || !c->r || !i->gate || !i->read_shared || !i->write_shared || !i->read_reg || !i->write_reg || !i->barrier || !i->delay_us || !i->now_us || !c->io->metadata)return 22;
 c->begin=i->now_us(i->cookie);c->ack_active=0;c->r->sequence_before=c->session->sequence;
 if(c->book->attempted || c->book->quarantined || c->session->quarantined || !c->session->attempted || !c->session->version_verified || c->session->sequence!=1)goto done;
 if((e=KHIGate(c)))goto done;
 memset(&m,0,sizeof m);e=c->io->metadata(i->cookie,&m);if(e){c->r->io.io_error=e;goto done;}
 if((e=KHIGate(c)))goto done;if((e=KebabGmuHfiInitMetadataCheck(&m)))goto done;
 if((e=KHITable(c)))goto done;if((e=KHIIndices(c)))goto done;
 if(c->r->io.cmd_read!=c->r->io.cmd_write || c->r->io.response_read!=c->r->io.response_write){e=16;goto done;}
 c->book->attempted=1;
 if((e=KHISend(c,0,KHIPERF,59)))goto done;c->r->perf_ack=1;c->r->phase=KHI_PERF_ACCEPTED;
 if((e=KHISend(c,1,KHIBW,160)))goto done;c->r->bw_ack=1;c->r->phase=KHI_BW_ACCEPTED;
 if((e=KHISend(c,2,KHICOREFWSTART,2)))goto done;c->r->core_ack=1;c->r->io.hfi_initialized=1;c->r->phase=KHI_CORE_ACCEPTED;
 if((e=KHISend(c,3,KHIDCVS,4)))goto done;c->r->dcvs_ack=1;c->r->phase=KHI_DCVS_ACCEPTED;c->book->complete=1;e=0;
done:
 /* The final clock callback is an observation too: never publish success
  * after total-budget expiry or a backwards final sample. */
 final_now=i->now_us(i->cookie);
 c->r->io.elapsed_us=final_now>=c->begin?final_now-c->begin:0;
 if(!e && (final_now<c->begin || final_now-c->begin>=100000))e=110;
 c->r->sequence_after=c->session->sequence;c->r->io.result=e;c->r->io.stage=c->r->phase;
 if(c->book->attempted && e){c->book->complete=0;c->book->quarantined=1;c->session->quarantined=1;c->r->io.quarantined=1;c->r->phase=KHI_QUARANTINED;}
 return e;
}
#endif
