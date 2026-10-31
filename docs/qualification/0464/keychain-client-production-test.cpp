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
#include <cstdarg>
#include <string>
static std::vector<std::string> wrapMeta;
static int proc_selfpid(){return 63;}
static void hostLog(const char*format,...){
 if(strncmp(format,"KEBAB AKS WRAP-META:",20))return;
 char line[512];va_list a;va_start(a,format);int n=vsnprintf(line,sizeof(line),format,a);va_end(a);assert(n>0&&n<int(sizeof(line)));wrapMeta.emplace_back(line);
}
#define IOLog hostLog
static bool ready=true,exportFail=false;static unsigned ioFail,live;
static void *IOMalloc(size_t n){return ioFail==1?nullptr:calloc(1,n);}static void IOFree(void *p,size_t){free(p);}
static unsigned entitlementRefs;
struct IOExternalMethodDispatch{};struct OSDictionary{};struct OSObject{void release(){assert(entitlementRefs);entitlementRefs--;}};
static OSObject trueObject,falseObject,otherObject;static OSObject *kOSBooleanTrue=&trueObject;
static bool entitledRoot=false,entitledThree=true;
static OSObject *modelEntitlement(task_t t,const char*name){assert(!strcmp(name,"com.apple.keystore.access-keychain-keys"));OSObject*p=nullptr;if(t==(void*)1)p=entitledRoot?&trueObject:nullptr;if(t==(void*)3)p=entitledThree?&trueObject:nullptr;if(t==(void*)4)p=&falseObject;if(t==(void*)5)p=&otherObject;if(p)entitlementRefs++;return p;}

struct IOMemoryDescriptor{uint8_t bytes[32768]={0};size_t length=32768;bool shortWrite=false;size_t getLength(){return length;}size_t writeBytes(size_t at,const void *p,size_t n){if(shortWrite)return 0;memcpy(bytes+at,p,n);return n;}};
struct IOExternalMethodArguments{uint32_t asyncWakePort=0,asyncReferenceCount=0;const uint64_t *scalarInput=nullptr;uint32_t scalarInputCount=0;const void *structureInput=nullptr;uint32_t structureInputSize=0;IOMemoryDescriptor *structureInputDescriptor=nullptr;uint64_t *scalarOutput=nullptr;uint32_t scalarOutputCount=0;void *structureOutput=nullptr;uint32_t structureOutputSize=0;IOMemoryDescriptor *structureOutputDescriptor=nullptr;uint32_t structureOutputDescriptorSize=0;OSObject **structureVariableOutputData=nullptr;};
class IOUserClient;
class IOService:public OSObject{IOService *parent=nullptr;public:IOService(){++live;}virtual ~IOService(){--live;}bool init(void*){return ioFail!=2;}bool attach(IOService *p){if(ioFail==3)return false;parent=p;return true;}IOService *getProvider(){return parent;}virtual bool start(IOService*){return ioFail!=4;}void detach(IOService *p){if(parent==p)parent=nullptr;}void release(){delete this;}void setName(const char*){}void setProperty(const char*,bool){}void registerService(){}void terminate(){}static IOService *getResourceService(){static IOService p;return &p;}virtual IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**){return -1;}virtual IOReturn newUserClient(task_t,void*,UInt32,IOUserClient**){return -1;}};
class IOUserClient:public IOService{public:static OSObject *copyClientEntitlement(task_t t,const char*n){return modelEntitlement(t,n);}bool initWithTask(task_t,void*,UInt32,OSDictionary*){return ioFail!=2;}static IOReturn clientHasPrivilege(void *t,const char*){return t==(void*)1?0:kIOReturnNotPrivileged;}virtual IOReturn clientClose(){return 0;}virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return -1;}};
static bool kebab_file_keys_available(){return ready;}static bool kebab_device_classbag_available(){return ready;}static bool kebab_volume_class_available(const uint8_t*,uint32_t){return ready;}
static unsigned mappings;static int mapError;
static int kebab_volume_factory_activate(const uint8_t*){++mappings;return mapError;}
static int kebab_device_classbag_cookie(const uint8_t *ctx,void *p,unsigned n){assert(n==34);for(unsigned i=0;i<16;++i)if(ctx[i])return kIOReturnNotPermitted;if(!ready)return kIOReturnNotReady;memset(p,0x5a,n);((uint8_t*)p)[0]=0x4b;((uint8_t*)p)[1]=1;return 0;}
static int kebab_device_classbag_export(void *p,unsigned n){assert(n==4096);if(exportFail)return kIOReturnNotReady;memset(p,0x4a,n);return 0;}

