#include <thread>
#include <vector>
#include <initializer_list>

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cerrno>
using SInt32=int32_t;using IOReturn=int32_t;using UInt32=uint32_t;using task_t=void*;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=-1,kIOReturnBadArgument=-2,kIOReturnNotReady=-3,kIOReturnNotOpen=-4,kIOReturnNoSpace=-5,kIOReturnNoMemory=-6,kIOReturnIOError=-7,kIOReturnNotPrivileged=-8,kIOReturnError=-9,kIOReturnNotPermitted=-10};
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define kIOClientPrivilegeAdministrator "root"
#define IOLog(...) ((void)0)
static bool ready=true,exportFail=false;static unsigned ioFail,live;
static void *IOMalloc(size_t n){return ioFail==1?nullptr:calloc(1,n);}static void IOFree(void *p,size_t){free(p);}
struct IOExternalMethodDispatch{};struct OSDictionary{};struct OSObject{};
struct IOMemoryDescriptor{uint8_t bytes[32768]={0};size_t length=32768;bool shortWrite=false;size_t getLength(){return length;}size_t writeBytes(size_t at,const void *p,size_t n){if(shortWrite)return 0;memcpy(bytes+at,p,n);return n;}};
struct IOExternalMethodArguments{uint32_t asyncWakePort=0,asyncReferenceCount=0;const uint64_t *scalarInput=nullptr;uint32_t scalarInputCount=0;const void *structureInput=nullptr;uint32_t structureInputSize=0;IOMemoryDescriptor *structureInputDescriptor=nullptr;uint64_t *scalarOutput=nullptr;uint32_t scalarOutputCount=0;void *structureOutput=nullptr;uint32_t structureOutputSize=0;IOMemoryDescriptor *structureOutputDescriptor=nullptr;uint32_t structureOutputDescriptorSize=0;OSObject **structureVariableOutputData=nullptr;};
class IOUserClient;
class IOService:public OSObject{IOService *parent=nullptr;public:IOService(){++live;}virtual ~IOService(){--live;}bool init(void*){return ioFail!=2;}bool attach(IOService *p){if(ioFail==3)return false;parent=p;return true;}IOService *getProvider(){return parent;}virtual bool start(IOService*){return ioFail!=4;}void detach(IOService *p){if(parent==p)parent=nullptr;}void release(){delete this;}void setName(const char*){}void setProperty(const char*,bool){}void registerService(){}void terminate(){}static IOService *getResourceService(){static IOService p;return &p;}virtual IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**){return -1;}virtual IOReturn newUserClient(task_t,void*,UInt32,IOUserClient**){return -1;}};
class IOUserClient:public IOService{public:bool initWithTask(task_t,void*,UInt32,OSDictionary*){return ioFail!=2;}static IOReturn clientHasPrivilege(void *t,const char*){return t==(void*)1?0:kIOReturnNotPrivileged;}virtual IOReturn clientClose(){return 0;}virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return -1;}};
static bool kebab_file_keys_available(){return ready;}static bool kebab_device_classbag_available(){return ready;}static bool kebab_volume_class_available(const uint8_t*,uint32_t){return ready;}
static unsigned mappings;static int mapError;
static int kebab_volume_factory_activate(const uint8_t*){++mappings;return mapError;}
static int kebab_device_classbag_cookie(const uint8_t *ctx,void *p,unsigned n){assert(n==34);for(unsigned i=0;i<16;++i)if(ctx[i])return kIOReturnNotPermitted;if(!ready)return kIOReturnNotReady;memset(p,0x5a,n);((uint8_t*)p)[0]=0x4b;((uint8_t*)p)[1]=1;return 0;}
static int kebab_device_classbag_export(void *p,unsigned n){assert(n==4096);if(exportFail)return kIOReturnNotReady;memset(p,0x4a,n);return 0;}

#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
static bool OSCompareAndSwap(uint32_t a,uint32_t b,volatile SInt32 *p){SInt32 expected=static_cast<SInt32>(a);return __atomic_compare_exchange_n(p,&expected,static_cast<SInt32>(b),false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);}
static SInt32 OSAddAtomic(SInt32 a,volatile SInt32 *p){return __atomic_fetch_add(p,a,__ATOMIC_SEQ_CST);}
/* Original iOS16 IOServiceOpen/IOConnect ABI, backed by the live hardware-root
 * factory C/D bag. This is not the upstream empty/offline state stand-in. */

