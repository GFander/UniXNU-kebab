/* SPDX-License-Identifier: MIT */
#include "KebabDsiPioCore.h"
#include "KebabBrightnessRecord.h"
#include <kern/thread_call.h>
static bool kebab_backlight_service_update_locked(uint32_t);
static void kebab_backlight_service_ready(void);
static void kebab_backlight_service_status_locked(uint32_t out[3]);
struct KebabNativeDsiProbeReceipt {
 uint32_t magic,schema,bytes,started,finished,error,count,software_delta;
 KebabDsiPioObservation observation[9];uint32_t user_brightness[16];
};
static_assert(sizeof(KebabNativeDsiProbeReceipt)==1104,"fixed native DSI PIO/brightness receipt");
static volatile uint32_t kebab_dsi_probe_started,kebab_dsi_probe_finished;
static KebabNativeDsiProbeReceipt kebab_dsi_probe_result;
static IOLock *kebab_dsi_lock;
static thread_call_t kebab_brightness_call;
/* One atomic request: sequence32, limit16, desired16. UI never waits for MMIO. */
static uint64_t kebab_brightness_request=(2047ULL<<16)|65535,kebab_brightness_done;
static uint32_t kebab_brightness_ready,kebab_brightness_applied,kebab_brightness_error,kebab_brightness_attempts,kebab_brightness_invalid;
static uint64_t kebab_brightness_raw,kebab_brightness_limit_raw;
static void kebab_brightness_worker(thread_call_param_t,thread_call_param_t);
static bool kebab_native_backlight_start(void)
{
 if(kebab_dsi_lock)return kebab_brightness_call!=NULL;
 kebab_dsi_lock=IOLockAlloc();if(!kebab_dsi_lock)return false;
 kebab_brightness_call=thread_call_allocate(kebab_brightness_worker,NULL);
 if(!kebab_brightness_call){IOLockFree(kebab_dsi_lock);kebab_dsi_lock=NULL;return false;}
 return true; /* Fixed built-in board service; these have kernel lifetime. */
}
static bool kebab_dsi_probe_map(KebabNativeDisplayMaps *m,unsigned i,uint64_t base,uint64_t size)
{
 m->memory[i]=IOMemoryDescriptor::withPhysicalAddress(base,size,i==2?kIODirectionInOut:kIODirectionIn);
 if(!m->memory[i])return false;
 m->map[i]=m->memory[i]->map(kIOMapAnywhere|kIOMapInhibitCache|(i==2?0:kIOMapReadOnly));
 if(!m->map[i])return false;m->address[i]=m->map[i]->getVirtualAddress();return m->address[i]&&m->map[i]->getLength()>=size;
}
static int kebab_dsi_probe_read(void *cookie,unsigned region,size_t off,uint32_t *v)
{
 KebabNativeDisplayMaps *m=(KebabNativeDisplayMaps *)cookie;static const size_t size[]={0x10000,0x4000,0x400};
 if(region>=3||!v||(off&3)||off>size[region]-4||!m->address[region])return 5;
 if(region){uint32_t gcc=kebab_display_load(m->address[0],0xb00c);if(!(gcc&1)||(gcc&0x80000000u))return 5;}
 if(region==2){uint32_t gdsc=kebab_display_load(m->address[1],0x3000),ahb=kebab_display_load(m->address[1],0x2080);if(!(gdsc&0x80000000u)||(gdsc&1)||!(ahb&1)||(ahb&0x80000000u))return 5;}
 *v=kebab_display_load(m->address[region],off);return 0;
}
static int kebab_dsi_probe_write(void *cookie,size_t off,uint32_t value)
{
 KebabNativeDisplayMaps *m=(KebabNativeDisplayMaps *)cookie;bool allowed=false;
 if(off==0x1d4||off==0x1ec)allowed=value<=1;
 if(off==0x15c)allowed=value==0||value==0x30006;
 if(off==0x17c)allowed=value==0x00370002||value==0xa006000a||value==0xa0060052||value==0xa00600da||value==0xa00600db||value==0xa00600dc||value==0xc0390003||((value&0xff00f8ffu)==0xff000051u);
 if(off==0x90)allowed=value==1;
 if(off==0x110)allowed=!(value&~0x100001u); /* No IRQ enable bits. */
 if(!allowed||!m->address[2])return 22;
 uint32_t live;if(kebab_dsi_probe_read(cookie,2,0,&live)||live!=0x20040000u)return 5;
 *(volatile uint32_t *)(m->address[2]+off)=value;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 OSMemoryBarrier();
#endif
 return 0;
}
static bool kebab_dsi_probe_maps(KebabNativeDisplayMaps *m)
{return kebab_dsi_probe_map(m,0,0x100000,0x10000)&&kebab_dsi_probe_map(m,1,0xaf00000,0x4000)&&kebab_dsi_probe_map(m,2,0xae94000,0x400);}
static void kebab_dsi_probe_unmap(KebabNativeDisplayMaps *m)
{for(unsigned n=3;n;n--){unsigned i=n-1;if(m->map[i])m->map[i]->release();if(m->memory[i])m->memory[i]->release();}}
/* Called only in normal thread context with the shared DSI lock held. */
static int kebab_native_backlight_apply_locked(uint32_t target)
{
 if(target>2047)return 22;
 KebabNativeDisplayMaps m={};KebabDsiPioObservation sent={},read={};kebab_brightness_error=12;kebab_brightness_attempts++;
 if(!panic_active()&&kebab_dsi_probe_maps(&m)){
  KebabDsiPioIo io={&m,kebab_dsi_probe_read,kebab_dsi_probe_write,kebab_clock_test_delay};
  kebab_brightness_error=KebabDsiPioBrightness(&io,target,&sent);
  if(!kebab_brightness_error)kebab_brightness_error=KebabDsiPioRead(&io,0x52,2,&read);
  if(!kebab_brightness_error&&read.value!=target)kebab_brightness_error=71;
  if(!kebab_brightness_error){kebab_brightness_applied=read.value;if(!kebab_backlight_service_update_locked(read.value))kebab_brightness_error=12;}
 }
 kebab_dsi_probe_unmap(&m);if(kebab_brightness_error)kebab_brightness_ready=0;return(int)kebab_brightness_error;
}
static int kebab_native_backlight_queue(const void *record,size_t bytes)
{
 KebabBrightnessRecord value;int error=KebabBrightnessRecordDecode(record,bytes,&value);
 if(record&&bytes==0x46c){const uint8_t *p=(const uint8_t *)record;uint64_t raw;
  if(p[0x2e7]){memcpy(&raw,p+0x2ec,8);__atomic_store_n(&kebab_brightness_raw,raw,__ATOMIC_RELEASE);}
  if(p[0x2e8]){memcpy(&raw,p+0x2f4,8);__atomic_store_n(&kebab_brightness_limit_raw,raw,__ATOMIC_RELEASE);}
 }
 if(error){__atomic_add_fetch(&kebab_brightness_invalid,1,__ATOMIC_RELAXED);return error;}
 if(!value.brightness_present&&!value.limit_present)return 0;
 if(!kebab_brightness_call)return 95;
 uint64_t old=__atomic_load_n(&kebab_brightness_request,__ATOMIC_ACQUIRE),next;
 do{uint32_t brightness=value.brightness_present?value.brightness:(uint32_t)(old&65535);
    uint32_t limit=value.limit_present?value.limit:(uint32_t)((old>>16)&65535);
    next=(((old>>32)+1)<<32)|((uint64_t)limit<<16)|brightness;
 }while(!__atomic_compare_exchange_n(&kebab_brightness_request,&old,next,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
 thread_call_enter(kebab_brightness_call);return 0;
}
static void kebab_brightness_worker(thread_call_param_t,thread_call_param_t)
{
 IOLockLock(kebab_dsi_lock);
 for(unsigned retry=0;retry<8;retry++){
  uint64_t request=__atomic_load_n(&kebab_brightness_request,__ATOMIC_ACQUIRE);
  if(!(request>>32)||request==kebab_brightness_done)break;
  if(!kebab_brightness_ready){kebab_brightness_error=95;break;}
  uint32_t target=(uint32_t)(request&65535),limit=(uint32_t)((request>>16)&65535);if(target>limit)target=limit;
  (void)kebab_native_backlight_apply_locked(target);kebab_brightness_done=request;
  IOLog("KEBAB BRIGHTNESS: request=%u target=%u actual=%u error=%u; native DCS51/52\n",(uint32_t)(request>>32),target,kebab_brightness_applied,kebab_brightness_error);
  if(kebab_brightness_error){kebab_brightness_ready=0;break;}
 }
 IOLockUnlock(kebab_dsi_lock);
}
static void kebab_native_dsi_probe(void)
{
 if(!__sync_bool_compare_and_swap(&kebab_dsi_probe_started,0,1)){IOLog("KEBAB DSIPROBE: rejected error=16 already-attempted; no writes\n");IOLog("KEBAB DSIPROBE: end;\n");return;}
 KebabNativeDsiProbeReceipt *r=&kebab_dsi_probe_result;KebabNativeDisplayMaps m={};uint32_t before[8]={},after[8]={};uint64_t irq=0;
 r->error=95;kebab_fb_vblank_snapshot(before);
 if(!kebab_dsi_lock)goto done;
 IOLockLock(kebab_dsi_lock);
 if(panic_active()||before[0]!=1||!kebab_clock_test_irq(&irq))goto unlock;
 r->error=12;
 if(!kebab_dsi_probe_maps(&m))goto unlock;
 {
  static const unsigned command[]={0x0a,0x52,0xda,0xdb,0xdc};KebabDsiPioIo io={&m,kebab_dsi_probe_read,kebab_dsi_probe_write,kebab_clock_test_delay};
  for(unsigned i=0;i<5;i++){r->count++;r->error=KebabDsiPioRead(&io,command[i],i==1?2:1,&r->observation[i]);if(r->error)break;}
  if(!r->error){
   unsigned original=r->observation[1].value;
   if(original>2047||!original)r->error=95;
   else {
    unsigned target=original>1?original-original/8-1:0;r->count=6;
    r->error=KebabDsiPioBrightness(&io,target,&r->observation[5]);
    if(!r->error){r->count=7;r->error=KebabDsiPioRead(&io,0x52,2,&r->observation[6]);if(!r->error&&r->observation[6].value!=target)r->error=71;}
    IOSleep(120);
    /* A failed lowering/readback still restores the exact original level. */
    r->count=8;int restore_error=KebabDsiPioBrightness(&io,original,&r->observation[7]);
    r->count=9;int verify_error=KebabDsiPioRead(&io,0x52,2,&r->observation[8]);
    if(restore_error||verify_error||r->observation[8].value!=original)r->error=5;
    if(!r->error){kebab_brightness_ready=1;kebab_brightness_applied=original;
     uint64_t pending=__atomic_load_n(&kebab_brightness_request,__ATOMIC_ACQUIRE);
     while((pending&65535)==65535&&!__atomic_compare_exchange_n(&kebab_brightness_request,&pending,(pending&~65535ULL)|original,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){}
    }
   }
  }
 }
 IOSleep(30);kebab_fb_vblank_snapshot(after);
 r->software_delta=after[3]-before[3];
 {uint64_t end=0;if(!kebab_clock_test_irq(&end)||end<=irq||after[0]!=1||after[3]!=before[3]||after[4]!=before[4])r->error=5;}
unlock:
 if(r->error)kebab_brightness_ready=0;
 kebab_dsi_probe_unmap(&m);IOLockUnlock(kebab_dsi_lock);
done:
 __atomic_store_n(&kebab_dsi_probe_finished,1,__ATOMIC_RELEASE);
 IOLog("KEBAB DSIPROBE: result error=%u count=%u power=%x brightness=%u/%u/%u id=%x/%x/%x software-delta=%u; PIO panel read and restored DCS51 brightness test\n",r->error,r->count,r->observation[0].value,r->observation[1].value,r->observation[6].value,r->observation[8].value,r->observation[2].value,r->observation[3].value,r->observation[4].value,r->software_delta);
 kebab_backlight_service_ready();
 IOLog("KEBAB DSIPROBE: end;\n");
 if(kebab_brightness_call)thread_call_enter(kebab_brightness_call);
}
static void kebab_native_dsi_probe_receipt(KebabNativeDsiProbeReceipt *out)
{
 memset(out,0,sizeof *out);out->magic=0x31525044;out->schema=2;out->bytes=sizeof *out;
 out->started=__atomic_load_n(&kebab_dsi_probe_started,__ATOMIC_ACQUIRE);out->finished=__atomic_load_n(&kebab_dsi_probe_finished,__ATOMIC_ACQUIRE);
 if(out->finished){out->error=kebab_dsi_probe_result.error;out->count=kebab_dsi_probe_result.count;out->software_delta=kebab_dsi_probe_result.software_delta;memcpy(out->observation,kebab_dsi_probe_result.observation,sizeof out->observation);}
 if(kebab_dsi_lock){IOLockLock(kebab_dsi_lock);uint64_t request=__atomic_load_n(&kebab_brightness_request,__ATOMIC_ACQUIRE);
  out->user_brightness[0]=kebab_brightness_ready;out->user_brightness[1]=(uint32_t)(request>>32);out->user_brightness[2]=(uint32_t)(request&65535);out->user_brightness[3]=(uint32_t)((request>>16)&65535);
  out->user_brightness[4]=(uint32_t)(kebab_brightness_done>>32);out->user_brightness[5]=kebab_brightness_applied;out->user_brightness[6]=kebab_brightness_error;out->user_brightness[7]=kebab_brightness_attempts;out->user_brightness[8]=__atomic_load_n(&kebab_brightness_invalid,__ATOMIC_RELAXED);
  uint64_t raw=__atomic_load_n(&kebab_brightness_raw,__ATOMIC_ACQUIRE),limit=__atomic_load_n(&kebab_brightness_limit_raw,__ATOMIC_ACQUIRE);out->user_brightness[9]=(uint32_t)raw;out->user_brightness[10]=(uint32_t)(raw>>32);out->user_brightness[11]=(uint32_t)limit;out->user_brightness[12]=(uint32_t)(limit>>32);kebab_backlight_service_status_locked(out->user_brightness+13);IOLockUnlock(kebab_dsi_lock);}
}
