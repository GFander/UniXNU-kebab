from pathlib import Path
import hashlib,json,subprocess
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);root=args.source/'iokit/Kernel/arm';s=(root/'KebabFramebuffer.cpp').read_text();a=s.index('static void\nkebab_fb_handoff_console(');b=s.index('\nextern "C" void kebab_framebuffer_report',a);body=s[a:b]
prefix=r'''
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
unsigned fWidth=2,fHeight=2,fRowBytes=12,fDebugFlags=0;int fOffX=0,fOffY=0;uintptr_t fPixels;bool fConsolePaused=false;KebabSurface*fSurface=nullptr;int lock=0,*fLock=&lock;uint64_t fTracePresented=0;FrameTrace fTraceFirst={},fTraceLast={};uint32_t fLayerSurface[4]={};void present(const uint8_t*,uint32_t);
};
'''
main=r'''
static void put(uint8_t*p,unsigned off,uint32_t value){memcpy(p+off,&value,4);}
int main(){
 for(unsigned mode=0;mode<12;mode++){
  Memory memory={56};surface=KebabSurface{};surface.memory=&memory;surface.data.resize(80,0xa5);uint8_t*g=surface.data.data();
  for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned ch=0;ch<4;ch++)kpf_write(g+8+y*24+x*8,KPF_W40A,ch,ch==3?65536:((x+y+ch)%2?65536:0));
  std::vector<uint32_t>panel(12,0x69696969);AppleCLCD fb;fb.fPixels=(uintptr_t)(panel.data()+1);uint8_t q[0x110]={};put(q,0x104,1);put(q,0x54,2);put(q,0xcc,2);put(q,0xd0,2);cancel=kebab_console_muted=0;panic_now=false;
  if(mode==1){surface.bytesPerElement=4;surface.pixelFormat=KPF_BGRA;for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned ch=0;ch<4;ch++)g[8+y*24+x*4+ch]=ch==3?255:((x+y+ch)%2?255:0);}
  if(mode==2)kpf_write(g+8,KPF_W40A,3,32768);
  if(mode==3)surface.allocSize=47;
  if(mode==4)memory.n=47;
  if(mode==5)surface.bytesPerRow=15;
  if(mode==6)surface.planeCount=1;
  if(mode==7)surface.pixelFormat=0x26773461;
  if(mode==8)panic_now=true;
  if(mode==9)surface.elementWidth=2;if(mode==10){g[14]=0xc0;g[15]=0xff;}if(mode==11){g[14]=0;g[15]=0;}
  fb.present(q,1);bool good=mode<3||mode==10||mode==11;
  assert(!locks&&surface.refs==1);
  if(good){assert(cancel==1&&kebab_console_muted&&fb.fConsolePaused&&fb.fTraceLast.bytes==16&&fb.fTraceLast.rejected==0);for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){uint32_t want=0xff000000;for(unsigned ch=0;ch<3;ch++)if((x+y+ch)%2)want|=255u<<(ch*8);assert(panel[1+y*3+x]==want);}}
  else{assert(cancel==0&&!fb.fConsolePaused);for(auto v:panel)assert(v==0x69696969);if(mode!=8)assert(fb.fTraceLast.rejected==1);}
  assert(panel[0]==0x69696969&&panel[3]==0x69696969&&panel[6]==0x69696969&&panel[11]==0x69696969);
 }
 puts("PASS12 actual present/handoff cases, byte and padding checks");return 0;
}
'''
out=w/'wide-present-tests-0519';out.mkdir(exist_ok=True);(out/'KebabPixelFormat.h').write_bytes((root/'KebabPixelFormat.h').read_bytes());(out/'actual.cpp').write_text(prefix+body+main)
def run(name):
 p=subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(out/(name+'.cpp')),'-o',str(out/name)],capture_output=True,text=True);assert not p.returncode,p.stderr
 p=subprocess.run([str(out/name)],capture_output=True,text=True);(out/(name+'.txt')).write_text(p.stdout+p.stderr);return p
q=run('actual');assert not q.returncode,q.stdout+q.stderr
mutants=[('old-format-gate','|| wide) && s->planeCount','|| false) && s->planeCount'),('wrong-wide-step','from+(uint64_t)x*8','from+(uint64_t)x*4'),('reject-transparency','bool wide = bpe == 8 && s->pixelFormat == KPF_W40A;','bool wide = bpe == 8 && s->pixelFormat == KPF_W40A; if(wide && src && (kpf_word((const uint8_t *)(src+s->offset)+6)>>6)<894)src=0;')]
for name,old,new in mutants:
 assert old in body;path=out/(name+'.cpp');path.write_text(prefix+body.replace(old,new)+main);p=run(name);assert p.returncode,(name,p.stdout)
(w/'present-tests.json').write_text(json.dumps(dict(passed=True,actual_present_cases=12,compiled_runtime_mutants=3,source_sha256=hashlib.sha256(s.encode()).hexdigest(),explicit_surface_memory_lock_and_console_models=True,device_memory_alignment_not_host_proven=True),indent=2)+'\n');print(q.stdout)
