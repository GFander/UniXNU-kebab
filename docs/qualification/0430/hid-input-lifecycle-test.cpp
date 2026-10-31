
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <map>
#include <mutex>
#include <future>
#include <condition_variable>
#include "kebab_hid_reports.h"
using IOReturn=int;using IOOptionBits=unsigned;using OSObject=struct Object;
struct Object {virtual ~Object()=default;};
struct OSDictionary{};
enum IOHIDReportType {kIOHIDReportTypeInput,kIOHIDReportTypeOutput,kIOHIDReportTypeFeature};
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,kIOReturnNoMemory=3,
 kIOReturnNoDevice=4,kIOReturnUnsupported=5,kIOReturnNoSpace=6,kIOReturnVMError=7,kIODirectionNone=0};
#define LCK_GRP_DECLARE(name,...) unsigned name=0
#define LCK_MTX_DECLARE(name,...) std::mutex name
static void lck_mtx_lock(std::mutex *p){p->lock();}
static void lck_mtx_unlock(std::mutex *p){p->unlock();}
#define OSDeclareDefaultStructors(...)
#define OSDefineMetaClassAndStructors(...)
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
template<typename... T> static void fixtureLog(const char *,T... ){}
#define IOLog(...) fixtureLog(__VA_ARGS__)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(0)
static std::string failure;
static unsigned defaultMask=3,publications=0,originalStops=0,originalReports=0;
static bool reportFail=false,blockReport=false,reportEntered=false,reportLeave=false;
static std::mutex reportMutex;static std::condition_variable reportCondition;
static std::map<std::string,int> live;
struct Resource:Object {
 std::string kind;unsigned refs=1;
 explicit Resource(const char *k):kind(k){live[kind]++;}
 ~Resource(){live[kind]--;}
 void retain(){refs++;}void release(){assert(refs);if(!--refs)delete this;}
};
struct OSNumber:Object {unsigned value;explicit OSNumber(unsigned v):value(v){}
 unsigned unsigned32BitValue(){return value;}static OSNumber *withNumber(unsigned v,unsigned){return new OSNumber(v);}};
