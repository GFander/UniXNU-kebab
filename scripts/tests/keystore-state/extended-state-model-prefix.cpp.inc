#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <initializer_list>
using IOReturn=int;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnNotPrivileged=2,kIOReturnBadArgument=3,kIOReturnNotReady=4,kIOReturnNotOpen=5,kIOReturnNoSpace=6,kIOReturnIOError=7,kIOReturnNoMemory=8,kIOReturnError=9};
struct OSObject{};struct IOService{};struct IOExternalMethodDispatch{};
struct IOMemoryDescriptor{size_t length=128;bool shortWrite=false;uint8_t bytes[128];size_t getLength(){return length;}size_t writeBytes(size_t at,const void*p,size_t n){assert(at+n<=length&&length<=128);size_t put=n-(shortWrite?1:0);memcpy(bytes+at,p,put);return put;}};
struct IOExternalMethodArguments{unsigned scalarInputCount=0,scalarOutputCount=0;uint64_t*scalarInput=nullptr,*scalarOutput=nullptr;size_t structureInputSize=0,structureOutputSize=0;void*structureOutput=nullptr;IOMemoryDescriptor*structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;uint32_t structureOutputDescriptorSize=0;unsigned asyncWakePort=0,asyncReferenceCount=0;OSObject**structureVariableOutputData=nullptr;};
struct kebab_device_classbag_status{uint32_t version,policy,available_classes;};
#define KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE 1
#define KEBAB_DEVICE_BAG_CLASS_C_D_MASK 0x18
static kebab_device_classbag_status bank={1,1,0x18};static bool ready=true;static int statusError=0,statusCalls=0,mutations=0;
static bool kuc_ready(){return ready;}
static int kebab_device_classbag_get_status(kebab_device_classbag_status*p){statusCalls++;if(statusError)return statusError;*p=bank;return 0;}
#define IOLog(...) ((void)0)
static int proc_selfpid(){return 27;}
static void*IOMalloc(size_t n){return malloc(n);}static void IOFree(void*p,size_t){free(p);}
static int kebab_device_classbag_export(void*,size_t){mutations++;return kIOReturnError;}
static int kebab_device_keybag_get_lock_state(uint32_t*v){*v=4;return 0;}
static int kuc_device_lock_method(uint32_t,IOExternalMethodArguments*){mutations++;return kIOReturnUnsupported;}
static int kuc_keychain_method(uint32_t,IOExternalMethodArguments*){mutations++;return kIOReturnUnsupported;}
static int kuc_cookie_method(uint32_t,IOExternalMethodArguments*){mutations++;return kIOReturnUnsupported;}
static int kuc_notify_method(IOService*,uint32_t,IOExternalMethodArguments*){mutations++;return kIOReturnUnsupported;}
class AppleKeyStoreUserClient{public:bool administrative,keychainAccess,deviceAccess,opened=false;AppleKeyStoreUserClient(unsigned role):administrative(role&1),keychainAccess(role&2),deviceAccess(role&4){}IOService*getProvider(){return nullptr;}IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*);};
