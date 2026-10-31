"""Exercise actual normal service lifecycle and native state query boundaries."""
from pathlib import Path
import subprocess
import tempfile

r=Path(__file__).parent
s=(r/'after/iokit/Kernel/arm/KebabGeni.cpp').read_text()
def fn(name):
    at=s.index('KebabGeniBus::'+name+'(');at=s.rfind('\n',0,at-1)+1
    return s[at:s.index('\n}',at)+2]
at=s.index('static bool\nkebab_geni_property');prop=s[at:s.index('\n}',at)+2]
at=s.index('class KebabI2CDevice :');end=s.index('OSDefineMetaClassAndStructors(KebabGeniBus,')
decl=s[at:end].replace('class KebabGeniBus :','struct KebabGeniBus :').replace('OSDefineMetaClassAndStructors(KebabI2CDevice, IOService);','')
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <thread>
#define OSDeclareDefaultStructors(n)
#define APPLE_KEXT_OVERRIDE
#define IOLog(...) ((void)0)
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
#define OSSafeReleaseNULL(p) do {if(p){p->release();p=nullptr;}}while(0)
using IOReturn=int;
enum {kIOReturnSuccess,kIOReturnBadArgument,kIOReturnNoDevice,kIOReturnNotReady};
static constexpr int kIOServiceSynchronous=1;
static int live,locks,attached,published,backendCalls;
static bool inParentInit,backendMapped=true;
static std::string failure;
static std::promise<void>*entered,*proceed;
struct OSObject {
 virtual ~OSObject()=default;virtual void free(){};
 void release(){free();delete this;}
};
struct OSDictionary:OSObject{};
struct OSData:OSObject {
 std::vector<uint8_t>bytes;
 OSData(const void*p,size_t n):bytes((const uint8_t*)p,(const uint8_t*)p+n){}
 size_t getLength(){return bytes.size();}const void*getBytesNoCopy(){return bytes.data();}
};
struct IOService:OSObject {
 std::map<std::string,OSObject*>props;IOService*provider=nullptr;bool initialized=false;
 IOService(){live++;}~IOService(){live--;}
 static void*operator new(size_t n)noexcept{if(failure=="new")return nullptr;return ::operator new(n);}
 static void operator delete(void*p){::operator delete(p);}
 bool init(OSDictionary*=nullptr)=delete;
 bool init(OSDictionary *properties=nullptr){(void)properties;initialized=failure!=(inParentInit?"super-init":"child-init");return initialized;}
 bool start(IOService*){return failure!="super-start";}void stop(IOService*){}void free()override{}
 void setName(const char*){}bool setProperty(const char*,const char*){return true;}
 bool setProperty(const char*,bool){return true;}
 bool setProperty(const char*,OSObject*v){return v&&failure!="property";}
 OSObject*getProperty(const char*k){return props[k];}
 bool attach(IOService*p){if(failure=="attach")return false;provider=p;attached++;return true;}
 void detach(IOService*p){assert(provider==p);provider=nullptr;attached--;}
 IOService*getProvider(){assert(initialized);return provider;}
 bool terminate(int){return true;}void registerService(){published++;}
};
struct IOLock{std::mutex m;};
static IOLock*IOLockAlloc(){if(failure=="lock")return nullptr;locks++;return new IOLock;}
static void IOLockFree(IOLock*p){locks--;delete p;}
static void IOLockLock(IOLock*p){p->m.lock();}static void IOLockUnlock(IOLock*p){p->m.unlock();}
static std::mutex transaction;
static void kebab_geni_transaction_lock(){transaction.lock();}
static void kebab_geni_transaction_unlock(){transaction.unlock();}
'''.replace(' bool init(OSDictionary*=nullptr)=delete;\n','')
header=(r/'after/bsd/dev/kebab_geni.h').read_text()
# Stub backend declarations need C linkage to match the production C header.
prefix=prefix.replace('static void kebab_geni_transaction_lock()','extern "C" void kebab_geni_transaction_lock()').replace('static void kebab_geni_transaction_unlock()','extern "C" void kebab_geni_transaction_unlock()')
backend=r'''
static const uint64_t resources[]={0xa94000,0x4000,0xac0000,0x2000,0xf500000,0x40000};
extern "C" void kebab_geni_get_info(kebab_geni_info*p){*p={};memcpy(p->resources,resources,sizeof(resources));p->mapped=backendMapped;}
extern "C" int kebab_geni_read_status(kebab_geni_status*p){
 backendCalls++;if(entered){auto*e=entered;entered=nullptr;e->set_value();proceed->get_future().wait();}
 *p={};p->info.mapped=true;p->protocol=3;return failure=="read" ? -1:0;
}
extern "C" void kebab_touch_get_status(kebab_touch_status*p){*p={};p->queue_ok=true;}
'''
main=r'''
int main(){
 IOService provider;uint32_t address=0x4b;
 OSData reg(resources,sizeof(resources)),addr(&address,4);
 provider.props={{"reg",&reg},{"kebab,touch-address",&addr}};
 for(const char*f:{"super-init","lock","super-start","new","child-init","property","attach","bad-reg","bad-address","unmapped"}){
   failure=f;published=0;KebabGeniBus bus;inParentInit=true;bool ok=bus.init(nullptr);inParentInit=false;
   if(ok){
     if(failure=="bad-reg")reg.bytes[0]^=1;
     if(failure=="bad-address")addr.bytes[0]^=1;
     backendMapped=failure!="unmapped";assert(!bus.start(&provider));backendMapped=true;
     if(failure=="bad-reg")reg.bytes[0]^=1;
     if(failure=="bad-address")addr.bytes[0]^=1;
   }
   bus.free();assert(live==2&&locks==0&&attached==0&&published==0);
 }
 failure="";KebabGeniBus bus;inParentInit=true;assert(bus.init(nullptr));inParentInit=false;
 assert(bus.start(&provider)&&attached==1&&published==2);
 kebab_geni_status state;kebab_touch_status touch;
 assert(bus.copyState(nullptr,&touch)==kIOReturnBadArgument);
 assert(bus.copyState(&state,&touch)==kIOReturnSuccess&&state.protocol==3&&touch.queue_ok);
 failure="read";assert(bus.copyState(&state,&touch)==kIOReturnNotReady);failure="";
 std::promise<void>began,allow;entered=&began;proceed=&allow;
 auto query=std::async(std::launch::async,[&]{return bus.copyState(&state,&touch);});began.get_future().wait();
 auto stop=std::async(std::launch::async,[&]{bus.stop(&provider);});
 assert(stop.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout);
 allow.set_value();assert(query.get()==kIOReturnSuccess);stop.get();int prior=backendCalls;
 assert(bus.copyState(&state,&touch)==kIOReturnNoDevice&&prior==backendCalls);
 bus.free();assert(live==2&&locks==0&&attached==0);
 puts("actual GENI parent: 10 failures, scoped address, typed query/error, stop drain/offline and balanced resources PASS");
}
'''
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('')
    p.write_text(prefix+header+backend+prop+'\n'+decl+'\n'+'\n'.join(fn(n) for n in ['init','start','clearChild','stop','free','copyState'])+main)
    subprocess.run(['clang++','-std=c++17','-pthread','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