struct OSString:Object {static OSString *withCString(const char *){return new OSString;}};
struct IOWorkLoop;
struct IOService:Object {
 std::string name;OSNumber roleProperty{0};
 explicit IOService(const char *s="IOService"):name(s){}
 virtual bool start(IOService *){return failure!="super-start";}
 virtual void stop(IOService *){}virtual void free(){}
 virtual IOWorkLoop *getWorkLoop()const{return nullptr;}
 void *metaCast(const char *s){return name==s?this:nullptr;}
 Object *getProperty(const char *){return &roleProperty;}
 void setProperty(const char *,bool){}void setProperty(const char *,unsigned,unsigned){}
 void registerService(){publications++;}
};
struct IOTimerEventSource:Resource {
 bool armed=false,added=false;unsigned timeout=0;
 IOTimerEventSource():Resource("timer"){}
 static IOTimerEventSource *timerEventSource(OSObject *,void(*)(OSObject *,IOTimerEventSource *)){
  return failure=="timer"?nullptr:new IOTimerEventSource;}
 void setTimeoutMS(unsigned n){armed=true;timeout=n*1000;}
 void setTimeoutUS(unsigned n){armed=true;timeout=n;}
 void cancelTimeout(){armed=false;}
};
struct IOWorkLoop:Resource {
 std::recursive_mutex mutex;
 IOWorkLoop():Resource("loop"){}
 static IOWorkLoop *workLoop(){return failure=="loop"?nullptr:new IOWorkLoop;}
 IOReturn addEventSource(IOTimerEventSource *t){if(failure=="add")return 99;assert(!t->added);t->added=true;return 0;}
 void removeEventSource(IOTimerEventSource *t){assert(t->added);t->added=false;}
 IOReturn runAction(IOReturn(*f)(OSObject *,void *,void *,void *,void *),OSObject *o){
  std::lock_guard<std::recursive_mutex> guard(mutex);return f(o,nullptr,nullptr,nullptr,nullptr);}
};
struct IOMemoryDescriptor:Resource {
 uint8_t bytes[1024]={};unsigned length;
 explicit IOMemoryDescriptor(unsigned n):Resource("memory"),length(n){}
 unsigned getLength(){return length;}
 unsigned writeBytes(unsigned at,const void *b,unsigned n){if(failure=="write")return 0;assert(at+n<=length);memcpy(bytes+at,b,n);return n;}
};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor {
 explicit IOBufferMemoryDescriptor(unsigned n):IOMemoryDescriptor(n){}
 static IOBufferMemoryDescriptor *withBytes(const void *b,unsigned n,unsigned){
  if(failure=="memory")return nullptr;auto p=new IOBufferMemoryDescriptor(n);memcpy(p->bytes,b,n);return p;}
};
// Explicit substitute: tests do not implement or certify original HID parser,
// IOService termination, hardware transactions or actual HID event consumption.
struct IOHIDDevice:IOService {
 IOWorkLoop *retained=nullptr;
 virtual IOReturn newReportDescriptor(IOMemoryDescriptor **)const=0;
 virtual IOReturn getReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits)=0;
 bool start(IOService *)override{
  if(failure=="original"){stop(nullptr);return false;}
  IOMemoryDescriptor *d=nullptr;
  if(newReportDescriptor(&d)){stop(nullptr);return false;}
  assert(d&&d->getLength()>0);d->release();
  retained=getWorkLoop();assert(retained);retained->retain();return true;
 }
 void stop(IOService *)override{originalStops++;}
 void free()override{OSSafeReleaseNULL(retained);}
 IOReturn handleReportWithTime(uint64_t t,IOMemoryDescriptor *m,IOHIDReportType type,unsigned){
  assert(t&&m&&type==kIOHIDReportTypeInput);originalReports++;
  if(blockReport){std::unique_lock<std::mutex> lock(reportMutex);reportEntered=true;reportCondition.notify_all();reportCondition.wait(lock,[]{return reportLeave;});}
  return reportFail?91:0;
 }
};
static bool PE_parse_boot_argn(const char *,void *,unsigned){return false;}
static bool PE_get_default(const char *,void *p,unsigned n){assert(n==4);memcpy(p,&defaultMask,4);return true;}
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t *p){*p=n;}
static LCK_GRP_DECLARE(khid_group,"kebab-hid");
static LCK_MTX_DECLARE(khid_lock,&khid_group);
struct KebabHIDChannel {
    kebab_hid_queue queue;
    kebab_hid_backend backend;
    uint64_t sent;
    uint32_t result;
};
static KebabHIDChannel channels[3];
static bool validRole(unsigned role) {return role==KEBAB_HID_KEYS || role==KEBAB_HID_TOUCH;}
extern "C" int
kebab_hid_backend_requested(unsigned role)
{
    if (!validRole(role)) return 0;
    uint32_t legacy=0;
    bool specified=PE_parse_boot_argn(role==KEBAB_HID_KEYS?"kebab_keys":"kebab_touch",&legacy,sizeof(legacy));
    if (specified && legacy==0) return 0;
    if (specified && legacy==2) return 1;
    uint32_t mask=0;
    return PE_get_default("kebab,hid-input",&mask,sizeof(mask)) && !(mask&~3u) &&
        (mask&(1u<<(role-1)));
}