static const uint8_t kuc_data_uuid[16]={0x61,0x70,0x66,0x73,0x75,0x75,0x69,0x64,0,0x40,0x76,0x6f,0x6c,0x75,0x6d,1};
static bool
kuc_ready(void)
{
 return kebab_file_keys_available()&&kebab_device_classbag_available()&&
     kebab_volume_class_available(kuc_data_uuid,3)&&kebab_volume_class_available(kuc_data_uuid,4);
}

/* Included inside the native IOUserClient implementation. Original iOS16
 * aks_params layout: LE32 count, then LE32 length + four-byte padded bytes. */
static bool
kuc_fields(const void *input,unsigned size,unsigned count,const uint8_t **fields,uint32_t *lengths)
{
 if(!input||size<4||count>3)return false;
 const uint8_t *p=static_cast<const uint8_t *>(input);uint32_t actual;
 memcpy(&actual,p,4);if(actual!=count)return false;
 unsigned at=4;
 for(unsigned i=0;i<count;++i){
  if(at>size||size-at<4)return false;
  uint32_t n;memcpy(&n,p+at,4);at+=4;
  if(n>size-at||n>UINT32_MAX-3)return false;
  fields[i]=p+at;lengths[i]=n;
  unsigned padded=(n+3)&~3u;if(padded>size-at)return false;
  for(unsigned j=n;j<padded;++j)if(p[at+j])return false;
  at+=padded;
 }
 return at==size;
}

static IOReturn
kuc_cookie_method(uint32_t selector,IOExternalMethodArguments *a)
{
 const uint8_t *fields[3]={};uint32_t lengths[3]={};
 if(!a->scalarInput||a->scalarInputCount!=(selector==68?1u:2u)||a->scalarInput[0]||
     (selector==65&&a->scalarInput[1])||a->scalarOutputCount||a->structureInputDescriptor||
     !kuc_fields(a->structureInput,a->structureInputSize,selector==68?1:3,fields,lengths))return kIOReturnBadArgument;
 uint8_t cookie[34]={};IOReturn result=kIOReturnBadArgument;
 if(selector==68){
  if(lengths[0]!=16||(a->structureOutputDescriptor&&a->structureOutput))return kIOReturnBadArgument;
  size_t capacity=a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize;
  if(capacity<34||(!a->structureOutputDescriptor&&!a->structureOutput))return kIOReturnNoSpace;
  result=(IOReturn)kebab_device_classbag_cookie(fields[0],cookie,sizeof(cookie));
  if(result==kIOReturnSuccess){
   if(a->structureOutputDescriptor){
    if(a->structureOutputDescriptor->writeBytes(0,cookie,sizeof(cookie))!=sizeof(cookie))result=kIOReturnIOError;
    else a->structureOutputDescriptorSize=sizeof(cookie);
   }else{memcpy(a->structureOutput,cookie,sizeof(cookie));a->structureOutputSize=sizeof(cookie);}
  }
 }else{
  if(lengths[0]!=16||lengths[1]!=34||lengths[2]!=16||a->structureOutputSize||a->structureOutputDescriptor)return kIOReturnBadArgument;
  if(memcmp(fields[0],kuc_data_uuid,16))return kIOReturnNotPermitted;
  result=(IOReturn)kebab_device_classbag_cookie(fields[2],cookie,sizeof(cookie));
  if(result==kIOReturnSuccess){
   unsigned diff=0;for(unsigned i=0;i<sizeof(cookie);++i)diff|=cookie[i]^fields[1][i];
   if(diff)result=kIOReturnNotPermitted;
   else result=(IOReturn)kebab_volume_factory_activate(fields[0]);
   IOLog("KEBAB AKS UC: authenticated system bag -> original Data factory C/D mapping result=%x\n",result);
  }
 }
 bzero(cookie,sizeof(cookie));return result;
}


static IOReturn kuc_notify_method(IOService *,uint32_t,IOExternalMethodArguments *);

