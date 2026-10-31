#!/usr/bin/env python3
"""Run actual framebuffer lifecycle bodies with allocation failures and timer concurrency."""
from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(sys.argv[1]).read_text()
def function(name):
    begin=source.index('AppleCLCD::'+name+'(')
    begin=source.rfind('\n',0,begin-1)+1
    opening=source.index('{',begin)
    depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]+'\n'
start=source.index('class AppleCLCD :')
end=source.index('\nOSDefineMetaClassAndStructors(AppleCLCD,',start)
declaration=source[start:end].replace('class AppleCLCD :','struct AppleCLCD :',1)
bodies='\n'.join(function(n) for n in ('init','stopEvents','stopEventsAction','releaseResources','stop','free','start','available'))
stub=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <mutex>
#include <future>
#include <thread>
#include <cstdio>
#define APPLE_KEXT_OVERRIDE
#define OSDeclareDefaultStructors(x)
#define IOLog(...) ((void)0)
#define OSSafeReleaseNULL(p) do {if(p){(p)->release();(p)=nullptr;}}while(0)
#define KEBAB_FB_SWAP_REC_SIZE 0x46c
#define KEBAB_FB_LAYERS 4
#define KEBAB_FB_LOGICAL_W 1125
#define KEBAB_FB_LOGICAL_H 2436
#define KEBAB_FB_FORMAT_BGRA 0x42475241
#define KEBAB_FB_VSYNC_US 16667
#define kIOUserClientClassKey "IOUserClientClass"
constexpr int kIOReturnSuccess=0;
using IOReturn=int;
static std::string failure;
static std::map<std::string,int> live;
static int publications,super_stops,wakes,kebab_console_muted;
struct Resource {
    std::string kind;unsigned refs=1;
    explicit Resource(const char *k):kind(k){++live[kind];}
    virtual ~Resource(){--live[kind];}
    void retain(){++refs;}void release(){assert(refs);if(!--refs)delete this;}
};
struct OSObject {};
struct OSDictionary {};
struct IOService:OSObject {
    bool init(OSDictionary *){return failure!="super-init";}
    bool start(IOService *){return failure!="super-start";}
    void stop(IOService *){++super_stops;}void free(){}
    void setProperty(const char *,const char *){}
    void setProperty(const char *,bool){}
    void setProperty(const char *,uint64_t,unsigned){}
    void registerService(){++publications;}
};
struct IOLock:Resource {std::mutex mutex;IOLock():Resource("lock"){} };
static IOLock *IOLockAlloc(){return failure=="lock"?nullptr:new IOLock;}
static void IOLockFree(IOLock *p){p->release();}
static void IOLockLock(IOLock *p){p->mutex.lock();}
static void IOLockUnlock(IOLock *p){p->mutex.unlock();}
static void IOLockWakeup(IOLock *,void *,bool){++wakes;}
struct OSArray:Resource {
    OSArray():Resource("clients"){}
    static OSArray *withCapacity(unsigned){return failure=="clients"?nullptr:new OSArray;}
};
struct IOMemoryMap:Resource {IOMemoryMap():Resource("map"){} };
struct KebabSurface:Resource {
    uint32_t id=1;KebabSurface():Resource("surface"){}
    uintptr_t kernelAddress(){return failure=="pixels"?0:0x100000;}
};
static KebabSurface *kebab_surface_create_physical(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t){
    return failure=="surface"?nullptr:new KebabSurface;
}
static void kebab_surface_release_physical(KebabSurface *p){p->release();}
struct IOTimerEventSource:Resource {
    bool armed=false;IOTimerEventSource():Resource("timer"){}
    static IOTimerEventSource *timerEventSource(OSObject *,void(*)(OSObject *,IOTimerEventSource *)){
        return failure=="timer"?nullptr:new IOTimerEventSource;
    }
    void setTimeoutUS(unsigned){armed=true;}void cancelTimeout(){armed=false;}
};
struct IOWorkLoop:Resource {
    std::recursive_mutex gate;IOTimerEventSource *source=nullptr;
    IOWorkLoop():Resource("loop"){}
    ~IOWorkLoop(){assert(source==nullptr);}
    static IOWorkLoop *workLoop(){return failure=="loop"?nullptr:new IOWorkLoop;}
    int addEventSource(IOTimerEventSource *p){
        if(failure=="add")return 1;source=p;p->retain();return 0;
    }
    void removeEventSource(IOTimerEventSource *p){assert(source==p&&!p->armed);source=nullptr;p->release();}
    void closeGate(){gate.lock();}void openGate(){gate.unlock();}
    IOReturn runAction(IOReturn(*fn)(OSObject *,void *,void *,void *,void *),OSObject *target){
        std::lock_guard<std::recursive_mutex> held(gate);return fn(target,nullptr,nullptr,nullptr,nullptr);
    }
};
struct PE_Video {uint64_t v_baseAddr;unsigned v_width,v_height,v_rowBytes;};
static void PE_current_console(PE_Video *v){*v={failure=="video"?0ULL:0x9c000000ULL,1080,2400,4320};}
static bool PE_parse_boot_argn(const char *,void *,unsigned){return false;}
struct IOMobileFramebuffer:IOService {};
class IOMobileFramebufferUserClient;
'''
main=r'''
void AppleCLCD::vsyncFired(OSObject *,IOTimerEventSource *){}
static void clear(){for(auto v:live)assert(v.second==0);publications=super_stops=wakes=0;}
int main(){
    IOService provider;
    for(const char *tag:{"super-start","video","lock","clients","surface","pixels","loop","timer","add"}){
        clear();failure=tag;AppleCLCD fb{};assert(fb.init(nullptr));
        assert(!fb.start(&provider));assert(publications==0);
        fb.free();for(auto v:live)assert(v.second==0);
        assert(super_stops==(failure=="super-start"?0:1));
    }
    clear();failure="";AppleCLCD fb{};assert(fb.init(nullptr)&&fb.start(&provider));
    assert(publications==1&&fb.available()&&fb.fVsync->armed);
    std::promise<void> active,release,stopped;auto go=release.get_future();
    std::thread timer([&]{fb.fWorkLoop->closeGate();active.set_value();go.wait();fb.fWorkLoop->openGate();});
    active.get_future().wait();
    std::thread stop([&]{fb.stop(&provider);stopped.set_value();});auto done=stopped.get_future();
    assert(done.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    release.set_value();done.wait();timer.join();stop.join();
    assert(!fb.available()&&!fb.fVsync->armed&&!fb.fVsyncAdded&&wakes>0);
    fb.fConsolePaused=true;kebab_console_muted=1;fb.free();
    assert(kebab_console_muted==0);for(auto v:live)assert(v.second==0);
    puts("Nine start failures, no leaked resources, in-flight timer quiescence, waiter wake and console restoration: PASS");
}
'''
cache=Path.home()/'.cache';cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fb-lifecycle-',dir=cache) as temp:
    cpp,binary=Path(temp)/'test.cpp',Path(temp)/'test'
    cpp.write_text(stub+'\n'+declaration+'\n'+bodies+'\n'+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