extern "C" void
kebab_hid_backend_ready(unsigned role,unsigned interval,unsigned contacts)
{
    if (!validRole(role) || interval<1000 || interval>1000000 ||
        (role==KEBAB_HID_TOUCH && (!contacts || contacts>KEBAB_HID_CONTACTS))) return;
    lck_mtx_lock(&khid_lock);
    channels[role].backend.ready=1;
    channels[role].backend.interval_us=interval;
    channels[role].backend.contacts=contacts;
    channels[role].backend.error=0;
    lck_mtx_unlock(&khid_lock);
}
extern "C" void
kebab_hid_backend_error(unsigned role,unsigned error)
{
    if (!validRole(role)) return;
    lck_mtx_lock(&khid_lock);
    if (error || channels[role].backend.error==EIO) channels[role].backend.error=error;
    lck_mtx_unlock(&khid_lock);
}
extern "C" void
kebab_hid_backend_sample(unsigned role,const kebab_hid_frame *frame)
{
    if (!validRole(role)) return;
    lck_mtx_lock(&khid_lock);
    KebabHIDChannel &c=channels[role];
    if ((role==KEBAB_HID_TOUCH && c.backend.ready && frame &&
        (frame->active & ~((1u<<c.backend.contacts)-1))) ||
        !kebab_hid_queue_push(&c.queue,role,frame)) c.backend.error=EINVAL;
    else if (c.backend.ready) c.backend.error=0;
    lck_mtx_unlock(&khid_lock);
}
static kebab_hid_backend
backend(unsigned role)
{
    kebab_hid_backend status={};
    lck_mtx_lock(&khid_lock);
    if (validRole(role)) {
        status=channels[role].backend;
        status.produced=channels[role].queue.produced;
        status.dropped=channels[role].queue.dropped;
    }
    lck_mtx_unlock(&khid_lock);return status;
}
static bool claim(unsigned role,uintptr_t owner)
{
    lck_mtx_lock(&khid_lock);
    bool ok=validRole(role) && channels[role].backend.ready && !channels[role].backend.error &&
        kebab_hid_queue_claim(&channels[role].queue,owner);
    lck_mtx_unlock(&khid_lock);return ok;
}
static void unclaim(unsigned role,uintptr_t owner)
{
    lck_mtx_lock(&khid_lock);
    if (validRole(role)) (void)kebab_hid_queue_release(&channels[role].queue,owner);
    lck_mtx_unlock(&khid_lock);
}
static bool pop(unsigned role,uintptr_t owner,kebab_hid_frame *out)
{
    lck_mtx_lock(&khid_lock);
    bool ok=validRole(role) && !channels[role].backend.error &&
        kebab_hid_queue_pop(&channels[role].queue,owner,out);
    lck_mtx_unlock(&khid_lock);return ok;
}
static bool latest(unsigned role,uintptr_t owner,kebab_hid_frame *out)
{
    lck_mtx_lock(&khid_lock);
    bool ok=validRole(role) && channels[role].backend.ready && !channels[role].backend.error &&
        channels[role].queue.owner==owner && channels[role].queue.latest_valid;
    if (ok) *out=channels[role].queue.latest;
    lck_mtx_unlock(&khid_lock);return ok;
}
extern "C" void kebab_hid_input_report(void);
extern "C" void
kebab_hid_input_report(void)
{
    /* Copy status only: never place the two complete queues on kernel stack. */
    for (unsigned role=1;role<=2;role++) {
        kebab_hid_backend state={};uint64_t sent;uint32_t result;
        lck_mtx_lock(&khid_lock);
        state=channels[role].backend;state.produced=channels[role].queue.produced;
        state.dropped=channels[role].queue.dropped;sent=channels[role].sent;result=channels[role].result;
        lck_mtx_unlock(&khid_lock);
        IOLog("KEBAB HID: role=%u ready=%u interval=%u contacts=%u backend-error=%u produced=%llu dropped=%llu sent=%llu result=0x%x\n",
            role,state.ready,state.interval_us,state.contacts,state.error,state.produced,state.dropped,sent,result);
    }
}

