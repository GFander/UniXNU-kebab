/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_DISPLAY_IRQ_CORE_H
#define KEBAB_DISPLAY_IRQ_CORE_H
#include <stdint.h>
#include <string.h>
/* Only INTID115 and MDP pingpong0 DONE(bit8). Group assignment, timers, SGIs,
 * unrelated masks and fault status are never programmed. MMIO peers provide
 * retained mappings and live display resources before arm/dispatch. */
typedef struct {
 void *cookie;
 uint32_t (*read32)(void *,unsigned,uint32_t);
 void (*write32)(void *,unsigned,uint32_t,uint32_t);
 void (*write64)(void *,unsigned,uint32_t,uint64_t);
 void (*write8)(void *,unsigned,uint32_t,uint8_t);
 void (*barrier)(void *);
} KebabDisplayIrqIo;
typedef struct {
 uint32_t state,reason,initial_enable,priority,route_low,route_high,last_pending,mdss_status;
 uint64_t events,last_time,min_period;
 uint32_t frame,sequence;
} KebabDisplayIrqState;
static void KebabDisplayIrqDisable(const KebabDisplayIrqIo *io,KebabDisplayIrqState *s,uint32_t reason)
{
 io->write32(io->cookie,0,0x18c,1u<<19);
 io->barrier(io->cookie);
 uint32_t en=io->read32(io->cookie,1,0x1010);
 io->write32(io->cookie,1,0x1010,en&~0x100u);
 io->barrier(io->cookie);
 __atomic_store_n(&s->reason,reason,__ATOMIC_RELEASE);
 __atomic_store_n(&s->state,3,__ATOMIC_RELEASE);
}
static int KebabDisplayIrqArm(const KebabDisplayIrqIo *io,KebabDisplayIrqState *s,uint64_t route,uint64_t min_period)
{
 if(!io || !s || !io->read32 || !io->write32 || !io->write64 || !io->write8 || !io->barrier || !min_period)return 1;
 if(__atomic_load_n(&s->state,__ATOMIC_ACQUIRE))return 2;
 s->min_period=min_period;
 if((io->read32(io->cookie,0,0)&0x80000012u)!=0x12u)return 3;
 if(32*((io->read32(io->cookie,0,4)&31)+1)<=115)return 3;
 s->initial_enable=io->read32(io->cookie,0,0x10c);
 if((s->initial_enable&(1u<<19)) || (io->read32(io->cookie,0,0x30c)&(1u<<19)) ||
    ((io->read32(io->cookie,0,0xc1c)>>6)&3) || io->read32(io->cookie,1,0x1010))return 3;
 if(io->read32(io->cookie,1,0)!=0x60000000u || io->read32(io->cookie,1,0x1000)!=0x60000000u)return 4;
 s->priority=io->read32(io->cookie,0,0x470)>>24;
 s->route_low=io->read32(io->cookie,0,0x6398);s->route_high=io->read32(io->cookie,0,0x639c);
 io->write64(io->cookie,0,0x6398,route);io->write8(io->cookie,0,0x473,0xa0);io->barrier(io->cookie);
 int error=5;
 if(io->read32(io->cookie,0,0x6398)!=(uint32_t)route || io->read32(io->cookie,0,0x639c)!=(uint32_t)(route>>32) ||
    (io->read32(io->cookie,0,0x470)>>24)!=0xa0)goto rollback;
 io->write32(io->cookie,1,0x1018,0x100);io->write32(io->cookie,1,0x1010,0x100);io->barrier(io->cookie);
 error=6;if(io->read32(io->cookie,1,0x1010)!=0x100)goto rollback;
 __atomic_store_n(&s->state,1,__ATOMIC_RELEASE);
 io->write32(io->cookie,0,0x10c,1u<<19);io->barrier(io->cookie);
 error=7;if(!(io->read32(io->cookie,0,0x10c)&(1u<<19)))goto rollback;
 return 0;
rollback:
 KebabDisplayIrqDisable(io,s,(uint32_t)error);
 io->write64(io->cookie,0,0x6398,(uint64_t)s->route_low|((uint64_t)s->route_high<<32));
 io->write8(io->cookie,0,0x473,(uint8_t)s->priority);io->barrier(io->cookie);return error;
}
static int KebabDisplayIrqHandle(const KebabDisplayIrqIo *io,KebabDisplayIrqState *s,uint64_t now)
{
 if(__atomic_load_n(&s->state,__ATOMIC_ACQUIRE)!=1)return 0;
 uint32_t top=io->read32(io->cookie,1,0x10),pending=io->read32(io->cookie,1,0x1014);
 __atomic_store_n(&s->mdss_status,top,__ATOMIC_RELAXED);__atomic_store_n(&s->last_pending,pending,__ATOMIC_RELAXED);
 uint32_t en=io->read32(io->cookie,1,0x1010);
 if(top&~1u || en!=0x100 || !(pending&0x100)) {KebabDisplayIrqDisable(io,s,8);return 1;}
 uint64_t last=__atomic_load_n(&s->last_time,__ATOMIC_RELAXED);
 if(last && (now<=last || now-last<s->min_period)){KebabDisplayIrqDisable(io,s,9);return 1;}
 io->write32(io->cookie,1,0x1018,0x100);io->barrier(io->cookie);
 (void)__atomic_add_fetch(&s->sequence,1,__ATOMIC_SEQ_CST);
 __atomic_store_n(&s->frame,io->read32(io->cookie,1,0x6b8ac),__ATOMIC_RELAXED);
 __atomic_store_n(&s->last_time,now,__ATOMIC_RELEASE);
 (void)__atomic_add_fetch(&s->events,1,__ATOMIC_RELEASE);
 (void)__atomic_add_fetch(&s->sequence,1,__ATOMIC_SEQ_CST);return 1;
}
#endif