#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
static bool OSCompareAndSwap(uint32_t a,uint32_t b,volatile SInt32 *p){SInt32 expected=static_cast<SInt32>(a);return __atomic_compare_exchange_n(p,&expected,static_cast<SInt32>(b),false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);}
static SInt32 OSAddAtomic(SInt32 a,volatile SInt32 *p){return __atomic_fetch_add(p,a,__ATOMIC_SEQ_CST);}
/* Backend metadata only. This describes the authenticated factory C/D bank,
 * not Apple SEP, passcode support or availability of other key classes. */
#define KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE 1u
#define KEBAB_DEVICE_BAG_CLASS_C_D_MASK ((1u << 3) | (1u << 4))
struct kebab_device_classbag_status {
 uint32_t version, policy, available_classes;
};

static kebab_device_classbag_status backendStatus={1,1,KEBAB_DEVICE_BAG_CLASS_C_D_MASK};
static int statusError;static unsigned statusCalls;
static int kebab_device_classbag_get_status(kebab_device_classbag_status *out){statusCalls++;if(statusError)return statusError;*out=backendStatus;return 0;}

static unsigned keyCalls;static int keyFailure;
static int kebab_keychain_key_crypt(int32_t bag,uint32_t cls,const void*input,unsigned in,void*out,unsigned bytes,bool encrypt){
 keyCalls++;assert(!bag&&input&&out&&in==(encrypt?32u:40u)&&bytes==(encrypt?40u:32u));
 if(keyFailure)return keyFailure;
 if(cls!=7&&cls!=8&&cls!=10&&cls!=11)return kIOReturnUnsupported;
 const uint8_t*p=(const uint8_t*)input;uint8_t*q=(uint8_t*)out;
 if(encrypt){memset(q,0x6b,8);for(unsigned i=0;i<32;i++)q[i+8]=p[i]^0xa5;}
 else{for(unsigned i=0;i<8;i++)if(p[i]!=0x6b)return kIOReturnNotPermitted;for(unsigned i=0;i<32;i++)q[i]=p[i+8]^0xa5;}
 return 0;
}
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



/* Original aks_get_device_state returns a DER SET of UTF8-key/INTEGER
 * sequences. Only report the two fields established by our real bank:
 * ss bit2 (provisioned C/D keys available since boot), sls3 (factory no PIN).
 * Do not claim sls0/all-class unlock or invent SEP/backoff/generation fields.
 * The original target decoder handles absent optional fields itself. */
static IOReturn
kuc_device_state_method(IOExternalMethodArguments *a)
{
 if(a->scalarInputCount!=1||!a->scalarInput||a->scalarInput[0]||
     a->scalarOutputCount||a->structureInputSize||a->structureInputDescriptor||
     (a->structureOutputDescriptor&&a->structureOutput))return kIOReturnBadArgument;
 const size_t bytes=21;
 size_t capacity=a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize;
 if(capacity<bytes||(!a->structureOutputDescriptor&&!a->structureOutput))return kIOReturnNoSpace;
 struct kebab_device_classbag_status status={};
 IOReturn result=(IOReturn)kebab_device_classbag_get_status(&status);
 if(result!=kIOReturnSuccess)return result;
 if(status.version!=1||status.policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE||
     status.available_classes!=KEBAB_DEVICE_BAG_CLASS_C_D_MASK)return kIOReturnNotReady;
 /* Canonical DER SET ordering: the shorter ss entry precedes sls. */
 const uint8_t state[bytes]={0x31,0x13,
     0x30,0x07,0x0c,0x02,'s','s',0x02,0x01,0x04,
     0x30,0x08,0x0c,0x03,'s','l','s',0x02,0x01,0x03};
 if(a->structureOutputDescriptor){
  if(a->structureOutputDescriptor->writeBytes(0,state,bytes)!=bytes)return kIOReturnIOError;
  a->structureOutputDescriptorSize=(uint32_t)bytes;
 }else{memcpy(a->structureOutput,state,bytes);a->structureOutputSize=bytes;}
 IOLog("KEBAB AKS STATE: authenticated factory policy=%u available-classes=0x%x sls=3 ss=4; C/D only, no A/B/passcode/SEP\n",
     status.policy,status.available_classes);
 return kIOReturnSuccess;
}