struct KebabHIDTransport : public IOService {
    OSDeclareDefaultStructors(KebabHIDTransport);
    IOWorkLoop *loop=nullptr;
    IOTimerEventSource *timer=nullptr;
    unsigned role=0,attempts=0;
    bool added=false,stopping=true,published=false;
    static void tick(OSObject *,IOTimerEventSource *);
    static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
    void stopTimer();
public:
    bool start(IOService *);
    void stop(IOService *);
    void free();
};
OSDefineMetaClassAndStructors(KebabHIDTransport,IOService);
bool KebabHIDTransport::start(IOService *provider)
{
    OSNumber *number=OSDynamicCast(OSNumber,getProperty("kebab,hid-role"));
    role=number?number->unsigned32BitValue():0;
    if (!provider || !validRole(role) ||
        !provider->metaCast(role==KEBAB_HID_KEYS?"KebabPmicKeys":"KebabTouch")) return false;
    if (!kebab_hid_backend_requested(role)) return false;
    if (!IOService::start(provider)) return false;
    loop=IOWorkLoop::workLoop();
    timer=IOTimerEventSource::timerEventSource(this,tick);
    if (!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess) {
        stopTimer();IOService::stop(provider);return false;
    }
    added=true;stopping=false;timer->setTimeoutMS(50);
    IOLog("KEBAB HID: transport role=%u waiting for actual backend initialization\n",role);
    return true;
}
void KebabHIDTransport::tick(OSObject *owner,IOTimerEventSource *source)
{
    KebabHIDTransport *self=static_cast<KebabHIDTransport *>(owner);
    if (self->stopping) return;
    kebab_hid_backend status=backend(self->role);
    if (status.ready && !status.error) {
        self->published=true;
        self->setProperty("kebab,hid-role",self->role,32);
        self->setProperty("BackendReady",true);
        IOLog("KEBAB HID: transport role=%u actual backend ready; publish original HID device matching\n",self->role);
        self->registerService();return;
    }
    if (++self->attempts>=2400) {
        self->setProperty("BackendReady",false);
        IOLog("KEBAB HID: transport role=%u unavailable after120s ready=%u error=%u; no HID device published\n",self->role,status.ready,status.error);return;
    }
    source->setTimeoutMS(50);
}
IOReturn KebabHIDTransport::stopAction(OSObject *owner,void *,void *,void *,void *)
{
    KebabHIDTransport *self=static_cast<KebabHIDTransport *>(owner);self->stopping=true;
    if (self->timer) {
        self->timer->cancelTimeout();
        if (self->added) {self->loop->removeEventSource(self->timer);self->added=false;}
    }
    return kIOReturnSuccess;
}
void KebabHIDTransport::stopTimer()
{
    if (loop) loop->runAction(stopAction,this);else stopAction(this,nullptr,nullptr,nullptr,nullptr);
}
void KebabHIDTransport::stop(IOService *provider) {stopTimer();IOService::stop(provider);}
void KebabHIDTransport::free() {stopTimer();OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);IOService::free();}