class AppleKeyStoreUserClient final : public IOUserClient {
 OSDeclareDefaultStructors(AppleKeyStoreUserClient)
 bool opened=false;
public:
 IOReturn clientClose() override {opened=false;terminate();return kIOReturnSuccess;}
 IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) override;
};
OSDefineMetaClassAndStructors(AppleKeyStoreUserClient,IOUserClient)

IOReturn
AppleKeyStoreUserClient::externalMethod(uint32_t selector,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *)
{
 IOReturn result=kIOReturnUnsupported;
 if(!a)return kIOReturnBadArgument;
 const unsigned in=a->scalarInputCount,out=a->scalarOutputCount,si=a->structureInputSize;
 if(a->asyncWakePort||a->asyncReferenceCount||a->structureVariableOutputData)result=kIOReturnBadArgument;
 else if(!kuc_ready())result=kIOReturnNotReady;
 else if(selector==0){
  if(in||out||si||a->structureInputDescriptor||a->structureOutputSize||a->structureOutputDescriptor)result=kIOReturnBadArgument;
  else{opened=true;result=kIOReturnSuccess;}
 }else if(!opened)result=kIOReturnNotOpen;
 else if(selector==102||selector==103){
  result=kuc_notify_method(getProvider(),selector,a);
 }else if(selector==65||selector==68){
  result=kuc_cookie_method(selector,a);
 }else if(selector==14){
  if(in||out!=1||!a->scalarOutput||si||a->structureInputDescriptor||a->structureOutputSize||a->structureOutputDescriptor)result=kIOReturnBadArgument;
  else{a->scalarOutput[0]=0;a->scalarOutputCount=1;result=kIOReturnSuccess;}
 }else if(selector==7){
  if(in!=1||!a->scalarInput||a->scalarInput[0]||out!=1||!a->scalarOutput||si||a->structureInputDescriptor||a->structureOutputSize||a->structureOutputDescriptor)result=kIOReturnBadArgument;
  else{a->scalarOutput[0]=4;a->scalarOutputCount=1;result=kIOReturnSuccess;}
 }else if(selector==3){
  if(in!=1||!a->scalarInput||a->scalarInput[0]||out||si||a->structureInputDescriptor||
      (a->structureOutputDescriptor&&a->structureOutput))result=kIOReturnBadArgument;
  else{
   size_t capacity=a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize;
   if(capacity<4096||(!a->structureOutputDescriptor&&!a->structureOutput))result=kIOReturnNoSpace;
   else{
    uint8_t *record=static_cast<uint8_t *>(IOMalloc(4096));
    if(!record)result=kIOReturnNoMemory;
    else{
     result=(IOReturn)kebab_device_classbag_export(record,4096);
     if(result==kIOReturnSuccess){
      if(a->structureOutputDescriptor){
       if(a->structureOutputDescriptor->writeBytes(0,record,4096)!=4096)result=kIOReturnIOError;
       else a->structureOutputDescriptorSize=4096;
      }else{memcpy(a->structureOutput,record,4096);a->structureOutputSize=4096;}
     }
     bzero(record,4096);IOFree(record,4096);
    }
   }
  }
 }
 IOLog("KEBAB AKS UC: selector=%u scalar=%u/%u struct-in=%u result=%x; hardware factory C/D only; struct-out=%llu desc=%u/%u\n",selector,in,out,si,result,(unsigned long long)a->structureOutputSize,a->structureInputDescriptor!=nullptr,a->structureOutputDescriptor!=nullptr);
 return result;
}

