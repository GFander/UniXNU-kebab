/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_MDP_CLOCK_CORE_H
#define KEBAB_MDP_CLOCK_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {void *cookie;int(*read32)(void*,unsigned,size_t,uint32_t*);int(*write_cc)(void*,size_t,uint32_t);void(*delay_us)(void*,unsigned);} KebabMdpClockIo;
typedef struct {uint32_t error,phase,restored,original_cfg,lower_cfg,final_cfg,original_cmd,final_cmd,frame_begin,frame_lower_begin,frame_lower_end,frame_restored,write_attempts,update_polls;} KebabMdpClockTest;
/* One fixed inherited profile only. Same PLL parent, divider2->3->2, voltage
 * and all other roots unchanged. Nominal300/200MHz derives from the pinned
 * vendor plan, not an independent oscillator measurement. */
static int kebab_mdp_clock_write(const KebabMdpClockIo *io,KebabMdpClockTest *o,size_t off,uint32_t value)
{o->write_attempts++;return io->write_cc(io->cookie,off,value);}
static int kebab_mdp_clock_update(const KebabMdpClockIo *io,KebabMdpClockTest *o)
{
 if(kebab_mdp_clock_write(io,o,0x20c8,3))return 1;
 for(unsigned n=0;n<2000;n++){
  uint32_t cmd;o->update_polls++;if(io->read32(io->cookie,1,0x20c8,&cmd))return 1;
  if(!(cmd&1))return 0;io->delay_us(io->cookie,10);
 }
 return 1;
}
static int KebabMdpClockExercise(const KebabMdpClockIo *io,KebabMdpClockTest *o)
{
 if(o)memset(o,0,sizeof *o);
 if(!io || !o || !io->read32 || !io->write_cc || !io->delay_us)return 22;
 o->phase=1;uint32_t gcc,gdsc,ahb,core,version,mdp;
 if(io->read32(io->cookie,0,0xb00c,&gcc) || io->read32(io->cookie,1,0x3000,&gdsc) ||
    io->read32(io->cookie,1,0x2080,&ahb) || io->read32(io->cookie,1,0x200c,&core))return o->error=5;
 if(!(gcc&1) || (gcc&0x80000000u) || !(gdsc&0x80000000u) || (gdsc&1) || (ahb&0x80000000u) || (core&0x80000000u))return o->error=6;
 if(io->read32(io->cookie,2,0,&version) || io->read32(io->cookie,2,0x1000,&mdp) ||
    io->read32(io->cookie,1,0x20cc,&o->original_cfg) || io->read32(io->cookie,1,0x20c8,&o->original_cmd))return o->error=5;
 if(version!=0x60000000u || mdp!=version || o->original_cfg!=0x100403 || (o->original_cmd&~0x80000000u))return o->error=95;
 if(io->read32(io->cookie,2,0x6b8ac,&o->frame_begin))return o->error=5;
 o->phase=2;o->lower_cfg=(o->original_cfg&~31u)|5;
 if(kebab_mdp_clock_write(io,o,0x20c8,2) || kebab_mdp_clock_write(io,o,0x20cc,o->lower_cfg) || kebab_mdp_clock_update(io,o)){o->error=5;goto restore;}
 {
  uint32_t cfg;if(io->read32(io->cookie,1,0x20cc,&cfg) || cfg!=o->lower_cfg ||
    io->read32(io->cookie,2,0x6b8ac,&o->frame_lower_begin)){o->error=5;goto restore;}
 }
 o->phase=3;io->delay_us(io->cookie,120000);
 if(io->read32(io->cookie,2,0x6b8ac,&o->frame_lower_end) ||
    (uint32_t)(o->frame_lower_end-o->frame_lower_begin)<3 || (uint32_t)(o->frame_lower_end-o->frame_lower_begin)>16)o->error=5;
restore:
 o->phase=4;
 /* Always attempt restoration after the first write, including denied/stuck
  * writes. No broad reset, clock disable or voltage operation is available. */
 int control_error=kebab_mdp_clock_write(io,o,0x20cc,o->original_cfg);
 control_error|=kebab_mdp_clock_update(io,o);
 io->delay_us(io->cookie,20000);
 int frame_error=io->read32(io->cookie,2,0x6b8ac,&o->frame_restored);
 control_error|=kebab_mdp_clock_write(io,o,0x20c8,0);
 control_error|=io->read32(io->cookie,1,0x20cc,&o->final_cfg);
 control_error|=io->read32(io->cookie,1,0x20c8,&o->final_cmd);
 if(control_error || frame_error)o->error=5;
 o->restored=(!control_error && o->final_cfg==o->original_cfg && !(o->final_cmd&3));
 if(!o->restored || (uint32_t)(o->frame_restored-o->frame_lower_end)<1)o->error=5;
 o->phase=5;return(int)o->error;
}
#endif
