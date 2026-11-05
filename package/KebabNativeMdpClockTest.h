/* SPDX-License-Identifier: MIT */
#include "KebabMdpClockCore.h"
static volatile uint32_t kebab_clock_test_started,kebab_clock_test_finished;
static KebabMdpClockTest kebab_clock_test_core;
static uint32_t kebab_clock_test_error,kebab_clock_test_soft_delta;
static uint64_t kebab_clock_test_irq_before,kebab_clock_test_irq_released,kebab_clock_test_irq_after;
static bool kebab_clock_test_map(KebabNativeDisplayMaps *m,unsigned i,uint64_t base,uint64_t size)
{
 m->memory[i]=IOMemoryDescriptor::withPhysicalAddress(base,size,i==1?kIODirectionInOut:kIODirectionIn);
 if(!m->memory[i])return false;
 m->map[i]=m->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(i==1?0:kIOMapReadOnly));
 if(!m->map[i])return false;m->address[i]=m->map[i]->getVirtualAddress();
 return m->address[i] && m->map[i]->getLength()>=size;
}
static int kebab_clock_test_read(void *cookie,unsigned region,size_t offset,uint32_t *out)
{
 KebabNativeDisplayMaps *m=(KebabNativeDisplayMaps *)cookie;static const size_t size[]={0x10000,0x4000,0x70000};
 if(region>=3 || !out || (offset&3) || offset>size[region]-4 || !m->address[region])return 5;
 if(region) {uint32_t gcc=kebab_display_load(m->address[0],0xb00c);if(!(gcc&1) || (gcc&0x80000000u))return 5;}
 if(region==2) {
  uint32_t gdsc=kebab_display_load(m->address[1],0x3000);
  if(!(gdsc&0x80000000u) || (gdsc&1) || (kebab_display_load(m->address[1],0x2080)&0x80000000u) ||
     (kebab_display_load(m->address[1],0x200c)&0x80000000u))return 5;
 }
 *out=kebab_display_load(m->address[region],offset);return 0;
}
static int kebab_clock_test_write(void *cookie,size_t offset,uint32_t value)
{
 KebabNativeDisplayMaps *m=(KebabNativeDisplayMaps *)cookie;
 if((offset!=0x20c8 && offset!=0x20cc) || !m->address[0] || !m->address[1])return 22;
 uint32_t gcc=kebab_display_load(m->address[0],0xb00c);if(!(gcc&1) || (gcc&0x80000000u))return 5;
 *(volatile uint32_t *)(m->address[1]+offset)=value;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static void kebab_clock_test_delay(void *cookie,unsigned us)
{ (void)cookie;if(us>=1000)IOSleep((us+999)/1000);else IODelay(us); }
static bool kebab_clock_test_irq(uint64_t *events)
{
 for(unsigned i=0;i<3;i++){
  uint32_t irq[16];kebab_display_irq_snapshot(irq);
  if(irq[0]==2){IODelay(10);continue;}
  *events=(uint64_t)irq[4]|((uint64_t)irq[5]<<32);return irq[0]==1 && !irq[1] && *events;
 }
 return false;
}
static void kebab_native_mdp_clock_test(void)
{
 if(!__sync_bool_compare_and_swap(&kebab_clock_test_started,0,1)) {
  IOLog("KEBAB DCLKTEST: rejected error=16 already-attempted; no writes\n");IOLog("KEBAB DCLKTEST: end;\n");return;
 }
 KebabNativeDisplayMaps m={};uint32_t before[8]={},after[8]={};uint64_t soft_before=0,soft_after=0;
 kebab_fb_vblank_snapshot(before);soft_before=(uint64_t)before[3]|((uint64_t)before[4]<<32);
 kebab_clock_test_error=95;
 if(panic_active() || before[0]!=1 || !kebab_clock_test_irq(&kebab_clock_test_irq_before))goto done;
 kebab_clock_test_error=12;
 if(!kebab_clock_test_map(&m,0,0x100000,0x10000) || !kebab_clock_test_map(&m,1,0xaf00000,0x4000) || !kebab_clock_test_map(&m,2,0xae00000,0x70000))goto done;
 {
  KebabMdpClockIo io={&m,kebab_clock_test_read,kebab_clock_test_write,kebab_clock_test_delay};
  kebab_clock_test_error=(uint32_t)KebabMdpClockExercise(&io,&kebab_clock_test_core);
 }
 if(!kebab_clock_test_irq(&kebab_clock_test_irq_released))kebab_clock_test_error=5;
 IOSleep(30);
 if(!kebab_clock_test_irq(&kebab_clock_test_irq_after) || kebab_clock_test_irq_after<=kebab_clock_test_irq_released)kebab_clock_test_error=5;
 kebab_fb_vblank_snapshot(after);soft_after=(uint64_t)after[3]|((uint64_t)after[4]<<32);
 kebab_clock_test_soft_delta=(uint32_t)(soft_after-soft_before);
 if(after[0]!=1 || soft_after!=soft_before)kebab_clock_test_error=5;
done:
 for(unsigned n=3;n;n--){unsigned i=n-1;if(m.map[i])m.map[i]->release();if(m.memory[i])m.memory[i]->release();}
 __atomic_store_n(&kebab_clock_test_finished,1,__ATOMIC_RELEASE);
 IOLog("KEBAB DCLKTEST: result error=%u restored=%u cfg=%x/%x/%x cmd=%x/%x frames=%u/%u/%u/%u writes=%u irq=%llu/%llu/%llu software-delta=%u; nominal divider change, no PLL/voltage writes\n",kebab_clock_test_error,kebab_clock_test_core.restored,kebab_clock_test_core.original_cfg,kebab_clock_test_core.lower_cfg,kebab_clock_test_core.final_cfg,kebab_clock_test_core.original_cmd,kebab_clock_test_core.final_cmd,kebab_clock_test_core.frame_begin,kebab_clock_test_core.frame_lower_begin,kebab_clock_test_core.frame_lower_end,kebab_clock_test_core.frame_restored,kebab_clock_test_core.write_attempts,kebab_clock_test_irq_before,kebab_clock_test_irq_released,kebab_clock_test_irq_after,kebab_clock_test_soft_delta);
 IOLog("KEBAB DCLKTEST: end;\n");
}
static void kebab_native_mdp_clock_receipt(uint32_t out[24])
{
 memset(out,0,24*sizeof *out);out[0]=__atomic_load_n(&kebab_clock_test_started,__ATOMIC_ACQUIRE);out[1]=__atomic_load_n(&kebab_clock_test_finished,__ATOMIC_ACQUIRE);
 if(!out[1])return;
 static_assert(sizeof(KebabMdpClockTest)==56,"clock-core receipt fields");
 out[2]=kebab_clock_test_error;out[3]=kebab_clock_test_soft_delta;memcpy(out+4,&kebab_clock_test_core,sizeof kebab_clock_test_core);
 out[18]=(uint32_t)kebab_clock_test_irq_before;out[19]=(uint32_t)(kebab_clock_test_irq_before>>32);
 out[20]=(uint32_t)kebab_clock_test_irq_released;out[21]=(uint32_t)(kebab_clock_test_irq_released>>32);
 out[22]=(uint32_t)kebab_clock_test_irq_after;out[23]=(uint32_t)(kebab_clock_test_irq_after>>32);
}
