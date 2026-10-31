#include <cerrno>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
using UInt32=unsigned;using task_t=void*;using kern_return_t=int;using IOReturn=int;
using IOOptionBits=unsigned;
constexpr unsigned kIOServiceSynchronous=0x2,kIOServiceAsynchronous=0x8;
constexpr int kIOReturnSuccess=0,kIOReturnNotPrivileged=0xe00002c1,kIOReturnBadArgument=0xe00002c2;
constexpr unsigned NSEC_PER_SEC=1000000000;
struct Panic:std::runtime_error{using std::runtime_error::runtime_error;};
static void panic(const char *fmt,...) {throw Panic(fmt);}
static void IOLog(const char *,...) {}
struct IOSimpleLock {bool held=false;};
static IOSimpleLock originalLock;
static void IOSimpleLockLock(IOSimpleLock *l){assert(!l->held);l->held=true;}
static void IOSimpleLockUnlock(IOSimpleLock *l){assert(l->held);l->held=false;}
static bool interrupts=true;
static bool ml_set_interrupts_enabled(bool b){bool old=interrupts;interrupts=b;return old;}
static uint64_t now=1000000000;
static uint64_t mach_absolute_time(){return now;}
using thread_call_param_t=void*;
struct Timer{void(*fn)(void*,void*);void *parameter;bool active=false;uint64_t when=0;};
using thread_call_t=Timer*;
static Timer *lastTimer;
static bool timerFail;
static thread_call_t thread_call_allocate(void(*fn)(void*,void*),void *p){if(timerFail)return nullptr;return lastTimer=new Timer{fn,p};}
static void clock_interval_to_deadline(unsigned n,unsigned scale,uint64_t *t){assert(scale==NSEC_PER_SEC);*t=now+(uint64_t)n*scale;}
static void thread_call_enter_delayed(Timer *t,uint64_t when){assert(t);t->active=true;t->when=when;}
static void thread_call_cancel(Timer *t){t->active=false;}
static void thread_call_cancel_wait(Timer *t){t->active=false;}
static void thread_call_free(Timer *t){if(lastTimer==t)lastTimer=nullptr;delete t;}
class OSObject {public:virtual ~OSObject()=default;virtual void release(){delete this;}};
class OSSymbol:public OSObject {public:std::string text;explicit OSSymbol(const char *t):text(t){}static const OSSymbol *withCString(const char*t){return new OSSymbol(t);}};
static bool badBase,badClient,preFail,postFail,iteratorInvalid,initFail,attachFail,startFail,privileged=true;
static int loadError,bootArg=1,originalOpenCalls;
static bool deferredMatching=true,publishFail,foreignMatch,duplicateMatch;
static unsigned syncRegistrations,asyncRegistrations;
class OSMetaClass;static const OSMetaClass *subclassMeta();
class OSMetaClass {unsigned size;const OSMetaClass *super;public:explicit OSMetaClass(unsigned n,const OSMetaClass *s=nullptr):size(n),super(s){}unsigned getClassSize()const{return size;}const OSMetaClass *getSuperClass()const{return super;}static const OSMetaClass *getMetaClassWithName(const OSSymbol*);static void *preModLoad(const char*){return preFail?nullptr:(void*)1;}static int postModLoad(void*p){assert(p);return postFail?1:0;}};
class OSDictionary:public OSObject{};
class IOUserClient:public OSObject{};
static IOUserClient originalClient;
class IOService;
static std::vector<IOService*> services;
static std::vector<IOService*> pendingServices;
class OSIterator:public OSObject {unsigned at=0;public:OSObject *getNextObject();bool isValid(){return !iteratorInvalid;}};
class IOService:public OSObject {
 char opaque[128]{};
public:
 virtual bool init(){return !initFail;}
 virtual bool start(IOService*){return !startFail;}
 virtual void stop(IOService*){}
 virtual void free(){delete this;}
 virtual IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**){return -1;}
 bool attach(IOService*){return !attachFail;}
 virtual void registerService(IOOptionBits options=0){
  if(publishFail)return;
  if(options&kIOServiceSynchronous){assert(!(options&kIOServiceAsynchronous));syncRegistrations++;}
  else {asyncRegistrations++;if(deferredMatching){pendingServices.push_back(this);return;}}
  services.push_back(foreignMatch?getResourceService():this);
  if(duplicateMatch)services.push_back(getResourceService());
 }
 void setProperty(const char*,const char*){}
 void setProperty(const char*,bool){}
 static IOService *getResourceService(){static IOService resource;return &resource;}
 static OSDictionary *serviceMatching(const char *name){assert(!strcmp(name,"IOWatchdog"));return new OSDictionary;}
 static OSIterator *getMatchingServices(OSDictionary*){return services.empty()&&!iteratorInvalid?nullptr:new OSIterator;}
};
static_assert(sizeof(IOService)==136);
OSObject *OSIterator::getNextObject(){return at<services.size()?services[at++]:nullptr;}
class IOWatchdog:public IOService {
 char originalPrivate[112]{};
public:
 static OSMetaClass gMetaClass;
 void stop(IOService*)override {disableBackend();}
 IOReturn newUserClient(task_t,void*,UInt32 type,OSDictionary*,IOUserClient**client)override {
  originalOpenCalls++;if(!privileged)return kIOReturnNotPrivileged;if(type!=1)return kIOReturnBadArgument;*client=&originalClient;return 0;
 }
 virtual void enableBackend()=0;virtual void disableBackend()=0;virtual void restartBackend()=0;
};
OSMetaClass IOWatchdog::gMetaClass{248};
static_assert(sizeof(IOWatchdog)==248);
const OSMetaClass *OSMetaClass::getMetaClassWithName(const OSSymbol *s){static OSMetaClass client{232},bad{247};if(s->text=="KebabPlatformWatchdog")return subclassMeta();if(s->text=="IOWatchdog")return badBase?&bad:&IOWatchdog::gMetaClass;return badClient?&bad:&client;}
#define OSSafeReleaseNULL(p) do {delete p;p=nullptr;}while(0)
#define OSDeclareDefaultStructors(T) public:static OSMetaClass gMetaClass;
#define OSDefineMetaClassAndStructors(T,B) OSMetaClass T::gMetaClass{sizeof(T),&B::gMetaClass};
struct kmod_info_t{const char *name;};
extern "C" kmod_info_t g_kernel_kmod_info={"kernel"};
extern "C" int OSKextLoadKextWithIdentifier(const char *s){assert(!strcmp(s,"com.apple.driver.AppleARMWatchdogTimer"));return loadError;}
static bool PE_parse_boot_argn(const char *name,void *out,int n){assert(!strcmp(name,"ram-system-watchdog")&&n==4);memcpy(out,&bootArg,4);return true;}
static void setField(IOWatchdog *w,unsigned offset,uint64_t v){memcpy((char*)w+offset,&v,8);}
extern "C" bool kebab_original_watchdog_start(IOWatchdog *w,IOService *provider,unsigned unused,unsigned flags){
 assert(!unused&&!flags&&provider);if(startFail)return false;
 IOSimpleLock *lock=&originalLock;memcpy((char*)w+0xa0,&lock,sizeof(lock));*((char*)w+0xa8)=1;
 setField(w,0xb8,0);setField(w,0xc8,now+600ULL*NSEC_PER_SEC);w->registerService();w->enableBackend();return true;
}

