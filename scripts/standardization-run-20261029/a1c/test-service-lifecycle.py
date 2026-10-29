"""Execute actual parent lifecycle with failed initialization and concurrent stop."""
from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(sys.argv[1]).read_text()
def fn(name):
    at=source.index('KebabSPMIBus::'+name+'(');at=source.rfind('\n',0,at-1)+1
    end=source.index('\n}',at)+2
    return source[at:end]
start=source.index('class KebabSPMIDevice :');end=source.index('OSDefineMetaClassAndStructors(KebabSPMIBus,')
decl=source[start:end].replace('class KebabSPMIBus :','struct KebabSPMIBus :')
decl=decl.replace('OSDefineMetaClassAndStructors(KebabSPMIDevice, IOService);','')
at=source.index('static bool\nkebab_spmi_property');prop=source[at:source.index('\n}',at)+2]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <future>
#include <thread>
#define APPLE_KEXT_OVERRIDE
#define OSDeclareDefaultStructors(n)
#define IOLog(...) ((void)0)
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
#define OSSafeReleaseNULL(p) do {if(p){p->release();p=nullptr;}}while(0)
using IOReturn=int;
enum {kIOReturnSuccess,kIOReturnBadArgument,kIOReturnNoDevice,kIOReturnIOError,kIOReturnNotReady};
constexpr int kIOServiceSynchronous=1;
static std::string failure;
static int liveServices,liveArrays,liveLocks,attachments,published,superStops,nubNumber,callCount;
static bool inParentInit,keysEnabled;
static std::promise<void>*entered,*proceed;
struct OSObject {
 unsigned refs=1;
 virtual ~OSObject()=default;virtual void free(){}
 void retain(){++refs;}void release(){assert(refs);if(!--refs){free();delete this;}}
};
struct OSDictionary:OSObject{};
struct OSData:OSObject {
 std::vector<uint8_t> bytes;
 OSData(const void*p,size_t n):bytes((const uint8_t*)p,(const uint8_t*)p+n){}
 size_t getLength(){return bytes.size();}const void*getBytesNoCopy(){return bytes.data();}
};
struct IOService:OSObject {
 std::map<std::string,OSObject*> props;std::string name;IOService*provider=nullptr;int nub=0;
 IOService(){++liveServices;}~IOService(){--liveServices;}
 static void*operator new(size_t n) noexcept {if(failure=="new-"+std::to_string(nubNumber+1))return nullptr;return ::operator new(n);}
 static void operator delete(void*p){::operator delete(p);}
 bool init(OSDictionary* = nullptr){
  if(inParentInit)return failure!="super-init";
  nub=++nubNumber;return failure!="init-"+std::to_string(nub);
 }
 bool start(IOService*){return failure!="super-start";}
 void stop(IOService*){++superStops;}void free()override{}
 void setName(const char*n){name=n;}
 bool setProperty(const char*k,OSObject*v){if(failure=="property-"+std::to_string(nub))return false;props[k]=v;return v!=nullptr;}
 bool setProperty(const char*,const char*){return true;}bool setProperty(const char*,bool){return true;}
 bool setProperty(const char*,uint64_t,unsigned){return true;}
 OSObject*getProperty(const char*k){return props[k];}
 bool attach(IOService*p){if(failure=="attach-"+std::to_string(nub))return false;provider=p;++attachments;return true;}
 void detach(IOService*p){assert(provider==p);provider=nullptr;--attachments;}
 IOService*getProvider(){return provider;}
 void registerService(){++published;}bool terminate(int){return true;}
};
struct OSArray:OSObject {
 std::vector<OSObject*>items;OSArray(){++liveArrays;}~OSArray(){--liveArrays;}
 static OSArray*withCapacity(unsigned){return failure=="array"?nullptr:new OSArray;}
 bool setObject(OSObject*p){if(failure=="store-"+std::to_string(items.size()+1))return false;p->retain();items.push_back(p);return true;}
 unsigned getCount(){return (unsigned)items.size();}OSObject*getObject(unsigned i){return items[i];}
 void flushCollection(){for(auto*p:items)p->release();items.clear();}
 void free()override{flushCollection();}
};
struct IOLock{std::mutex m;};
static IOLock*IOLockAlloc(){if(failure=="lock")return nullptr;++liveLocks;return new IOLock;}
static void IOLockFree(IOLock*p){--liveLocks;delete p;}
static void IOLockLock(IOLock*p){p->m.lock();}static void IOLockUnlock(IOLock*p){p->m.unlock();}
struct kebab_spmi_bus_info{uint64_t resources[8];uint32_t version,ee,write_apid;bool mapped,write_channel_mapped;};
struct kebab_rpmh_info{uint64_t resources[4];uint32_t irq_status,command_status,phase;bool mapped,writes_enabled;};
static const uint64_t resources[]={0xc440000,0x1100,0xe600000,0x100000,0xc40a000,0x26000,0xc600000,0x2000000};
static const uint64_t rpmhResources[]={0x18220000,0x10000,0x80860000,0x20000};
static void kebab_spmi_bus_get_info(kebab_spmi_bus_info*p){*p={};memcpy(p->resources,resources,sizeof(resources));p->mapped=true;p->version=0x50010000;}
static void kebab_rpmh_get_info(kebab_rpmh_info*p){*p={};memcpy(p->resources,rpmhResources,sizeof(rpmhResources));p->mapped=true;}
static bool kebab_spmi_version_known(uint32_t v){return v!=0&&v!=UINT32_MAX;}
static bool PE_parse_boot_argn(const char*,uint32_t*p,unsigned){*p=keysEnabled;return true;}
static int kebab_spmi_pon_state(uint8_t*p){
 ++callCount;if(entered){auto*e=entered;entered=nullptr;e->set_value();proceed->get_future().wait();}
 p[0]=4;p[1]=0;return 0;
}
static int kebab_spmi_keys_sample(uint8_t*p){++callCount;memset(p,1,4);p[3]=0x10;return 0;}
static int kebab_rpmh_read_status(kebab_rpmh_info*p){++callCount;kebab_rpmh_get_info(p);return 0;}
'''
code=prefix+prop+'\n'+decl+'\n'+'\n'.join(fn(n) for n in ['init','addChild','start','clearChildren','stop','free','ponState','keysSample','vrmStatus'])
code+=r'''
int main(){
 IOService provider;uint32_t ee=0,pon[]={0,8},keys[]={0,8,0xc5,0xc6};
 OSData reg(resources,sizeof(resources)),eeData(&ee,4),ponData(pon,sizeof(pon)),keyData(keys,sizeof(keys)),rsc(rpmhResources,16),db(rpmhResources+2,16);
 provider.props={{"reg",&reg},{"qcom,ee",&eeData},{"kebab,pon",&ponData},{"kebab,pmic-keys",&keyData},{"kebab,rpmh-rsc",&rsc},{"kebab,rpmh-cmd-db",&db}};
 std::vector<std::string> failures={"super-init","lock","array","super-start","bad-reg"};
 for(int i=1;i<=3;i++)for(const char*p:{"new-","init-","property-","attach-","store-"})failures.push_back(std::string(p)+std::to_string(i));
 for(const auto&f:failures){
  failure=f;nubNumber=published=superStops=0;
  KebabSPMIBus bus;inParentInit=true;bool initialized=bus.init(nullptr);inParentInit=false;
  if(initialized){
   if(f=="bad-reg")reg.bytes[0]^=1;
   assert(!bus.start(&provider));
   if(f=="bad-reg")reg.bytes[0]^=1;
  }
  bus.free();assert(liveServices==2&&!liveArrays&&!liveLocks&&!attachments&&!published);
 }
 failure="";nubNumber=published=0;KebabSPMIBus bus;inParentInit=true;assert(bus.init(nullptr));inParentInit=false;
 assert(bus.start(&provider)&&published==4&&attachments==3);
 uint8_t state[2],raw[4];kebab_rpmh_info status;
 assert(bus.ponState(state)==kIOReturnSuccess&&state[0]==4);
 assert(bus.keysSample(raw)==kIOReturnNotReady);
 assert(bus.vrmStatus(&status)==kIOReturnSuccess&&status.mapped);
 std::promise<void>began,allow;entered=&began;proceed=&allow;
 auto request=std::async(std::launch::async,[&]{return bus.ponState(state);});began.get_future().wait();
 auto stop=std::async(std::launch::async,[&]{bus.stop(&provider);});
 assert(stop.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout);
 allow.set_value();assert(request.get()==kIOReturnSuccess);stop.get();int calls=callCount;
 assert(bus.ponState(state)==kIOReturnNoDevice&&bus.keysSample(raw)==kIOReturnNoDevice&&bus.vrmStatus(&status)==kIOReturnNoDevice&&calls==callCount);
 bus.free();assert(liveServices==2&&!liveArrays&&!liveLocks&&!attachments);
 puts("PASS: actual SPMI parent rollback across 20 failure cases, scoped nubs, disabled keys and concurrent stop/drain");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-pthread','-Wall','-Wextra','-fsanitize=address,undefined',
        str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=20)
