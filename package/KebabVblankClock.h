/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_VBLANK_CLOCK_H
#define KEBAB_VBLANK_CLOCK_H
#include <stdint.h>
/* A timer may poll this mailbox, but only a new live hardware IRQ event may
 * produce a hardware notification. CPU-copy completion remains separate from
 * physical scanout/atomic page-flip completion. All state is work-loop owned. */
typedef struct {uint64_t seen,hardware,software;uint32_t mode;} KebabVblankClock;
typedef struct {uint64_t timestamp;uint32_t notify,hardware,next_us;} KebabVblankDecision;
static KebabVblankDecision KebabVblankStep(KebabVblankClock *s,uint32_t irq_state,uint64_t events,uint64_t last_irq,uint64_t now,uint64_t max_age)
{
 KebabVblankDecision d={0,0,0,1000};
 if(irq_state==2)return d; /* In-flight IRQ tuple: wait without fabricating a tick. */
 if(irq_state==1 && events && last_irq && now>=last_irq && now-last_irq<=max_age && events>=s->seen) {
  s->mode=1;
  if(events==s->seen)return d;
  s->seen=events;s->hardware++;d.timestamp=last_irq;d.notify=1;d.hardware=1;return d;
 }
 s->mode=0;s->software++;d.timestamp=now;d.notify=1;d.next_us=16667;return d;
}
#endif