// Adapted from mouse233/unixnu-fuxi RamWatchdog.cpp (MIT), blob 29b2a369bc1d6b8ff3ea5b27320fcca5e9e403d9.
// Requalified for original iOS16 20H392; no shared addresses with iOS17.
// SPDX-License-Identifier: MIT
// RAM bring-up backend for the original 20H392 IOWatchdog/client protocol.
// This is a software deadline observer, not a claim of Snapdragon hardware WDT.

extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
bool kebab_original_watchdog_start(IOWatchdog *, IOService *, unsigned, unsigned);
// stage, accepted clients, polls, original heartbeat sequence, enabled, deadline,
// backend enable count, backend disable count. RAM only, never a disk log.
volatile uint64_t kebab_watchdog_state[8];
const char *kebab_watchdog_step = "arguments";
int kebab_watchdog_detail;
int kebab_watchdog_prepare(void);
}

class KebabPlatformWatchdog : public IOWatchdog {
    // Owner Sandbox policy maps its existing Apple platform-watchdog class
    // predicate to this real native class. No Apple hardware identity alias.
    OSDeclareDefaultStructors(KebabPlatformWatchdog)
    thread_call_t timer = nullptr;
    unsigned armed = 0;
    static void expired(thread_call_param_t parameter, thread_call_param_t) {
        auto *self = static_cast<KebabPlatformWatchdog *>(parameter);
        if (!__atomic_load_n(&self->armed, __ATOMIC_ACQUIRE)) return;
        // These fields and their lock are qualified from the original methods
        // that enable/disable/pet the watchdog. Never write original state here.
        auto *lock = *reinterpret_cast<IOSimpleLock **>(reinterpret_cast<char *>(self) + 0xa0);
        if (!lock) panic("KEBAB WATCHDOG missing original heartbeat lock");
        bool interrupts = ml_set_interrupts_enabled(false);
        IOSimpleLockLock(lock);
        bool enabled = *reinterpret_cast<unsigned char *>(reinterpret_cast<char *>(self) + 0xa8);
        uint64_t sequence = *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(self) + 0xb8);
        uint64_t deadline = *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(self) + 0xc8);
        uint64_t now = mach_absolute_time();
        IOSimpleLockUnlock(lock);
        ml_set_interrupts_enabled(interrupts);
        __atomic_add_fetch(&kebab_watchdog_state[2], 1, __ATOMIC_RELAXED);
        kebab_watchdog_state[3] = sequence;
        kebab_watchdog_state[4] = enabled;
        kebab_watchdog_state[5] = deadline;
        if (enabled && (!deadline || now >= deadline))
            panic("KEBAB WATCHDOG software deadline expired: beats=%llu now=%llx deadline=%llx",
                sequence, now, deadline);
        uint64_t polls=kebab_watchdog_state[2];
        if(polls<=3 || !(polls%30)) IOLog("KEBAB WATCHDOG: clients=%llu polls=%llu beats=%llu enabled=%u remaining=%lld\n",kebab_watchdog_state[1],polls,sequence,enabled,(long long)(deadline-now));
        if (__atomic_load_n(&self->armed, __ATOMIC_ACQUIRE)) self->schedule();
    }
    void schedule() {
        uint64_t next;
        clock_interval_to_deadline(1, NSEC_PER_SEC, &next);
        thread_call_enter_delayed(timer, next);
    }
