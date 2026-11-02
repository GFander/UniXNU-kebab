"""Execute actual client/auth bodies with explicit IOKit lifetime models."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
w=Path(__file__).resolve().parent;source=(w/'KebabSoftwareScaler.cpp').read_text()
def cut(begin,end):return source[source.index(begin):source.index(end,source.index(begin))]
auth=cut('static bool\nksc_authorized','\nclass IOSurfaceAcceleratorClient')
transfer=cut('static struct ksc_image\nksc_description','\nIOReturn\nIOSurfaceAcceleratorClient::externalMethod')
external=cut('IOReturn\nIOSurfaceAcceleratorClient::externalMethod','\nextern "C" int\nkebab_scaler_start')
shim=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <vector>
#include <cassert>
#include <mutex>
using task_t=int;using proc_t=int;using IOReturn=int;using OSObject=int;using IOExternalMethodDispatch=int;
using io_user_reference_t=uint64_t;using OSAsyncReference64=uint64_t[8];
#define TASK_NULL 0
#define MACH_PORT_NULL 0
#define OSSafeReleaseNULL(x) do{if(x){(x)->release();(x)=nullptr;}}while(0)
enum{ kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnUnsupported=2,kIOReturnNotFound=3,kIOReturnNotReady=4,kIOReturnVMError=5,kIOReturnNoDevice=6,kIOReturnNotPermitted=7,kIOUserNotifyOptionCanDrop=1,kIOMapAnywhere=1,kIOMapReadOnly=2 };
static int current=1,proc=1,procTask=1,uid=501;static uint8_t actualUUID[16]={0x70,0xb7,0x23,0x40,0x73,0x7a,0x39,0x73,0x8b,0x47,0x4e,0x99,0x70,0xf4,0xec,0xb9};
static task_t current_task(){return current;}static proc_t current_proc(){return proc;}
static task_t proc_task(proc_t){return procTask;}static int proc_ucred(proc_t){return 1;}static int kauth_cred_getuid(int){return uid;}
static void proc_getexecutableuuid(proc_t,uint8_t *q,size_t n){memcpy(q,actualUUID,n);}
static void IOLog(const char *,...){}static void OSMemoryBarrier(){}
static int kernel_task=0,activeMaps=0,notifications=0,notifyResult=0,writes=0;static bool failSourceMap=false,failDestMap=false;
static std::vector<uint8_t> original(32),copied(32,0xa5);
class IOMemoryMap {
 std::vector<uint8_t> *data;
 public:IOMemoryMap(std::vector<uint8_t> *d):data(d){activeMaps++;}
 uintptr_t getVirtualAddress(){return (uintptr_t)data->data();}size_t getLength(){return data->size();}
 void release(){activeMaps--;delete this;}
};
class IOMemoryDescriptor {
 public:std::vector<uint8_t> *data;bool source;
 size_t getLength(){return data->size();}
 IOMemoryMap *createMappingInTask(task_t,int,int){if(source?failSourceMap:failDestMap)return nullptr;return new IOMemoryMap(data);}
};
class KebabSurface {
 public:uint32_t id,width=2,height=2,bytesPerRow=8,pixelFormat=0x42475241,offset=0,allocSize=32,planeCount=0,parentID=0,mapCache=0;
 uint16_t bytesPerElement=4;uint8_t elementWidth=1,elementHeight=1;IOMemoryDescriptor *memory;int refs=1;
 void release(){refs--;}
};
static IOMemoryDescriptor sm={&original,true},dm={&copied,false};static KebabSurface s,d;
static KebabSurface *kebab_surface_copy(uint32_t id){KebabSurface *p=id==2?&s:id==3?&d:nullptr;if(p)p->refs++;return p;}
static void kebab_surface_did_write(KebabSurface *){assert(copied[0]==original[0]);writes++;}
struct IOExternalMethodArguments{uint32_t scalarInputCount=0,scalarOutputCount=0,structureOutputSize=0,asyncReferenceCount=0,structureInputSize=0;void *structureOutputDescriptor=nullptr,*structureInputDescriptor=nullptr;const void *structureInput=nullptr;};
struct KebabUserClientCall{std::mutex &m;KebabUserClientCall(std::mutex *p):m(*p){m.lock();}~KebabUserClientCall(){m.unlock();}};
class IOSurfaceAcceleratorClient{
 public:std::mutex lock;std::mutex *fLock=&lock;task_t fTask=1;int fPort=1;bool fClosed=false,inactive=false;uint64_t fCompleted=0;
 bool isInactive(){return inactive;}
 static void setAsyncReference64(uint64_t *q,int port,uint64_t cb,uint64_t ctx,int task){q[0]=port;q[1]=cb;q[2]=ctx;q[3]=task;}
 static int sendAsyncResult64WithOptions(uint64_t *q,int result,uint64_t *arg,int n,int options){assert(writes==1 && copied[0]==original[0] && activeMaps==2);if(q[0]==0)return 0;assert(q[0]==1 && q[1]==0x1234 && q[2]==0x5678 && q[3]==1 && result==0 && n==1 && *arg==0x9abc && options==1);notifications++;return notifyResult;}
 IOReturn transform(const uint8_t *,size_t);
 IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *);
};
'''
main=r'''
static void reset(){s.id=2;s.memory=&sm;s.pixelFormat=0x42475241;s.parentID=0;d.id=3;d.memory=&dm;d.parentID=0;s.refs=d.refs=1;sm.data=&original;dm.data=&copied;failSourceMap=failDestMap=false;notifications=writes=notifyResult=0;activeMaps=0;current=proc=procTask=1;uid=501;for(unsigned i=0;i<32;i++)original[i]=i+1;std::fill(copied.begin(),copied.end(),0xa5);}
int main(){
 assert(ksc_authorized(1));assert(!ksc_authorized(0));assert(!ksc_authorized(2));uid=0;assert(!ksc_authorized(1));uid=501;actualUUID[0]^=1;assert(!ksc_authorized(1));actualUUID[0]^=1;procTask=2;assert(!ksc_authorized(1));procTask=1;
 for(int mode=0;mode<15;mode++){
  reset();IOSurfaceAcceleratorClient c;uint8_t q[368]={};uint32_t ids[2]={2,3};memcpy(q,ids,8);uint64_t params[4]={0x1234,0x5678,0x9abc,0x3000};memcpy(q+8,params,32);IOExternalMethodArguments a;a.structureInput=q;a.structureInputSize=sizeof(q);int want=0;unsigned selector=1;
  switch(mode){case 1:failSourceMap=true;want=5;break;case 2:failDestMap=true;want=5;break;case 3:s.pixelFormat=0x26424741;want=2;break;case 4:d.memory=s.memory;want=2;break;case 5:c.fPort=0;break;case 6:notifyResult=9;want=9;break;case 7:c.fClosed=true;want=6;break;case 8:uid=0;want=7;break;case 9:selector=4;want=2;break;case 10:a.structureInputSize--;want=1;break;case 11:a.structureInputDescriptor=&s;want=1;break;case 12:a.scalarInputCount=1;want=1;break;case 13:case 14:s.pixelFormat=d.pixelFormat=KPF_W40A;s.bytesPerElement=d.bytesPerElement=8;s.bytesPerRow=d.bytesPerRow=16;for(unsigned i=0;i<4;i++)for(unsigned ch=0;ch<4;ch++){uint16_t word=(ch==3?894:384)<<6;original[i*8+ch*2]=word;original[i*8+ch*2+1]=word>>8;}if(mode==14){original[6]=0;original[7]=0xa0;want=0;}break;}
  int result=c.externalMethod(selector,&a,nullptr,nullptr,nullptr);assert(result==want);assert(s.refs==1 && d.refs==1 && activeMaps==0);
  if(mode==13||mode==14){assert(writes==1&&notifications==1&&c.fCompleted==1&&!memcmp(original.data(),copied.data(),32));}else if(mode==0){assert(writes==1 && notifications==1 && c.fCompleted==1);assert(!memcmp(original.data(),copied.data(),16));for(int i=16;i<32;i++)assert(copied[i]==0xa5);}
  else if(mode==5)assert(writes==1 && notifications==0 && c.fCompleted==1);
  else if(mode==6)assert(writes==1 && notifications==1 && c.fCompleted==0);
  else assert(writes==0 && notifications==0 && c.fCompleted==0);
 }
 puts("PASS original auth body and15 actual client copy/cleanup/completion/error cases");
}
'''
body=shim+(w/'KebabScalerCore.h').read_text()+auth+transfer+external+main
with tempfile.TemporaryDirectory(prefix='ksc-client-') as tmp:
 t=Path(tmp);p=t/'test.cpp';p.write_text(body);exe=t/'test'
 compiled=subprocess.run(['clang++','-I',str(w),'-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True)
 if compiled.returncode:print(compiled.stderr.decode());raise SystemExit(compiled.returncode)
 result=subprocess.run([str(exe)],check=True,capture_output=True,text=True);print(result.stdout,end='')
 mutants=[('auth-uuid-ignored','return !memcmp(actual,uuid,sizeof(uuid));','return true;'),('fake-success-on-notify-failure','ret=sendAsyncResult64WithOptions(ref,kIOReturnSuccess,&arg,1,kIOUserNotifyOptionCanDrop);','(void)sendAsyncResult64WithOptions(ref,kIOReturnSuccess,&arg,1,kIOUserNotifyOptionCanDrop);'),('leaked-source-map','OSSafeReleaseNULL(sm);',''),('reject-real-transparent-copy','    ksc_run(&plan,','    if(alpha_rejected!=UINT32_MAX){ret=kIOReturnUnsupported;goto done;} ksc_run(&plan,')]
 receipts=[]
 for name,before,after in mutants:
  assert before in body;p.write_text(body.replace(before,after))
  subprocess.run(['clang++','-I',str(w),'-std=c++17','-O1','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True,capture_output=True)
  result=subprocess.run([str(exe)],capture_output=True);assert result.returncode!=0,name;receipts.append(dict(name=name,compiled=True,runtime_return=result.returncode))
out=dict(passed=True,production_client_sha256=hashlib.sha256((w/'KebabSoftwareScaler.cpp').read_bytes()).hexdigest(),cases=15,auth_body_tested=True,asan_ubsan=True,compiled_runtime_mutants=receipts,explicit_IOKit_models_not_device_proof=True)
(w/'host-client-tests.json').write_text(json.dumps(out,indent=2)+'\n')