class AppleKeyStore final : public IOService {
 OSDeclareDefaultStructors(AppleKeyStore)
 /* Original selectors102/103 store one provider-owned notification flag.
  * This records registration, not class-key availability or unlock success. */
 volatile SInt32 notificationState=0;
 IOReturn makeClient(task_t task,void *security,UInt32 type,OSDictionary *properties,IOUserClient **out){
  if(out)*out=nullptr;
  if(!out||!task||type)return kIOReturnBadArgument;
  if(IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
  if(!kuc_ready())return kIOReturnNotReady;
  AppleKeyStoreUserClient *client=new AppleKeyStoreUserClient;
  if(!client)return kIOReturnNoMemory;
  if(!client->initWithTask(task,security,type,properties)){client->release();return kIOReturnError;}
  if(!client->attach(this)){client->release();return kIOReturnError;}
  if(!client->start(this)){client->detach(this);client->release();return kIOReturnError;}
  IOLog("KEBAB AKS UC: root client opened; original ABI, authenticated durable hardware bag available\n");
  *out=client;return kIOReturnSuccess;
 }
public:
 void markNotificationsRegistered(){(void)OSCompareAndSwap(0,1,&notificationState);}
 bool notificationsRegistered(){return OSAddAtomic(0,&notificationState)!=0;}
 IOReturn newUserClient(task_t t,void *s,UInt32 n,OSDictionary *p,IOUserClient **out) override{return makeClient(t,s,n,p,out);}
 IOReturn newUserClient(task_t t,void *s,UInt32 n,IOUserClient **out) override{return makeClient(t,s,n,nullptr,out);}
};
OSDefineMetaClassAndStructors(AppleKeyStore,IOService)

static IOReturn
kuc_notify_method(IOService *service,uint32_t selector,IOExternalMethodArguments *a)
{
 if(a->scalarInputCount||a->structureInputSize||a->structureInputDescriptor||
     a->structureOutputSize||a->structureOutputDescriptor)return kIOReturnBadArgument;
 if(selector==102){
  if(a->scalarOutputCount)return kIOReturnBadArgument;
 }else if(selector==103){
  if(a->scalarOutputCount!=1||!a->scalarOutput)return kIOReturnBadArgument;
 }else return kIOReturnUnsupported;
 AppleKeyStore *provider=OSDynamicCast(AppleKeyStore,service);
 if(!provider)return kIOReturnNotReady;
 if(selector==102)provider->markNotificationsRegistered();
 bool state=provider->notificationsRegistered();
 if(selector==103){a->scalarOutput[0]=state?1:0;a->scalarOutputCount=1;}
 IOLog("KEBAB AKS NOTIFY: selector=%u provider-state=%u; registration flag only\n",selector,state?1:0);
 return kIOReturnSuccess;
}
static AppleKeyStore *kuc_provider;
extern "C" int kebab_aks_userclient_start(void);
extern "C" int
kebab_aks_userclient_start(void)
{
 if(!kuc_ready())return EPERM;
 if(kuc_provider)return 0;
 IOService *resources=IOService::getResourceService();if(!resources)return ENXIO;
 AppleKeyStore *provider=new AppleKeyStore;if(!provider)return ENOMEM;
 if(!provider->init(nullptr)){provider->release();return ENOMEM;}
 if(!provider->attach(resources)){provider->release();return EIO;}
 if(!provider->start(resources)){provider->detach(resources);provider->release();return EIO;}
 provider->setName("AppleKeyStore");provider->setProperty("HiOSHardwareRootFactoryCD",true);
 provider->registerService();kuc_provider=provider;
 IOLog("KEBAB AKS UC: published /IOResources/AppleKeyStore; native bank handle0, no SEP/passcode claim\n");
 return 0;
}
int main(){unsigned cases=0;AppleKeyStoreUserClient client;IOExternalMethodArguments a;uint64_t scalar=0xdeadbeef,input=0;uint8_t output[4096];
 auto call=[&](unsigned sel,IOExternalMethodArguments &v){++cases;return client.externalMethod(sel,&v,nullptr,nullptr,nullptr);};
 a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(14,a)==kIOReturnNotOpen&&scalar==0xdeadbeef);
 a={};assert(call(0,a)==0);a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(14,a)==0&&scalar==0);
 for(unsigned selector: {7u,3u}){a={};a.scalarInput=&input;a.scalarInputCount=1;
  if(selector==7){a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(selector,a)==0&&scalar==4);}
  else{a.structureOutput=output;a.structureOutputSize=4096;assert(call(selector,a)==0&&a.structureOutputSize==4096);for(uint8_t x:output)assert(x==0x4a);}
  input=1;memset(output,0xa5,4096);scalar=0xdeadbeef;assert(call(selector,a)==kIOReturnBadArgument);input=0;
  ready=false;assert(call(selector,a)==kIOReturnNotReady&&scalar==0xdeadbeef);for(uint8_t x:output)assert(x==0xa5);ready=true;
 }
 for(unsigned sel=0;sel<128;++sel)if(sel!=0&&sel!=3&&sel!=7&&sel!=14&&sel!=65&&sel!=68&&sel!=102&&sel!=103){a={};assert(call(sel,a)==kIOReturnUnsupported);}
 a={};a.scalarInput=&input;a.scalarInputCount=1;a.structureOutput=output;a.structureOutputSize=4095;memset(output,0xa5,4096);assert(call(3,a)==kIOReturnNoSpace);for(uint8_t x:output)assert(x==0xa5);
 a.structureOutputSize=4096;exportFail=true;assert(call(3,a)==kIOReturnNotReady);for(uint8_t x:output)assert(x==0xa5);exportFail=false;
 ioFail=1;assert(call(3,a)==kIOReturnNoMemory);ioFail=0;
 IOMemoryDescriptor descriptor;a.structureOutput=nullptr;a.structureOutputDescriptor=&descriptor;assert(call(3,a)==0&&a.structureOutputDescriptorSize==4096);descriptor.shortWrite=true;assert(call(3,a)==kIOReturnIOError);
 a={};a.asyncReferenceCount=1;assert(call(0,a)==kIOReturnBadArgument);a={};a.structureInputSize=1;assert(call(0,a)==kIOReturnBadArgument);
 a={};a.scalarOutputCount=2;a.scalarOutput=&scalar;assert(call(14,a)==kIOReturnBadArgument);
 uint8_t get[24]={1,0,0,0,16,0,0,0};uint8_t token[34];
 a={};a.scalarInput=&input;a.scalarInputCount=1;a.structureInput=get;a.structureInputSize=24;a.structureOutput=token;a.structureOutputSize=34;assert(call(68,a)==0&&a.structureOutputSize==34&&token[0]==0x4b);
 for(unsigned n=0;n<24;++n){a.structureInputSize=n;assert(call(68,a)!=0);}a.structureInputSize=24;
 for(unsigned i=0;i<16;++i){get[8+i]=1;assert(call(68,a)==kIOReturnNotPermitted);get[8+i]=0;}
 a.structureOutputSize=33;assert(call(68,a)==kIOReturnNoSpace);
 uint8_t map[84]={3,0,0,0,16,0,0,0};memcpy(map+8,kuc_data_uuid,16);map[24]=34;memcpy(map+28,token,34);map[64]=16;uint64_t scalars[2]={0,0};
 a={};a.scalarInput=scalars;a.scalarInputCount=2;a.structureInput=map;a.structureInputSize=84;assert(call(65,a)==0&&mappings==1);
 for(unsigned n=0;n<84;++n){a.structureInputSize=n;assert(call(65,a)!=0&&mappings==1);}a.structureInputSize=84;
 for(unsigned i=0;i<34;++i){map[28+i]^=1;assert(call(65,a)==kIOReturnNotPermitted&&mappings==1);map[28+i]^=1;}
 for(unsigned i=0;i<16;++i){map[8+i]^=1;assert(call(65,a)==kIOReturnNotPermitted&&mappings==1);map[8+i]^=1;}
 map[62]=1;assert(call(65,a)==kIOReturnBadArgument);map[62]=0;
 map[68]=1;assert(call(65,a)==kIOReturnNotPermitted);map[68]=0;
 scalars[1]=1;assert(call(65,a)==kIOReturnBadArgument);scalars[1]=0;
 mapError=kIOReturnNotReady;assert(call(65,a)==kIOReturnNotReady&&mappings==2);mapError=0;
 client.clientClose();a={};a.scalarOutputCount=1;a.scalarOutput=&scalar;assert(call(14,a)==kIOReturnNotOpen);
 AppleKeyStore provider;IOUserClient *out=nullptr;
 assert(provider.newUserClient((void*)2,nullptr,0,&out)==kIOReturnNotPrivileged&&!out);++cases;
 assert(provider.newUserClient((void*)1,nullptr,1,&out)==kIOReturnBadArgument&&!out);++cases;
 for(unsigned fail=2;fail<=4;++fail){ioFail=fail;unsigned old=live;assert(provider.newUserClient((void*)1,nullptr,0,&out)!=0&&!out&&live==old);++cases;}ioFail=0;
 ready=false;assert(provider.newUserClient((void*)1,nullptr,0,&out)==kIOReturnNotReady&&!out);++cases;ready=true;
 assert(provider.newUserClient((void*)1,nullptr,0,&out)==0&&out);out->release();++cases;

 AppleKeyStore notifyProvider,otherProvider;AppleKeyStoreUserClient c1,c2,c3;
 assert(c1.attach(&notifyProvider)&&c2.attach(&notifyProvider)&&c3.attach(&otherProvider));
 IOExternalMethodArguments n;
 assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==kIOReturnNotOpen);++cases;
 for(auto *c:{&c1,&c2,&c3})assert(c->externalMethod(0,&n,nullptr,nullptr,nullptr)==0);
 auto query=[&](AppleKeyStoreUserClient &c,uint64_t expected){uint64_t value=0xdeadbeef;IOExternalMethodArguments q;q.scalarOutput=&value;q.scalarOutputCount=1;assert(c.externalMethod(103,&q,nullptr,nullptr,nullptr)==0&&value==expected&&q.scalarOutputCount==1);++cases;};
 query(c1,0);query(c2,0);query(c3,0);
 for(unsigned selector:{102u,103u})for(unsigned bad=0;bad<10;++bad){
  IOExternalMethodArguments q;uint64_t value=0xdeadbeef;IOMemoryDescriptor descriptor;
  if(selector==103){q.scalarOutput=&value;q.scalarOutputCount=1;}
  switch(bad){case 0:q.scalarInputCount=1;break;case 1:q.structureInputSize=1;break;case 2:q.structureInputDescriptor=&descriptor;break;case 3:q.structureOutputSize=1;break;case 4:q.structureOutputDescriptor=&descriptor;break;case 5:q.scalarOutputCount=2;break;case 6:q.asyncWakePort=1;break;case 7:q.asyncReferenceCount=1;break;case 8:q.structureVariableOutputData=(OSObject**)&scalar;break;case 9:if(selector==103)q.scalarOutput=nullptr;else q.scalarOutputCount=1;break;}
  assert(c1.externalMethod(selector,&q,nullptr,nullptr,nullptr)==kIOReturnBadArgument&&value==0xdeadbeef);++cases;
  query(c2,0);
 }
 ready=false;assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==kIOReturnNotReady);++cases;ready=true;query(c2,0);
 AppleKeyStoreUserClient unattached;assert(unattached.externalMethod(0,&n,nullptr,nullptr,nullptr)==0);assert(unattached.externalMethod(102,&n,nullptr,nullptr,nullptr)==kIOReturnNotReady);++cases;
 assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==0);++cases;query(c1,1);query(c2,1);query(c3,0);
 assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==0);++cases;query(c2,1);
 c1.clientClose();query(c2,1);
 // Distinct clients share provider-owned state; thousands of simultaneous reads/sets.
 std::vector<std::thread> threads;
 for(unsigned t=0;t<4;++t)threads.emplace_back([&]{for(unsigned k=0;k<2000;++k){uint64_t value=99;IOExternalMethodArguments q;q.scalarOutput=&value;q.scalarOutputCount=1;assert(c2.externalMethod(103,&q,nullptr,nullptr,nullptr)==0&&value==1);IOExternalMethodArguments set;assert(c2.externalMethod(102,&set,nullptr,nullptr,nullptr)==0);}});
 for(auto &t:threads)t.join();cases+=16000;query(c2,1);query(c3,0);
 ready=false;uint64_t untouched=99;IOExternalMethodArguments q;q.scalarOutput=&untouched;q.scalarOutputCount=1;assert(c2.externalMethod(103,&q,nullptr,nullptr,nullptr)==kIOReturnNotReady&&untouched==99);++cases;ready=true;
 AppleKeyStore freshProvider;assert(!freshProvider.notificationsRegistered());++cases;
 printf("PASS %u actual selector/access/marshal/refusal cases; mocked IOKit and sealed-bank export, hardware not claimed\n",cases);
}