public:
    void registerService(IOOptionBits options = 0) override {
        // The original start calls this virtual with options=0. Its asynchronous
        // default does not guarantee the MatchedState required by our lookup.
        IOService::registerService((options & ~IOOptionBits(kIOServiceAsynchronous)) | kIOServiceSynchronous);
    }
    bool prepareTimer() {
        timer = thread_call_allocate(expired, this);
        return timer != nullptr;
    }
    bool start(IOService *provider) override {
        // Same option arguments as the original hardware subclass's zero-flag
        // path; no boot-arg change to the original monitoring/permission policy.
        return timer && kebab_original_watchdog_start(this, provider, 0, 0);
    }
    void stop(IOService *provider) override {
        IOWatchdog::stop(provider);
    }
    void free() override {
        __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
        if (timer) { thread_call_cancel_wait(timer); thread_call_free(timer); timer = nullptr; }
        IOService::free();
    }
    IOReturn newUserClient(task_t task, void *security, UInt32 type,
                          OSDictionary *properties, IOUserClient **client) override {
        IOReturn result = IOWatchdog::newUserClient(task, security, type, properties, client);
        if (result == kIOReturnSuccess && client && *client) {
            __atomic_add_fetch(&kebab_watchdog_state[1], 1, __ATOMIC_RELAXED);
            IOLog("KEBAB WATCHDOG: original user-client accepted type=%u\n",type);
        }
        return result;
    }
    // Exact three abstract slots of the original IOWatchdog, after IOService.
    // The deadline is owned by its original heartbeat/enable/disable handlers.
    void enableBackend() override {
        __atomic_store_n(&armed, 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&kebab_watchdog_state[6], 1, __ATOMIC_RELAXED);
        schedule();
    }
    void disableBackend() override {
        __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
        __atomic_add_fetch(&kebab_watchdog_state[7], 1, __ATOMIC_RELAXED);
        if (timer) thread_call_cancel(timer);
    }
    void restartBackend() override {
        // Re-evaluate the actual original deadline; do not manufacture a pet.
        if (__atomic_load_n(&armed, __ATOMIC_ACQUIRE)) schedule();
    }
};
OSDefineMetaClassAndStructors(KebabPlatformWatchdog, IOWatchdog)

