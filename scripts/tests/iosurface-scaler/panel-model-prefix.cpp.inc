
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include "KebabPixelFormat.h"
#define KEBAB_FB_LAYERS 4
#define KEBAB_FB_FORMAT_BGRA KPF_BGRA
#define FALSE 0
#define OSSafeReleaseNULL(x) do{if(x){x->release();x=nullptr;}}while(0)
static bool panic_now=false;static int cancel=0,kebab_console_muted=0,kebab_fb_swaps_logged=100,locks=0;
static bool panic_active(){return panic_now;}
static int PE_parse_boot_argn(const char*,int*p,size_t){*p=1;return 1;}
static void vc_progress_set(int,int){cancel++;}static void OSMemoryBarrier(){}
static void IOLockLock(int*){locks++;}static void IOLockUnlock(int*){assert(locks==1);locks--;}
#define IOLog(...) ((void)0)
static uint32_t get32(const uint8_t*p,unsigned off){uint32_t v;memcpy(&v,p+off,4);return v;}
struct Memory{uint64_t n;uint64_t getLength(){return n;}};
struct KebabSurface{uint32_t id=2,width=2,height=2,bytesPerRow=24,pixelFormat=KPF_W40A,planeCount=0,offset=8,allocSize=56;uint16_t bytesPerElement=8;uint8_t elementWidth=1,elementHeight=1;Memory*memory;std::vector<uint8_t>data;int refs=1;uintptr_t kernelAddress(){return(uintptr_t)data.data();}void release(){refs--;}};
static KebabSurface surface;static KebabSurface*kebab_surface_copy(unsigned id){if(id!=2)return nullptr;surface.refs++;return &surface;}
class AppleCLCD{public:
struct FrameTrace {uint64_t bytes;uint32_t id,mask,selected,found,missing,self,rejected;uint32_t surfaces[4],formats[4];};
unsigned fLogicalW=2,fLogicalH=2,fWidth=2,fHeight=2,fRowBytes=12,fDebugFlags=0;int fOffX=0,fOffY=0;uintptr_t fPixels;bool fConsolePaused=false;KebabSurface*fSurface=nullptr;int lock=0,*fLock=&lock;uint64_t fTracePresented=0;FrameTrace fTraceFirst={},fTraceLast={};uint32_t fLayerSurface[4]={};void present(const uint8_t*,uint32_t);
};
