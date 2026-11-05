/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_DSI_PIO_CORE_H
#define KEBAB_DSI_PIO_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {void *cookie;int(*read32)(void*,unsigned,size_t,uint32_t*);int(*write_dsi)(void*,size_t,uint32_t);void(*delay_us)(void*,unsigned);} KebabDsiPioIo;
typedef struct {
 uint32_t error,phase,restored,writes,polls,command,requested;
 uint32_t hardware_version,control_before,status_before,int_before,dma_before,length_before,tpg_before;
 uint32_t int_done,readback_control,raw_readback,response_type,value,returned_bytes;
 uint32_t dma_after,length_after,tpg_after,int_after,status_after,reserved[3];
} KebabDsiPioObservation;
/* Pinned Qualcomm2.4 inherited CMD profile only. Two embedded short headers:
 * maximum return2, then a last/read/BTA DCS header; brightness uses one
 * last/long header and three payload bytes. No memory DMA, PLL, panel reset,
 * other panel commands or interrupt enables are available here. */
static int kebab_dsi_pio_write(const KebabDsiPioIo *io,KebabDsiPioObservation *o,size_t off,uint32_t v)
{o->writes++;return io->write_dsi(io->cookie,off,v);}
static int kebab_dsi_pio_read(const KebabDsiPioIo *io,KebabDsiPioObservation *o,size_t off,uint32_t *v)
{(void)o;return io->read32(io->cookie,2,off,v);}
static int kebab_dsi_pio_transfer(const KebabDsiPioIo *io,unsigned command,unsigned requested,unsigned brightness,int writing,KebabDsiPioObservation *o)
{
 if(o)memset(o,0,sizeof *o);
 if(!io||!o||!io->read32||!io->write_dsi||!io->delay_us)return 22;
 o->command=command;o->requested=requested;o->phase=1;
 if(writing){if(command!=0x51||requested||brightness>2047)return o->error=22;o->value=brightness;}
 else if(!((command==0x52&&requested==2)||((command==0x0a||command==0xda||command==0xdb||command==0xdc)&&requested==1)))return o->error=22;
 uint32_t gcc,gdsc,ahb;static const size_t clocks[]={0x2028,0x202c,0x2004,0x2038};
 if(io->read32(io->cookie,0,0xb00c,&gcc)||io->read32(io->cookie,1,0x3000,&gdsc)||io->read32(io->cookie,1,0x2080,&ahb))return o->error=5;
 if(!(gcc&1)||(gcc&0x80000000u)||!(gdsc&0x80000000u)||(gdsc&1)||!(ahb&1)||(ahb&0x80000000u))return o->error=6;
 for(unsigned n=0;n<4;n++){uint32_t v;if(io->read32(io->cookie,1,clocks[n],&v))return o->error=5;if(!(v&1)||(v&0x80000000u))return o->error=6;}
 if(kebab_dsi_pio_read(io,o,0,&o->hardware_version)||kebab_dsi_pio_read(io,o,4,&o->control_before)||
    kebab_dsi_pio_read(io,o,0x110,&o->int_before)||kebab_dsi_pio_read(io,o,0x3c,&o->dma_before)||
    kebab_dsi_pio_read(io,o,0x4c,&o->length_before)||kebab_dsi_pio_read(io,o,0x15c,&o->tpg_before))return o->error=5;
 /* All IRQ enables zero; preserve unrelated frame statuses (0x500). No stale
  * command/BTA/error status or pre-existing pattern producer may be adopted. */
 if(o->hardware_version!=0x20040000u||(o->control_before&0x1f7)!=0x1f5||o->dma_before!=0x14000000||o->length_before!=8||o->tpg_before||
    (o->int_before&~0x15500u))return o->error=95;
 for(unsigned n=0;n<2000;n++){
  o->polls++;if(kebab_dsi_pio_read(io,o,8,&o->status_before))return o->error=5;
  if(!(o->status_before&7))break;if(n==1999)return o->error=60;io->delay_us(io->cookie,10);
 }
 o->phase=2;
 if(kebab_dsi_pio_write(io,o,0x1d4,1)||kebab_dsi_pio_write(io,o,0x1d4,0)||
    kebab_dsi_pio_write(io,o,0x15c,0x30006)||
    kebab_dsi_pio_write(io,o,0x17c,writing?0xc0390003u:0x00370002u)||
    kebab_dsi_pio_write(io,o,0x17c,writing?(0xff000051u|((brightness>>8)<<8)|((brightness&255)<<16)):(0xa0060000u|command))){o->error=5;goto restore;}
 o->phase=3;
 if(kebab_dsi_pio_write(io,o,0x90,1)){o->error=5;goto restore;}
 for(unsigned n=0;n<10000;n++){
  o->polls++;if(kebab_dsi_pio_read(io,o,0x110,&o->int_done)||kebab_dsi_pio_read(io,o,0x1d4,&o->readback_control)){o->error=5;break;}
  if(o->int_done&0x1000000){o->error=5;break;}
  if((o->int_done&1)&&(writing||(o->readback_control>>16)))break;
  if(n==9999){o->error=60;break;}io->delay_us(io->cookie,10);
 }
 if(!o->error&&!writing){
  if((o->readback_control>>16)!=4||kebab_dsi_pio_read(io,o,0x6c,&o->raw_readback))o->error=5;
  else {o->response_type=(o->raw_readback>>24)&255;o->returned_bytes=o->response_type==0x21?1:o->response_type==0x22?2:0;
   /* The command advertises maximum return2. A two-byte short reply is
    * valid for a one-byte consumer; retain its true wire length and consume
    * only the requested prefix. Physical557 power reply was0x229c0012. */
   if(o->returned_bytes<requested)o->error=71;
   else o->value=(o->raw_readback>>16)&255;if(requested==2)o->value=(o->value<<8)|((o->raw_readback>>8)&255);
  }
 }
restore:
 o->phase=4;
 /* Only this command's FIFO is reset. No engine/global reset; only owned
  * DMA/BTA status W1C bits are acknowledged, never inherited frame statuses. */
 {
  uint32_t status=7;int failed=kebab_dsi_pio_read(io,o,8,&status);
  if(failed||(status&3)){o->error=5;return(int)o->error;}
  failed=kebab_dsi_pio_write(io,o,0x15c,0);
  failed|=kebab_dsi_pio_write(io,o,0x1ec,1);io->delay_us(io->cookie,1);failed|=kebab_dsi_pio_write(io,o,0x1ec,0);
  uint32_t irq=0;int irq_error=kebab_dsi_pio_read(io,o,0x110,&irq);failed|=irq_error;
  if(!irq_error)failed|=kebab_dsi_pio_write(io,o,0x110,(irq&0xa222aa02u)|(irq&0x100001u));
  failed|=kebab_dsi_pio_read(io,o,0x3c,&o->dma_after);failed|=kebab_dsi_pio_read(io,o,0x4c,&o->length_after);
  failed|=kebab_dsi_pio_read(io,o,0x15c,&o->tpg_after);failed|=kebab_dsi_pio_read(io,o,0x110,&o->int_after);failed|=kebab_dsi_pio_read(io,o,8,&o->status_after);
  o->restored=!failed&&o->dma_after==o->dma_before&&o->length_after==o->length_before&&o->tpg_after==0&&!(o->int_after&0xa222aa02u)&&!(o->status_after&3);
  if(!o->restored)o->error=5;
 }
 o->phase=5;return(int)o->error;
}
static int KebabDsiPioRead(const KebabDsiPioIo *io,unsigned command,unsigned requested,KebabDsiPioObservation *o)
{return kebab_dsi_pio_transfer(io,command,requested,0,0,o);}
static int KebabDsiPioBrightness(const KebabDsiPioIo *io,unsigned value,KebabDsiPioObservation *o)
{return kebab_dsi_pio_transfer(io,0x51,0,value,1,o);}
#endif