static KebabPlatformWatchdog *watchdog; // Published service retained for this RAM boot.
static const OSMetaClass *lookup(const char *name) {
    const OSSymbol *key = OSSymbol::withCString(name);
    const OSMetaClass *meta = key ? OSMetaClass::getMetaClassWithName(key) : nullptr;
    OSSafeReleaseNULL(key);
    return meta;
}
static int countServices(IOService *expected) {
    OSDictionary *match = IOService::serviceMatching("IOWatchdog");
    if (!match) return -ENOMEM;
    OSIterator *iterator = IOService::getMatchingServices(match);
    match->release();
    if (!iterator) return 0;
    int count = 0;
    bool owns = expected == nullptr;
    while (OSObject *object = iterator->getNextObject()) {
        ++count;
        if (object == expected) owns = true;
    }
    bool valid = iterator->isValid();
    iterator->release();
    if (!valid) return -EAGAIN;
    return owns ? count : -ENXIO;
}
extern "C" int kebab_watchdog_prepare(void) {
    kebab_watchdog_step = "arguments";
    kebab_watchdog_detail = 0;
    if (watchdog) return EALREADY;
    kebab_watchdog_step = "load";
    kebab_watchdog_detail = OSKextLoadKextWithIdentifier("com.apple.driver.AppleARMWatchdogTimer");
    if (kebab_watchdog_detail) return ENXIO;
    kebab_watchdog_state[0] = 1;
    kebab_watchdog_step = "metaclass";
    const OSMetaClass *base = lookup("IOWatchdog"), *client = lookup("IOWatchdogUserClient");
    if (!base || base != &IOWatchdog::gMetaClass || base->getClassSize() != 0xf8 ||
        !client || client->getClassSize() != 0xe8) return ENOEXEC;
    kebab_watchdog_step = "preexisting";
    int before = countServices(nullptr);
    kebab_watchdog_detail = before;
    if (before != 0) return before < 0 ? -before : EEXIST;
    const OSMetaClass *subclass = lookup("KebabPlatformWatchdog");
    if (!subclass || subclass->getSuperClass() != base) return ENOEXEC;
    kebab_watchdog_step = "provider";
    IOService *provider = IOService::getResourceService();
    if (!provider) return ENXIO;
    kebab_watchdog_step = "allocate";
    watchdog = new KebabPlatformWatchdog;
    if (!watchdog) return ENOMEM;
    kebab_watchdog_step = "init";
    if (!watchdog->init()) return ENOMEM;
    kebab_watchdog_step = "timer";
    if (!watchdog->prepareTimer()) return ENOMEM;
    watchdog->setProperty("UniXNUWatchdogBackend", "software-original-deadline");
    watchdog->setProperty("HardwareWatchdogImplemented", false);
    kebab_watchdog_step = "attach";
    if (!watchdog->attach(provider)) return ENXIO;
    kebab_watchdog_step = "start";
    if (!watchdog->start(provider)) return ENXIO;
    kebab_watchdog_state[0] = 2;
    kebab_watchdog_step = "match";
    kebab_watchdog_detail = countServices(watchdog);
    if (kebab_watchdog_detail != 1)
        return kebab_watchdog_detail < 0 ? -kebab_watchdog_detail :
            kebab_watchdog_detail > 1 ? EEXIST : ENXIO;
    kebab_watchdog_state[0] = 3;
    kebab_watchdog_step = "ready";
    IOLog("KEBAB WATCHDOG registered matched started: original IOWatchdog/client; software deadline backend\n");
    return 0;
}
static const OSMetaClass *subclassMeta(){return &KebabPlatformWatchdog::gMetaClass;}
static unsigned cases;
static void reset(){
 if(watchdog){watchdog->free();watchdog=nullptr;}services.clear();
 pendingServices.clear();deferredMatching=true;publishFail=foreignMatch=duplicateMatch=false;
 syncRegistrations=asyncRegistrations=0;
 badBase=badClient=preFail=postFail=iteratorInvalid=initFail=attachFail=startFail=timerFail=false;
 privileged=true;loadError=0;bootArg=1;now=NSEC_PER_SEC;originalLock.held=false;interrupts=true;
 memset((void*)kebab_watchdog_state,0,sizeof(kebab_watchdog_state));
}
static void poll(){assert(lastTimer&&lastTimer->active);auto *t=lastTimer;t->active=false;t->fn(t->parameter,nullptr);}
int main(){
 reset();loadError=(int)0xdc00800d;assert(kebab_watchdog_prepare()==ENXIO);
 assert(!strcmp(kebab_watchdog_step,"load")&&kebab_watchdog_detail==loadError);cases++;
 reset();badBase=true;assert(kebab_watchdog_prepare()==ENOEXEC);cases++;
 reset();badClient=true;assert(kebab_watchdog_prepare()==ENOEXEC);cases++;
 reset();services.push_back(IOService::getResourceService());assert(kebab_watchdog_prepare()==EEXIST&&!watchdog&&!strcmp(kebab_watchdog_step,"preexisting"));cases++;
 reset();iteratorInvalid=true;assert(kebab_watchdog_prepare()==EAGAIN);cases++;
 reset();initFail=true;assert(kebab_watchdog_prepare()==ENOMEM);cases++;
 reset();timerFail=true;assert(kebab_watchdog_prepare()==ENOMEM);cases++;
 reset();attachFail=true;assert(kebab_watchdog_prepare()==ENXIO&&!strcmp(kebab_watchdog_step,"attach"));cases++;
 reset();startFail=true;assert(kebab_watchdog_prepare()==ENXIO&&!strcmp(kebab_watchdog_step,"start"));cases++;
 reset();publishFail=true;assert(kebab_watchdog_prepare()==ENXIO&&!strcmp(kebab_watchdog_step,"match")&&kebab_watchdog_detail==0);cases++;
 reset();foreignMatch=true;assert(kebab_watchdog_prepare()==ENXIO&&kebab_watchdog_detail==-ENXIO);cases++;
 reset();duplicateMatch=true;assert(kebab_watchdog_prepare()==EEXIST&&kebab_watchdog_detail==2);cases++;
 reset();assert(!kebab_watchdog_prepare());assert(kebab_watchdog_state[0]==3&&countServices(watchdog)==1);cases++;
 assert(syncRegistrations==1&&!asyncRegistrations&&pendingServices.empty());cases++;
 assert(!strcmp(kebab_watchdog_step,"ready")&&kebab_watchdog_detail==1);cases++;
 assert(kebab_watchdog_prepare()==EALREADY);cases++;
 IOUserClient *client=nullptr;privileged=false;assert(watchdog->newUserClient(nullptr,nullptr,1,nullptr,&client)==kIOReturnNotPrivileged&&!client);cases++;
 privileged=true;assert(watchdog->newUserClient(nullptr,nullptr,2,nullptr,&client)==kIOReturnBadArgument&&!client);cases++;
 assert(!watchdog->newUserClient(nullptr,nullptr,1,nullptr,&client)&&client==&originalClient&&kebab_watchdog_state[1]==1);cases++;
 assert(originalOpenCalls>=3);cases++;
 poll();assert(lastTimer->active&&kebab_watchdog_state[2]==1&&kebab_watchdog_state[4]==1);cases++;
 setField(watchdog,0xb8,17);setField(watchdog,0xc8,now+90ULL*NSEC_PER_SEC);now+=NSEC_PER_SEC;poll();assert(kebab_watchdog_state[3]==17);cases++;
 *((char*)watchdog+0xa8)=0;now+=1000ULL*NSEC_PER_SEC;poll();assert(lastTimer->active&&kebab_watchdog_state[4]==0);cases++;
 *((char*)watchdog+0xa8)=1;setField(watchdog,0xc8,now+90ULL*NSEC_PER_SEC);poll();assert(lastTimer->active);cases++;
 now+=91ULL*NSEC_PER_SEC;bool caught=false;try{poll();}catch(const Panic &p){caught=strstr(p.what(),"software deadline expired");}assert(caught&&!originalLock.held&&interrupts);cases++;
 reset();assert(!kebab_watchdog_prepare());setField(watchdog,0xc8,0);caught=false;try{poll();}catch(const Panic&){caught=true;}assert(caught);cases++;
 reset();assert(!kebab_watchdog_prepare());watchdog->stop(IOService::getResourceService());assert(!lastTimer->active&&kebab_watchdog_state[7]==1);cases++;
 reset();assert(!lastTimer);cases++;
 reset();deferredMatching=false;assert(!kebab_watchdog_prepare()&&syncRegistrations==1);cases++;
 reset();assert(!kebab_watchdog_prepare());services.clear();watchdog->registerService(kIOServiceAsynchronous);
 assert(syncRegistrations==2&&!asyncRegistrations&&pendingServices.empty()&&countServices(watchdog)==1);cases++;
 reset();
 printf("PASS %u actual C++ watchdog lifecycle/deadline/refusal cases; original APIs explicitly mocked\n",cases);
}