struct KebabHIDDevice : public IOHIDDevice {
    OSDeclareDefaultStructors(KebabHIDDevice);
    IOWorkLoop *loop=nullptr;
    IOTimerEventSource *timer=nullptr;
    unsigned role=0,interval=0,contacts=0;
    bool added=false,claimed=false,originalStarted=false,stopping=true,faulted=false;
    kebab_hid_frame previous={};
    bool havePrevious=false;
    uint64_t sent=0,lastDropped=0;
    static void tick(OSObject *,IOTimerEventSource *);
    static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
    void stopInput();
public:
    bool start(IOService *);
    void stop(IOService *);
    void free();
    IOWorkLoop *getWorkLoop() const {return loop;}
    OSString *newTransportString() const {return OSString::withCString(role==KEBAB_HID_KEYS?"SPMI":"I2C");}
    OSString *newManufacturerString() const {return OSString::withCString(role==KEBAB_HID_KEYS?"Qualcomm":"Synaptics");}
    OSString *newProductString() const {return OSString::withCString(role==KEBAB_HID_KEYS?"PM8150 physical keys":"S3908 touchscreen");}
    OSNumber *newReportIntervalNumber() const {return OSNumber::withNumber(interval,32);}
    IOReturn newReportDescriptor(IOMemoryDescriptor **) const;
    IOReturn getReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits);
};
OSDefineMetaClassAndStructors(KebabHIDDevice,IOHIDDevice);
IOReturn KebabHIDDevice::newReportDescriptor(IOMemoryDescriptor **out) const
{
    if (!out) return kIOReturnBadArgument;
    *out=nullptr;
    if (!validRole(role)) return kIOReturnNotReady;
    const uint8_t *bytes=role==KEBAB_HID_KEYS?kebab_hid_keys_descriptor:kebab_hid_touch_descriptor;
    unsigned n=role==KEBAB_HID_KEYS?sizeof(kebab_hid_keys_descriptor):sizeof(kebab_hid_touch_descriptor);
    *out=IOBufferMemoryDescriptor::withBytes(bytes,n,kIODirectionNone);
    return *out?kIOReturnSuccess:kIOReturnNoMemory;
}
bool KebabHIDDevice::start(IOService *provider)
{
    if (!provider || !provider->metaCast("KebabHIDTransport")) return false;
    OSNumber *number=OSDynamicCast(OSNumber,provider->getProperty("kebab,hid-role"));
    role=number?number->unsigned32BitValue():0;
    kebab_hid_backend status=backend(role);
    if (!validRole(role) || !status.ready || status.error) return false;
    interval=status.interval_us;contacts=status.contacts;
    loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
    if (!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess) {stopInput();return false;}
    added=true;
    if (!claim(role,(uintptr_t)this)) {stopInput();return false;}
    claimed=true;
    setProperty("Built-In",true);
    if (role==KEBAB_HID_TOUCH) {
        setProperty("DisplayIntegrated",true);setProperty("DigitizerCollectionDispatch",true);
    }
    __atomic_store_n(&stopping,false,__ATOMIC_RELEASE);
    /* Original start invokes virtual stop on partial failure. Arm ownership
     * before entering it so that callback also cleans original event sources. */
    originalStarted=true;
    if (!IOHIDDevice::start(provider)) {
        stopInput();
        if (originalStarted) {IOHIDDevice::stop(provider);originalStarted=false;}
        return false;
    }
    IOLog("KEBAB HID: original IOHIDDevice started role=%u interval=%u contacts=%u; real snapshots only\n",role,interval,contacts);
    timer->setTimeoutUS(interval);return true;
}
IOReturn KebabHIDDevice::getReport(IOMemoryDescriptor *memory,IOHIDReportType type,IOOptionBits options)
{
    if (!memory) return kIOReturnBadArgument;
    if (__atomic_load_n(&stopping,__ATOMIC_ACQUIRE)) return kIOReturnNoDevice;
    uint8_t report[KEBAB_HID_TOUCH_BYTES]={};unsigned n=0,id=options&255;
    if (role==KEBAB_HID_TOUCH && type==kIOHIDReportTypeFeature && id==KEBAB_HID_CONTACT_FEATURE) {
        report[0]=KEBAB_HID_CONTACT_FEATURE;report[1]=(uint8_t)contacts;n=2;
    } else if (type==kIOHIDReportTypeInput && id==(role==KEBAB_HID_KEYS?KEBAB_HID_KEYS_REPORT:KEBAB_HID_TOUCH_REPORT)) {
        kebab_hid_frame frame={};
        if (!latest(role,(uintptr_t)this,&frame)) return kIOReturnNotReady;
        n=kebab_hid_encode(role,&frame,nullptr,report,sizeof(report));
    } else return kIOReturnUnsupported;
    if (!n) return kIOReturnBadArgument;
    if (memory->getLength()<n) return kIOReturnNoSpace;
    return memory->writeBytes(0,report,n)==n?kIOReturnSuccess:kIOReturnVMError;
}
void KebabHIDDevice::tick(OSObject *owner,IOTimerEventSource *source)
{
    KebabHIDDevice *self=static_cast<KebabHIDDevice *>(owner);
    bool stopped=__atomic_load_n(&self->stopping,__ATOMIC_ACQUIRE);
    if (stopped || self->faulted) return;
    kebab_hid_backend state=backend(self->role);
    if (state.dropped!=self->lastDropped) {
        IOLog("KEBAB HID: role=%u queue loss=%llu; input history not certified\n",self->role,state.dropped);
        self->lastDropped=state.dropped;
    }
    unsigned consumed=0;
    for (;consumed<8;consumed++) {
        kebab_hid_frame frame={};
        if (!pop(self->role,(uintptr_t)self,&frame)) break;
        uint8_t report[KEBAB_HID_TOUCH_BYTES]={};
        unsigned n=kebab_hid_encode(self->role,&frame,self->havePrevious?&self->previous:nullptr,report,sizeof(report));
        IOBufferMemoryDescriptor *memory=n?IOBufferMemoryDescriptor::withBytes(report,n,kIODirectionNone):nullptr;
        uint64_t timestamp=0;nanoseconds_to_absolutetime(frame.timestamp_ns,&timestamp);
        IOReturn result=memory?self->handleReportWithTime(timestamp,memory,kIOHIDReportTypeInput,0):(n?kIOReturnNoMemory:kIOReturnBadArgument);
        OSSafeReleaseNULL(memory);
        lck_mtx_lock(&khid_lock);
        channels[self->role].result=result;
        if (result==kIOReturnSuccess) channels[self->role].sent++;
        lck_mtx_unlock(&khid_lock);
        if (result!=kIOReturnSuccess) {
            self->faulted=true;
            self->setProperty("InputDispatchError",result,32);
            IOLog("KEBAB HID: role=%u original report failed=0x%x time=%llu; no ambiguous automatic retry\n",self->role,result,frame.timestamp_ns);return;
        }
        self->previous=frame;self->havePrevious=true;self->sent++;
        if (self->sent<=16 || !(self->sent&(self->sent-1)))
            IOLog("KEBAB HID: report role=%u count=%llu time=%llu keys=%u active=0x%x original-result=0\n",self->role,self->sent,frame.timestamp_ns,frame.keys,frame.active);
    }
    source->setTimeoutUS(consumed==8?1000:self->interval);
}
IOReturn KebabHIDDevice::stopAction(OSObject *owner,void *,void *,void *,void *)
{
    KebabHIDDevice *self=static_cast<KebabHIDDevice *>(owner);
    __atomic_store_n(&self->stopping,true,__ATOMIC_RELEASE);
    if (self->timer) {
        self->timer->cancelTimeout();
        if (self->added) {self->loop->removeEventSource(self->timer);self->added=false;}
    }
    if (self->claimed) {unclaim(self->role,(uintptr_t)self);self->claimed=false;}
    return kIOReturnSuccess;
}
void KebabHIDDevice::stopInput()
{
    if (loop) loop->runAction(stopAction,this);else stopAction(this,nullptr,nullptr,nullptr,nullptr);
}
void KebabHIDDevice::stop(IOService *provider)
{
    stopInput();if (originalStarted) {IOHIDDevice::stop(provider);originalStarted=false;}
}
void KebabHIDDevice::free()
{
    stopInput();
    OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);
    IOHIDDevice::free();
}