/* Original 20H392 aks_wrap_key/aks_unwrap_key packet, AES256 key profile.
 * Authorization is assigned by the provider from AMFI entitlements, not here.
 * Wrapped bytes are backend-owned RFC3394, opaque to original userland.
 * Other key lengths/personas/bags/classes are not silently transformed. */
static IOReturn
kuc_keychain_method(uint32_t selector,IOExternalMethodArguments *a)
{
 const bool encrypt=selector==10;
 if((selector!=10&&selector!=11)||a->scalarInputCount!=2||!a->scalarInput||
     a->scalarInput[0]||a->scalarInput[1]>UINT32_MAX||
     a->scalarOutputCount!=(encrypt?1u:0u)||(encrypt&&!a->scalarOutput)||
     a->structureInputDescriptor||!a->structureInput||
     a->structureInputSize!=(encrypt?32u:40u)||
     (a->structureOutputDescriptor&&a->structureOutput))return kIOReturnBadArgument;
 const unsigned bytes=encrypt?40u:32u;
 const uint32_t cls=(uint32_t)a->scalarInput[1];
 size_t capacity=a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize;
 if(capacity<bytes||(!a->structureOutputDescriptor&&!a->structureOutput))return kIOReturnNoSpace;
 uint8_t output[40]={};
 IOReturn result=(IOReturn)kebab_keychain_key_crypt(0,cls,
     a->structureInput,a->structureInputSize,output,bytes,encrypt);
 if(result==kIOReturnSuccess){
  if(a->structureOutputDescriptor){
   if(a->structureOutputDescriptor->writeBytes(0,output,bytes)!=bytes)result=kIOReturnIOError;
   else a->structureOutputDescriptorSize=bytes;
  }else{memcpy(a->structureOutput,output,bytes);a->structureOutputSize=bytes;}
  if(result==kIOReturnSuccess&&encrypt){a->scalarOutput[0]=cls;a->scalarOutputCount=1;}
 }
 bzero(output,sizeof(output));
 IOLog("KEBAB AKS KEYCHAIN: selector=%u class=%u bytes=%u/%u result=%x; key bytes never logged\n",
     selector,cls,a->structureInputSize,bytes,result);
 return result;
}


static IOReturn kuc_notify_method(IOService *,uint32_t,IOExternalMethodArguments *);

