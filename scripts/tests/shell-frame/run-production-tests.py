"""Actual production readonly core and swap/present functions; explicit memory/IOKit models."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
r=Path(__file__).resolve().parent;ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path);ap.add_argument('--work',type=Path);a=ap.parse_args();w=a.work or r/'shell-frame-0485';root=a.source or w/'candidate';w.mkdir(parents=True,exist_ok=True)
bsd=(root/'bsd/kern/kebab_bootdiag.c').read_text();mach=(root/'osfmk/kern/kebab_bootdiag.c').read_text();fb=(root/'iokit/Kernel/arm/KebabFramebuffer.cpp').read_text()
def fun(text,decl,name):
 at=text.index(decl+'\n'+name+'(');end=text.index('\n}\n',at)+3;return text[at:end]
copy=fun(mach,'static bool','kebab_plugin_user_copy')
at=mach.index('struct kebab_shell_ack_state {');end=mach.index('\nvoid kebab_shell_ack_task_report(',at);shell=mach[at:end]
trace=fb[fb.index('\tstruct FrameTrace {'):fb.index('\tuint32_t      fLayerSurface[KEBAB_FB_LAYERS];')]
frame=fun(fb,'void','AppleCLCD::swap')+'\n'+fun(fb,'void','AppleCLCD::present')
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
using kern_return_t=int;constexpr int KERN_SUCCESS=0;
struct Cell {uint64_t address;std::vector<uint8_t> data;};
struct Map {std::vector<Cell> cells;bool kernel=false;};using vm_map_t=Map*;
#define VM_MAP_NULL nullptr
static void *pmap_kernel(){return (void*)1;}
static void *vm_map_pmap(vm_map_t m){return m->kernel?(void*)1:(void*)2;}
static uint64_t vm_map_min(vm_map_t){return 0x100000000ULL;}
static uint64_t vm_map_max(vm_map_t){return 0x300000000ULL;}
static unsigned reads;static bool mutate_global;static unsigned global_reads;
static int copyinmap(vm_map_t m,uint64_t address,void *out,size_t size){reads++;for(auto &c:m->cells){if(address>=c.address&&address-c.address<c.data.size()&&size<=c.data.size()-(address-c.address)){memcpy(out,c.data.data()+address-c.address,size);if(address==0x1dd5f3668ULL&&++global_reads==2&&mutate_global)*(uint64_t*)out=0x100000200ULL;return 0;}}return 5;}
template<class T>static void put(Map &m,uint64_t a,const T &v){m.cells.push_back({a,std::vector<uint8_t>((const uint8_t*)&v,(const uint8_t*)&v+sizeof(v))});}
static void setup(Map &m){m.cells.clear();m.kernel=false;reads=global_reads=0;mutate_global=false;uint8_t uuid[]={0x8f,0x97,0x04,0x52,0xc9,0xe3,0x3e,0x88,0xb1,0x14,0xfe,0xb1,0x86,0x4b,0xc9,0xe0};put(m,0x180000058ULL,uuid);uint64_t mask=0xffffffff8ULL,instance=0x100001000ULL;put(m,0x1800e8940ULL,mask);put(m,0x1dd5f3668ULL,instance);uint64_t shell[]={0x1dd5f2c50ULL,0,2,0,0,0x100002000ULL};uint64_t service[]={0x1dcffec08ULL,0,0,0x100003000ULL};uint64_t signal[]={0x1e1d7f910ULL,1};put(m,instance,shell);put(m,0x100002000ULL,service);put(m,0x100003000ULL,signal);}
static bool held=false;static void IOLockLock(void*){assert(!held);held=true;}static void IOLockUnlock(void*){assert(held);held=false;}
static void IOLog(const char*,...){assert(!held);}
static int kebab_console_muted;static bool PE_parse_boot_argn(const char*,int*,size_t){return false;}
#define KEBAB_FB_LAYERS 4
#define KEBAB_FB_SWAP_REC_SIZE 0x46c
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
struct Memory {uint64_t size=256;uint64_t getLength(){return size;}};
struct KebabSurface {unsigned refs=0;uint32_t id=7,width=8,height=8,bytesPerRow=32,offset=0,pixelFormat=0x42475241;uint32_t bytesPerElement=4,elementWidth=1,elementHeight=1;Memory storage;Memory* memory=&storage;uint8_t pixels[256];uintptr_t kernelAddress(){return (uintptr_t)pixels;}void release(){assert(refs);refs--;}};
static KebabSurface surf,self;static bool missing;static KebabSurface *kebab_surface_copy(uint32_t id){if(missing)return nullptr;KebabSurface *s=id==99?&self:&surf;s->refs++;return s;}
static uint32_t get32(const uint8_t *p,uint32_t off){uint32_t v;memcpy(&v,p+off,4);return v;}
static uint32_t kebab_fb_swaps_logged;
class AppleCLCD {public:
'''
middle=r'''
 void *fLock=(void*)1;uint32_t fSwapID=9,fQueuedID=0,fPendingID=0;uint8_t fPending[0x46c]={};bool fPendingValid=false,fHaveFrame=false;
 bool fConsolePaused=false;uint32_t fDebugFlags=0;KebabSurface *fSurface=&self;int fOffX=0,fOffY=0;uint32_t fWidth=8,fHeight=8,fRowBytes=32;uint8_t pixels[256]={};uintptr_t fPixels=(uintptr_t)pixels;uint32_t fLayerSurface[4]={};
 AppleCLCD(){fTraceQueued=fTracePresented=0;memset(&fTraceFirst,0,sizeof(fTraceFirst));memset(&fTraceLast,0,sizeof(fTraceLast));}
 void swap(const uint8_t*);void present(const uint8_t*,uint32_t);
};
static void reset_surface(){assert(!surf.refs&&!self.refs&&!held);surf=KebabSurface();memset(surf.pixels,0x37,sizeof(surf.pixels));surf.memory=&surf.storage;missing=false;}
static void store(uint8_t *p,unsigned off,uint32_t v){memcpy(p+off,&v,4);}
'''
tests=r'''
int main(){unsigned checks=0;Map map;struct kebab_shell_ack_state state;
 setup(map);assert(!kebab_shell_ack_snapshot(&map,&state)&&state.state==2&&state.completed==1);checks++;
 for(unsigned i=0;i<3;i++){setup(map);map.cells[i].data[0]^=0x40;assert(kebab_shell_ack_snapshot(&map,&state)!=0&&!state.completed&&!state.state);checks++;}
 for(unsigned i=3;i<6;i++){setup(map);map.cells[i].data[0]^=0x40;assert(kebab_shell_ack_snapshot(&map,&state)!=0&&!state.completed);checks++;}
 setup(map);map.cells[5].data[8]=0;assert(!kebab_shell_ack_snapshot(&map,&state)&&!state.completed);checks++;
 setup(map);map.cells[5].data[8]=2;assert(kebab_shell_ack_snapshot(&map,&state)==7&&!state.completed);checks++;
 setup(map);map.cells[3].data[16]=3;assert(kebab_shell_ack_snapshot(&map,&state)==7&&!state.completed);checks++;
 setup(map);mutate_global=true;assert(kebab_shell_ack_snapshot(&map,&state)==8&&!state.completed);checks++;
 setup(map);map.kernel=true;assert(kebab_shell_ack_snapshot(&map,&state)!=0&&!reads);checks++;
 assert(kebab_shell_ack_snapshot(nullptr,&state)!=0&&!reads);checks++;
 for(unsigned i=3;i<6;i++){setup(map);uint64_t isa;memcpy(&isa,map.cells[i].data.data(),8);isa|=0x1a000000001ULL;memcpy(map.cells[i].data.data(),&isa,8);assert(!kebab_shell_ack_snapshot(&map,&state)&&state.completed);checks++;}
 reset_surface();AppleCLCD fb;uint8_t rec[0x46c]={};fb.swap(rec);assert(fb.fTraceQueued==1&&fb.fPendingValid&&fb.fPendingID==9&&fb.fHaveFrame&&!memcmp(fb.fPending,rec,sizeof(rec)));checks++;
 fb.present(rec,1);assert(fb.fTracePresented==1&&!fb.fTraceFirst.mask&&!fb.fTraceFirst.selected&&!fb.fTraceFirst.bytes);checks++;
 store(rec,0x104,1);store(rec,0x54,7);store(rec,0xc4+8,8);store(rec,0xc4+12,8);
 fb.present(rec,2);assert(fb.fTracePresented==2&&fb.fTraceLast.mask==1&&fb.fTraceLast.selected==1&&fb.fTraceLast.found==1&&fb.fTraceLast.bytes==256&&fb.pixels[255]==0x37&&!surf.refs);assert(fb.fTraceFirst.id==1&&!fb.fTraceFirst.bytes);checks+=2;
 missing=true;fb.present(rec,3);assert(fb.fTraceLast.missing==1&&!fb.fTraceLast.bytes);checks++;
 reset_surface();surf.bytesPerElement=2;fb.present(rec,4);assert(fb.fTraceLast.rejected==1&&!fb.fTraceLast.bytes);checks++;
 reset_surface();surf.storage.size=128;fb.present(rec,5);assert(fb.fTraceLast.rejected==1&&!fb.fTraceLast.bytes);checks++;
 reset_surface();store(rec,0x54,99);fb.present(rec,6);assert(fb.fTraceLast.self==1&&!fb.fTraceLast.bytes&&!self.refs);checks++;
 fprintf(stdout,"PASS %u production readonly shell/swap/present checks; user VM/IOKit/pixels are models, no physical ACK or panel claim\n",checks);
}
'''
pieces=dict(copy=copy,shell=shell,frame=frame)
mutants=[('ignore-cache-uuid','shell','memcmp(uuid, cache_uuid, sizeof(uuid))','false'),('ignore-mask','shell','mask != 0xffffffff8ULL','false'),('wrong-shell-class','shell','0x1dd5f2c50ULL','0x1dd5f2c58ULL'),('ignore-signal-range','shell','signal[8] > 1','false'),('ignore-state-range','shell','shell[2] > 2','false'),('ignore-global-reread','shell','again_instance != instance','false'),('fake-ack','shell','out->completed = signal[8]','out->completed = 1'),('kernel-map-read','copy','vm_map_pmap(map) == pmap_kernel()','false'),('wrong-layer-mask','frame','get32(rec, 0x104)','get32(rec, 0x108)'),('fake-byte-count','frame','trace.bytes += (uint64_t)(size_t)w * 4 * (uint64_t)h;','trace.bytes += 1;'),('missing-not-counted','frame','trace.missing++','trace.self++'),('overwrite-first','frame','if (fTracePresented == 1)','if (true)'),('queue-count','frame','fTraceQueued++;','fTraceQueued += 2;')]
env=dict(os.environ);env['TMPDIR']=env.get('TMPDIR') or tempfile.gettempdir();runs=[]
with tempfile.TemporaryDirectory(dir=env['TMPDIR']) as temp:
 d=Path(temp)
 for label,part,old,new in [('baseline',None,None,None)]+mutants:
  units=dict(pieces)
  if part:assert units[part].count(old)==1,(label,units[part].count(old));units[part]=units[part].replace(old,new)
  fixture=prefix+trace+middle+'\n'+units['copy']+'\n'+units['shell']+'\n'+units['frame']+'\n'+tests;p=d/(label+'.cpp');p.write_text(fixture);exe=d/label
  built=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True,env=env);assert built.returncode==0,(label,'compile failure is not a behavioral rejection',built.stderr[-3500:])
  run=subprocess.run([str(exe)],capture_output=True,text=True,env=env);assert (run.returncode==0)==(label=='baseline'),(label,run.stdout,run.stderr[-2500:]);runs.append(dict(name=label,compiled=True,exit_code=run.returncode,stdout=run.stdout.strip()))
  if label=='baseline':(w/'production-test.cpp').write_text(fixture,newline='\n');print(run.stdout.strip(),flush=True)
q=dict(passed=True,production_source_sha256={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ('bsd/kern/kebab_bootdiag.c','osfmk/kern/kebab_bootdiag.c','iokit/Kernel/arm/KebabFramebuffer.cpp','iokit/Kernel/arm/KebabDriverDiagnostics.cpp')},fixture_sha256=hashlib.sha256((w/'production-test.cpp').read_bytes()).hexdigest(),compiled_behavioral_mutants=len(mutants),runs=runs,actual_production_core_and_swap_present=True,VM_IOKit_and_pixels_are_models=True,full_caller_and_registry_integration_host_qualified=False,native_or_physical_claim=False)
(w/'test-shell-frame-result-0485.json').write_text(json.dumps(q,indent=2)+'\n',newline='\n');print('PASS13 compiled behavioral mutants; no original reply or first-frame success inferred')