static void clean(){for(auto &p:live)assert(!p.second);}
static void reset(){clean();memset(channels,0,sizeof(channels));failure="";publications=originalReports=originalStops=0;reportFail=blockReport=false;}
static void ready(unsigned role=1){kebab_hid_backend_ready(role,16000,role==2?10:0);}
static void sample(unsigned n,unsigned keys){kebab_hid_frame f{};f.timestamp_ns=n;f.keys=keys;kebab_hid_backend_sample(1,&f);}
static void tick(KebabHIDDevice &d){std::lock_guard<std::recursive_mutex> g(d.loop->mutex);KebabHIDDevice::tick(&d,d.timer);}
int main(){
 IOService provider("KebabHIDTransport");provider.roleProperty.value=1;
 for(const char *f:{"loop","timer","add","original","memory"}){
  reset();ready();failure=f;KebabHIDDevice d;
  assert(!d.start(&provider));assert(!channels[1].queue.owner);
  assert(originalStops==unsigned(failure=="original"||failure=="memory"));
  if(d.timer)assert(!d.timer->added&&!d.timer->armed);
  d.free();clean();
 }
 reset();ready();assert(claim(1,42));{
  KebabHIDDevice d;assert(!d.start(&provider));assert(channels[1].queue.owner==42);d.free();
 }unclaim(1,42);clean();
 reset();{
  KebabHIDDevice d;assert(!d.start(&provider));assert(!originalReports);d.free();clean();
 }
 reset();ready();{
  KebabHIDDevice d;sample(1,0);assert(d.start(&provider));
  IOMemoryDescriptor *m=new IOMemoryDescriptor(2);
  assert(d.getReport(m,kIOHIDReportTypeInput,2)==0&&m->bytes[1]==0);
  assert(d.getReport(m,kIOHIDReportTypeInput,3)==kIOReturnUnsupported);
  assert(d.getReport(m,kIOHIDReportTypeOutput,2)==kIOReturnUnsupported);
  m->length=1;assert(d.getReport(m,kIOHIDReportTypeInput,2)==kIOReturnNoSpace);m->length=2;
  failure="write";assert(d.getReport(m,kIOHIDReportTypeInput,2)==kIOReturnVMError);failure="";
  for(unsigned n=2;n<20;n++)sample(n,n&1);
  tick(d);assert(originalReports==8&&channels[1].sent==8&&d.timer->timeout==1000);
  reportFail=true;tick(d);assert(d.faulted&&originalReports==9&&channels[1].sent==8&&channels[1].result==91);
  tick(d);assert(originalReports==9);
  d.stop(&provider);assert(!channels[1].queue.owner&&!d.timer->added&&!d.timer->armed&&originalStops==1);
  assert(d.getReport(m,kIOHIDReportTypeInput,2)==kIOReturnNoDevice);
  tick(d);assert(originalReports==9);m->release();d.free();clean();
 }
 reset();ready();{
  KebabHIDDevice d;assert(d.start(&provider));sample(1,1);
  blockReport=true;reportEntered=reportLeave=false;
  auto worker=std::async(std::launch::async,[&]{tick(d);});
  {std::unique_lock<std::mutex> lock(reportMutex);reportCondition.wait(lock,[]{return reportEntered;});}
  auto stop=std::async(std::launch::async,[&]{d.stop(&provider);});
  assert(stop.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
  {std::lock_guard<std::mutex> lock(reportMutex);reportLeave=true;reportCondition.notify_all();}
  worker.get();stop.get();assert(!d.timer->armed&&!d.timer->added&&!channels[1].queue.owner);d.free();clean();
 }
 reset();{
  IOService raw("KebabPmicKeys");KebabHIDTransport t;t.roleProperty.value=1;
  assert(t.start(&raw));KebabHIDTransport::tick(&t,t.timer);assert(!publications);
  ready();kebab_hid_backend_error(1,EIO);KebabHIDTransport::tick(&t,t.timer);assert(!publications);
  sample(1,0);KebabHIDTransport::tick(&t,t.timer);assert(publications==1);
  t.stop(&raw);assert(!t.timer->armed&&!t.timer->added);t.free();clean();
 }
 reset();{
  IOService raw("KebabPmicKeys");KebabHIDTransport t;t.roleProperty.value=1;
  assert(t.start(&raw));for(unsigned n=0;n<2400;n++)KebabHIDTransport::tick(&t,t.timer);
  assert(!publications&&!t.published);t.stop(&raw);t.free();clean();
 }
 reset();ready(2);provider.roleProperty.value=2;{
  KebabHIDDevice d;assert(d.start(&provider));auto m=new IOMemoryDescriptor(2);
  assert(d.getReport(m,kIOHIDReportTypeFeature,4)==0&&m->bytes[0]==4&&m->bytes[1]==10);
  m->release();d.stop(&provider);d.free();clean();
 }
 printf("PASS actual broker/driver allocation failures, ownership, unavailable backend, strict reports, failed-dispatch quarantine and gated stop concurrency; IOKit/hardware/parser explicit stand-ins\n");
}