class AppleKeyStoreUserClient final : public IOUserClient {
 OSDeclareDefaultStructors(AppleKeyStoreUserClient)
 friend class AppleKeyStore;
 /* Set once by the provider after initWithTask. Unprivileged connections
  * expose public status only; no selector can promote this access role. */
 bool administrative=false;
 bool keychainAccess=false;
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
 else if((selector==10||selector==11)&&!keychainAccess)result=kIOReturnNotPrivileged;
 else if(!administrative&&selector!=7&&selector!=14&&selector!=17&&selector!=10&&selector!=11)result=kIOReturnNotPrivileged;
 else if(selector==10||selector==11){
  result=kuc_keychain_method(selector,a);
 }
 else if(selector==102||selector==103){
  result=kuc_notify_method(getProvider(),selector,a);
 }else if(selector==65||selector==68){
  result=kuc_cookie_method(selector,a);
 }else if(selector==17){
  result=kuc_device_state_method(a);
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
 /* Fixed metadata only: key bytes are never logged. */
 static uint32_t wrap_reports;
 if((selector==10||selector==11)&&in==2&&a->scalarInput&&
     __atomic_load_n(&wrap_reports,__ATOMIC_RELAXED)<16){
  uint32_t report=__atomic_fetch_add(&wrap_reports,1u,__ATOMIC_RELAXED);
  if(report<16)IOLog("KEBAB AKS WRAP-META: selector=%u pid=%d bag64=0x%llx class64=0x%llx administrative=%u result=%x; key bytes never logged\n",
      selector,proc_selfpid(),(unsigned long long)a->scalarInput[0],(unsigned long long)a->scalarInput[1],administrative?1u:0u,result);
 }
 IOLog("KEBAB AKS UC: selector=%u scalar=%u/%u struct-in=%u result=%x; hardware factory C/D only; struct-out=%llu desc=%u/%u\n",selector,in,out,si,result,(unsigned long long)a->structureOutputSize,a->structureInputDescriptor!=nullptr,a->structureOutputDescriptor!=nullptr);
 IOLog("KEBAB AKS ACCESS: selector=%u role=%s result=%x\n",selector,administrative?"administrative":keychainAccess?"keychain":"read-only-status",result);
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
  /* MKBGetDeviceLockState is used by the mobile DataMigration service.
   * A connection is not authorization for key/volume/notification operations;
   * those remain gated per selector by the immutable role below. */
  bool administrative=IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator)==kIOReturnSuccess;
  if(!kuc_ready())return kIOReturnNotReady;
  AppleKeyStoreUserClient *client=new AppleKeyStoreUserClient;
  if(!client)return kIOReturnNoMemory;
  if(!client->initWithTask(task,security,type,properties)){client->release();return kIOReturnError;}
  client->administrative=administrative;
  {
   /* Strict AMFI OSBoolean entitlement; root status does not grant this role.
    * Capability is immutable and enables only selectors10/11, not bag admin.
    * This translation unit uses the retained raw OSPtr compatibility ABI. */
   OSObject *entitlement=IOUserClient::copyClientEntitlement(task,"com.apple.keystore.access-keychain-keys");
   client->keychainAccess=(entitlement==kOSBooleanTrue);
   if(entitlement)entitlement->release();
  }
  if(!client->attach(this)){client->release();return kIOReturnError;}
  if(!client->start(this)){client->detach(this);client->release();return kIOReturnError;}
  IOLog("KEBAB AKS UC: %s client opened; original ABI, authenticated durable hardware bag available\n",administrative?"administrative":"read-only status");
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
}int main(){unsigned cases=0;
 {AppleKeyStore metadataProvider;IOUserClient *base=nullptr;assert(metadataProvider.newUserClient((void*)1,nullptr,0,&base)==0);
 auto *client=static_cast<AppleKeyStoreUserClient*>(base);IOExternalMethodArguments open;assert(client->externalMethod(0,&open,nullptr,nullptr,nullptr)==0);
 uint64_t scalars[2]={0,6},returned=0x1234;unsigned char raw[32],sealed[64];memset(raw,0x6c,32);memset(sealed,0xa5,64);
 for(unsigned j=0;j<100;j++){IOExternalMethodArguments a;a.scalarInput=scalars;a.scalarInputCount=2;a.scalarOutput=&returned;a.scalarOutputCount=1;a.structureInput=raw;a.structureInputSize=32;a.structureOutput=sealed;a.structureOutputSize=40;
 assert(client->externalMethod(10,&a,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged&&returned==0x1234&&a.structureOutputSize==40);
 for(auto b:raw)assert(b==0x6c);for(auto b:sealed)assert(b==0xa5);}
 assert(wrapMeta.size()==16);for(const auto&line:wrapMeta)assert(line.find("pid=63 bag64=0x0 class64=0x6 administrative=1")!=std::string::npos);
 base->release();}
AppleKeyStore rootProvider;IOUserClient *rootBase=nullptr;assert(rootProvider.newUserClient((void*)1,nullptr,0,&rootBase)==0);auto &client=*static_cast<AppleKeyStoreUserClient *>(rootBase);IOExternalMethodArguments a;uint64_t scalar=0xdeadbeef,input=0;uint8_t output[4096];
 auto call=[&](unsigned sel,IOExternalMethodArguments &v){++cases;return client.externalMethod(sel,&v,nullptr,nullptr,nullptr);};
 a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(14,a)==kIOReturnNotOpen&&scalar==0xdeadbeef);
 a={};assert(call(0,a)==0);a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(14,a)==0&&scalar==0);
 for(unsigned selector: {7u,3u}){a={};a.scalarInput=&input;a.scalarInputCount=1;
  if(selector==7){a.scalarOutput=&scalar;a.scalarOutputCount=1;assert(call(selector,a)==0&&scalar==4);}
  else{a.structureOutput=output;a.structureOutputSize=4096;assert(call(selector,a)==0&&a.structureOutputSize==4096);for(uint8_t x:output)assert(x==0x4a);}
  input=1;memset(output,0xa5,4096);scalar=0xdeadbeef;assert(call(selector,a)==kIOReturnBadArgument);input=0;
  ready=false;assert(call(selector,a)==kIOReturnNotReady&&scalar==0xdeadbeef);for(uint8_t x:output)assert(x==0xa5);ready=true;
 }
 for(unsigned sel=0;sel<128;++sel)if(sel!=0&&sel!=3&&sel!=7&&sel!=14&&sel!=65&&sel!=68&&sel!=102&&sel!=103&&sel!=17){a={};assert(call(sel,a)==((sel==10||sel==11)?kIOReturnNotPrivileged:kIOReturnUnsupported));}
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
 assert(provider.newUserClient((void*)2,nullptr,0,&out)==0&&out);out->release();out=nullptr;++cases;
 assert(provider.newUserClient((void*)1,nullptr,1,&out)==kIOReturnBadArgument&&!out);++cases;
 for(unsigned fail=2;fail<=4;++fail){ioFail=fail;unsigned old=live;assert(provider.newUserClient((void*)1,nullptr,0,&out)!=0&&!out&&live==old);++cases;}ioFail=0;
 ready=false;assert(provider.newUserClient((void*)1,nullptr,0,&out)==kIOReturnNotReady&&!out);++cases;ready=true;
 assert(provider.newUserClient((void*)1,nullptr,0,&out)==0&&out);out->release();++cases;

 AppleKeyStore notifyProvider,otherProvider;IOUserClient *nc1=nullptr,*nc2=nullptr,*nc3=nullptr;assert(notifyProvider.newUserClient((void*)1,nullptr,0,&nc1)==0&&notifyProvider.newUserClient((void*)1,nullptr,0,&nc2)==0&&otherProvider.newUserClient((void*)1,nullptr,0,&nc3)==0);auto &c1=*static_cast<AppleKeyStoreUserClient *>(nc1);auto &c2=*static_cast<AppleKeyStoreUserClient *>(nc2);auto &c3=*static_cast<AppleKeyStoreUserClient *>(nc3);
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
 AppleKeyStoreUserClient unattached;assert(unattached.externalMethod(0,&n,nullptr,nullptr,nullptr)==0);assert(unattached.externalMethod(102,&n,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged);++cases;
 assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==0);++cases;query(c1,1);query(c2,1);query(c3,0);
 assert(c1.externalMethod(102,&n,nullptr,nullptr,nullptr)==0);++cases;query(c2,1);
 c1.clientClose();query(c2,1);
 // Distinct clients share provider-owned state; thousands of simultaneous reads/sets.
 std::vector<std::thread> threads;
 for(unsigned t=0;t<4;++t)threads.emplace_back([&]{for(unsigned k=0;k<2000;++k){uint64_t value=99;IOExternalMethodArguments q;q.scalarOutput=&value;q.scalarOutputCount=1;assert(c2.externalMethod(103,&q,nullptr,nullptr,nullptr)==0&&value==1);IOExternalMethodArguments set;assert(c2.externalMethod(102,&set,nullptr,nullptr,nullptr)==0);}});
 for(auto &t:threads)t.join();cases+=16000;query(c2,1);query(c3,0);
 ready=false;uint64_t untouched=99;IOExternalMethodArguments q;q.scalarOutput=&untouched;q.scalarOutputCount=1;assert(c2.externalMethod(103,&q,nullptr,nullptr,nullptr)==kIOReturnNotReady&&untouched==99);++cases;ready=true;
 AppleKeyStore freshProvider;assert(!freshProvider.notificationsRegistered());++cases;

 AppleKeyStoreUserClient stateClient;uint64_t stateHandle=0;uint8_t stateBytes[64];
 auto stateCall=[&](IOExternalMethodArguments &q){++cases;return stateClient.externalMethod(17,&q,nullptr,nullptr,nullptr);};
 auto stateArgs=[&](){IOExternalMethodArguments q;q.scalarInput=&stateHandle;q.scalarInputCount=1;q.structureOutput=stateBytes;q.structureOutputSize=64;return q;};
 auto unchangedState=[&](){for(auto x:stateBytes)assert(x==0xa5);};
 const uint8_t stateOracle[]={0x31,0x13,0x30,0x07,0x0c,0x02,'s','s',0x02,0x01,0x04,0x30,0x08,0x0c,0x03,'s','l','s',0x02,0x01,0x03};
 auto state=stateArgs();memset(stateBytes,0xa5,64);assert(stateCall(state)==kIOReturnNotOpen);unchangedState();
 IOExternalMethodArguments openState;assert(stateClient.externalMethod(0,&openState,nullptr,nullptr,nullptr)==0);
 assert(stateCall(state)==0&&state.structureOutputSize==sizeof(stateOracle)&&!memcmp(stateBytes,stateOracle,sizeof(stateOracle)));
 for(unsigned k=sizeof(stateOracle);k<64;++k)assert(stateBytes[k]==0xa5);
 for(unsigned capacity=0;capacity<sizeof(stateOracle);++capacity){state=stateArgs();state.structureOutputSize=capacity;memset(stateBytes,0xa5,64);unsigned prior=statusCalls;assert(stateCall(state)==kIOReturnNoSpace&&state.structureOutputSize==capacity&&statusCalls==prior);unchangedState();}
 for(unsigned bad=0;bad<11;++bad){state=stateArgs();IOMemoryDescriptor d;uint64_t scalarOut=0xdeadbeef;switch(bad){
 case 0:state.scalarInputCount=0;break;case 1:state.scalarInputCount=2;break;case 2:state.scalarInput=nullptr;break;
 case 3:state.scalarOutput=&scalarOut;state.scalarOutputCount=1;break;case 4:state.structureInputSize=1;break;case 5:state.structureInputDescriptor=&d;break;
 case 6:state.structureOutputDescriptor=&d;break;case 7:state.asyncWakePort=1;break;case 8:state.asyncReferenceCount=1;break;
 case 9:state.structureVariableOutputData=(OSObject**)&scalarOut;break;case 10:stateHandle=1;break;}
 memset(stateBytes,0xa5,64);unsigned prior=statusCalls;assert(stateCall(state)==kIOReturnBadArgument&&statusCalls==prior);unchangedState();stateHandle=0;}
 state=stateArgs();state.structureOutput=nullptr;assert(stateCall(state)==kIOReturnNoSpace);
 ready=false;state=stateArgs();memset(stateBytes,0xa5,64);unsigned prior=statusCalls;assert(stateCall(state)==kIOReturnNotReady&&statusCalls==prior);unchangedState();ready=true;
 statusError=kIOReturnNotReady;state=stateArgs();assert(stateCall(state)==kIOReturnNotReady&&state.structureOutputSize==64);unchangedState();statusError=0;
 for(unsigned bad=0;bad<4;++bad){backendStatus={1,1,KEBAB_DEVICE_BAG_CLASS_C_D_MASK};if(bad==0)backendStatus.version=2;if(bad==1)backendStatus.policy=2;if(bad==2)backendStatus.available_classes=1u<<4;if(bad==3)backendStatus.available_classes|=1u<<1;state=stateArgs();assert(stateCall(state)==kIOReturnNotReady&&state.structureOutputSize==64);unchangedState();}
 backendStatus={1,1,KEBAB_DEVICE_BAG_CLASS_C_D_MASK};
 IOMemoryDescriptor stateDescriptor;memset(stateDescriptor.bytes,0xa5,sizeof(stateDescriptor.bytes));state=stateArgs();state.structureOutput=nullptr;state.structureOutputDescriptor=&stateDescriptor;state.structureOutputDescriptorSize=99;
 for(unsigned capacity=0;capacity<sizeof(stateOracle);++capacity){stateDescriptor.length=capacity;assert(stateCall(state)==kIOReturnNoSpace&&state.structureOutputDescriptorSize==99);}stateDescriptor.length=64;
 assert(stateCall(state)==0&&state.structureOutputDescriptorSize==sizeof(stateOracle)&&!memcmp(stateDescriptor.bytes,stateOracle,sizeof(stateOracle)));for(unsigned k=sizeof(stateOracle);k<64;++k)assert(stateDescriptor.bytes[k]==0xa5);
 stateDescriptor.shortWrite=true;state.structureOutputDescriptorSize=99;assert(stateCall(state)==kIOReturnIOError&&state.structureOutputDescriptorSize==99);stateDescriptor.shortWrite=false;
 state=stateArgs();memset(stateBytes,0xa5,64);assert(stateClient.externalMethod(35,&state,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged);unchangedState();
 stateClient.clientClose();assert(stateCall(state)==kIOReturnNotOpen);unchangedState();

 IOUserClient *publicBase=nullptr;assert(provider.newUserClient((void*)2,nullptr,0,&publicBase)==0&&publicBase);
 auto &publicClient=*static_cast<AppleKeyStoreUserClient *>(publicBase);IOExternalMethodArguments pub;
 assert(publicClient.externalMethod(17,&pub,nullptr,nullptr,nullptr)==kIOReturnNotOpen);
 assert(publicClient.externalMethod(0,&pub,nullptr,nullptr,nullptr)==0);
 unsigned oldMappings=mappings;uint64_t zeroHandle=0,value=99;uint8_t publicBytes[64];
 pub={};pub.scalarInput=&zeroHandle;pub.scalarInputCount=1;pub.scalarOutput=&value;pub.scalarOutputCount=1;assert(publicClient.externalMethod(7,&pub,nullptr,nullptr,nullptr)==0&&value==4);
 pub={};pub.scalarOutput=&value;pub.scalarOutputCount=1;assert(publicClient.externalMethod(14,&pub,nullptr,nullptr,nullptr)==0&&value==0);
 pub={};pub.scalarInput=&zeroHandle;pub.scalarInputCount=1;pub.structureOutput=publicBytes;pub.structureOutputSize=64;assert(publicClient.externalMethod(17,&pub,nullptr,nullptr,nullptr)==0&&pub.structureOutputSize==21&&!memcmp(publicBytes,stateOracle,21));
 for(unsigned selector=1;selector<128;++selector){if(selector==7||selector==14||selector==17)continue;pub={};pub.scalarInput=&zeroHandle;pub.scalarInputCount=1;pub.structureOutput=publicBytes;pub.structureOutputSize=64;memset(publicBytes,0xa5,64);unsigned oldStatus=statusCalls;assert(publicClient.externalMethod(selector,&pub,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged&&pub.structureOutputSize==64&&statusCalls==oldStatus&&mappings==oldMappings);for(auto x:publicBytes)assert(x==0xa5);cases++;}
 // Reopening never promotes a public connection.
 pub={};assert(publicClient.externalMethod(0,&pub,nullptr,nullptr,nullptr)==0);assert(publicClient.externalMethod(3,&pub,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged);
 publicClient.clientClose();assert(publicClient.externalMethod(17,&pub,nullptr,nullptr,nullptr)==kIOReturnNotOpen);

 // New keychain capability is strict-boolean, immutable, and never bag admin.
 for(unsigned role=1;role<=5;role++){
  IOUserClient*base=nullptr;assert(provider.newUserClient((void*)(uintptr_t)role,nullptr,0,&base)==0&&base&&!entitlementRefs);
  IOExternalMethodArguments open;assert(base->externalMethod(0,&open,nullptr,nullptr,nullptr)==0);
  uint64_t args[2]={0,7},returned=0xdeadbeef;uint8_t raw[32],sealed[64],clear[64];memset(raw,0x36,32);
  auto packet=[&](bool encrypt){IOExternalMethodArguments v;v.scalarInput=args;v.scalarInputCount=2;v.scalarOutput=encrypt?&returned:nullptr;v.scalarOutputCount=encrypt?1:0;v.structureInput=encrypt?raw:sealed;v.structureInputSize=encrypt?32:40;v.structureOutput=encrypt?sealed:clear;v.structureOutputSize=64;return v;};
  auto callKey=[&](bool encrypt,IOExternalMethodArguments&a){cases++;return base->externalMethod(encrypt?10:11,&a,nullptr,nullptr,nullptr);};
  for(bool encrypt:{false,true}){auto v=packet(encrypt);memset(sealed,0x5a,64);memset(clear,0x5a,64);unsigned before=keyCalls;
   if(role!=3){assert(callKey(encrypt,v)==kIOReturnNotPrivileged&&keyCalls==before&&returned==0xdeadbeef&&v.structureOutputSize==64);for(auto x:sealed)assert(x==0x5a);for(auto x:clear)assert(x==0x5a);}
  }
  if(role==3){
   for(unsigned sel=1;sel<128;sel++){if(sel==7||sel==14||sel==17||sel==10||sel==11)continue;IOExternalMethodArguments v;unsigned before=keyCalls;assert(base->externalMethod(sel,&v,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged&&keyCalls==before);cases++;}
   for(unsigned cls:{7u,8u,10u,11u}){
    args[1]=cls;auto wrap=packet(true);memset(sealed,0x5a,64);assert(callKey(true,wrap)==0&&wrap.structureOutputSize==40&&returned==cls);for(unsigned i=40;i<64;i++)assert(sealed[i]==0x5a);
    auto unwrap=packet(false);memset(clear,0x5a,64);assert(callKey(false,unwrap)==0&&unwrap.structureOutputSize==32&&!memcmp(clear,raw,32));for(unsigned i=32;i<64;i++)assert(clear[i]==0x5a);
   }
   args[1]=7;
   for(bool encrypt:{false,true}){
    for(unsigned n=0;n<65;n++)if(n!=(encrypt?32u:40u)){auto v=packet(encrypt);v.structureInputSize=n;unsigned before=keyCalls;assert(callKey(encrypt,v)==kIOReturnBadArgument&&keyCalls==before);}
    for(unsigned n=0;n<(encrypt?40u:32u);n++){auto v=packet(encrypt);v.structureOutputSize=n;unsigned before=keyCalls;assert(callKey(encrypt,v)==kIOReturnNoSpace&&keyCalls==before&&v.structureOutputSize==n);}
    for(unsigned fault=0;fault<13;fault++){
     auto v=packet(encrypt);IOMemoryDescriptor d;uint64_t modified[2]={0,7};v.scalarInput=modified;
     switch(fault){case 0:v.scalarInputCount=0;break;case 1:v.scalarInputCount=3;break;case 2:v.scalarInput=nullptr;break;case 3:modified[0]=1;break;case 4:modified[1]=UINT64_MAX;break;case 5:v.scalarOutputCount=encrypt?0:1;break;case 6:if(encrypt)v.scalarOutput=nullptr;else v.structureInput=nullptr;break;case 7:v.structureInputDescriptor=&d;break;case 8:v.structureOutputDescriptor=&d;break;case 9:v.structureInput=nullptr;break;case 10:v.asyncWakePort=1;break;case 11:v.asyncReferenceCount=1;break;case 12:v.structureVariableOutputData=(OSObject**)&returned;break;}
     unsigned before=keyCalls;assert(callKey(encrypt,v)==kIOReturnBadArgument&&keyCalls==before);
    }
    auto v=packet(encrypt);v.structureOutput=nullptr;assert(callKey(encrypt,v)==kIOReturnNoSpace);
    IOMemoryDescriptor d;v.structureOutputDescriptor=&d;d.length=encrypt?39:31;unsigned before=keyCalls;assert(callKey(encrypt,v)==kIOReturnNoSpace&&keyCalls==before);
    d.length=64;d.shortWrite=true;v.structureOutputDescriptorSize=99;assert(callKey(encrypt,v)==kIOReturnIOError&&v.structureOutputDescriptorSize==99);
    d.shortWrite=false;assert(callKey(encrypt,v)==0&&v.structureOutputDescriptorSize==(encrypt?40u:32u));
    auto broken=packet(encrypt);keyFailure=kIOReturnNotReady;memset(sealed,0x5a,64);memset(clear,0x5a,64);returned=0xdeadbeef;
    assert(callKey(encrypt,broken)==kIOReturnNotReady&&broken.structureOutputSize==64&&returned==0xdeadbeef);for(auto x:sealed)assert(x==0x5a);for(auto x:clear)assert(x==0x5a);keyFailure=0;
    args[1]=9;broken=packet(encrypt);assert(callKey(encrypt,broken)==kIOReturnUnsupported&&broken.structureOutputSize==64);args[1]=7;
    // Restore the modeled authentic wrapped input for the next direction.
    auto restore=packet(true);assert(callKey(true,restore)==0);
   }
   entitledThree=false;open={};assert(base->externalMethod(0,&open,nullptr,nullptr,nullptr)==0);auto same=packet(true);assert(callKey(true,same)==0);entitledThree=true;
  }
  base->clientClose();auto closed=packet(true);unsigned before=keyCalls;assert(callKey(true,closed)==kIOReturnNotOpen&&keyCalls==before);base->release();assert(!entitlementRefs);
 }
 // A non-entitled connection cannot acquire capability by opening again.
 entitledThree=false;IOUserClient*unentitled=nullptr;assert(provider.newUserClient((void*)3,nullptr,0,&unentitled)==0);entitledThree=true;IOExternalMethodArguments reopen;assert(unentitled->externalMethod(0,&reopen,nullptr,nullptr,nullptr)==0);assert(unentitled->externalMethod(10,&reopen,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged);unentitled->release();assert(!entitlementRefs);

 publicBase->release();rootBase->release();nc1->release();nc2->release();nc3->release();
 printf("PASS %u actual selector/access/marshal/refusal cases; mocked IOKit and sealed-bank export, hardware not claimed\n",cases);
}
