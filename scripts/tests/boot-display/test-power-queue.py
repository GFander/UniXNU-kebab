from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--receipt-dir',type=Path,required=True);options=parser.parse_args();options.receipt_dir.mkdir(parents=True,exist_ok=True);w=options.receipt_dir
from pathlib import Path
import hashlib,json,subprocess,tempfile
p=options.source/'iokit/Kernel/arm/KebabFramebuffer.cpp';s=p.read_text();a=s.index('IOReturn\nAppleCLCD::requestPower(');b=s.index('\nbool\nAppleCLCD::available()',a);body=s[a:b]
shim=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
using IOReturn=int;constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1,KEBAB_FB_SWAP_REC_SIZE=0x46c;
struct OSObject {virtual ~OSObject()=default;};
static bool gated,panicking,panic_during_present,concurrent,fake_logo,power_allowed=true,press_during_match;static unsigned matches;
static int kebab_keys_boot_logo_idle_allowed(){return power_allowed;}
static int kebab_boot_logo_matches_framebuffer(uintptr_t,unsigned,unsigned,unsigned){matches++;if(press_during_match)power_allowed=false;return fake_logo;}static int locked;static std::vector<int> events;
static bool panic_active(){return panicking;}static uint64_t mach_absolute_time(){return 777;}
static void IOLockLock(void*){assert(!locked);locked=1;}static void IOLockUnlock(void*){assert(locked);locked=0;}
static void IOLockWakeup(void*,void*,bool){assert(locked);events.push_back(2);}
#define IOLog(...) do{}while(0)
struct WorkLoop {using Action=IOReturn(*)(OSObject*,void*,void*,void*,void*);IOReturn runAction(Action a,OSObject *o,void *p=nullptr){assert(!gated);gated=true;int r=a(o,p,nullptr,nullptr,nullptr);gated=false;return r;}};
class AppleCLCD:public OSObject {public:
 WorkLoop work,*fWorkLoop=&work;void *fLock=this;bool fStopping=false,fPowerOff=false,fPendingValid=true,fHaveFrame=true,fOffApplied=false;
 bool fBootIdleDecisionUsed=false,fBootLogoRetained=false;uintptr_t fPixels=0x1000;unsigned fWidth=1080,fHeight=2400,fRowBytes=4320;
 uint8_t fPending[KEBAB_FB_SWAP_REC_SIZE]={};uint32_t fPendingID=4,fCompletedID=3;uint64_t fCompletedTime=0;unsigned presented=0,blanked=0;
 IOReturn requestPower(bool);static IOReturn powerChangeAction(OSObject*,void*,void*,void*,void*);
 void present(const uint8_t *p,uint32_t id){assert(gated&&!locked);assert(id==4);presented++;events.push_back(1);if(concurrent){fPendingValid=true;fPendingID=5;}if(panic_during_present)panicking=true;}
 void blank(){assert(gated&&!locked&&!panicking);blanked++;events.push_back(3);}
};
'''
main=r'''
int main(int argc,char **argv){int mode=argc>1?atoi(argv[1]):0;AppleCLCD fb;
 if(mode==1)fb.fPendingValid=false;
 if(mode==2)fb.fStopping=true;
 if(mode==3)panicking=true;
 if(mode==4){fb.fPowerOff=true;fb.fOffApplied=true;fb.fPendingID=5;}
 if(mode==5)concurrent=true;
 if(mode==6)panic_during_present=true;
 if(mode>=7)fake_logo=true;
 if(mode==8)power_allowed=false;
 if(mode==9)press_during_match=true;
 if(mode==10)fb.fBootIdleDecisionUsed=true;
 int result=fb.requestPower(mode!=4);assert(!locked&&!gated);
 if(mode==2||mode==3){assert(result==kIOReturnNotReady&&events.empty()&&fb.fCompletedID==3&&!fb.fPowerOff);return 0;}
 if(mode==4){assert(result==0&&!fb.fPowerOff&&fb.fPendingValid&&fb.fPendingID==5&&!fb.fOffApplied&&events.empty());return 0;}
 if(mode==6){assert(result==kIOReturnNotReady&&fb.presented==1&&fb.fCompletedID==3&&!fb.fOffApplied);return 0;}
 if(mode==7){assert(result==0&&fb.fBootLogoRetained&&fb.fOffApplied&&fb.fPowerOff&&fb.blanked==0&&fb.fCompletedID==4&&matches==1);power_allowed=false;tick(&fb);assert(fb.blanked==1&&!fb.fBootLogoRetained);power_allowed=true;fb.requestPower(false);fb.requestPower(true);assert(fb.blanked==2&&matches==1);return 0;}
 assert(result==0&&fb.fPowerOff&&fb.fOffApplied&&fb.blanked==1);
 if(mode==1){assert(fb.presented==0&&fb.fCompletedID==3&&events==std::vector<int>({3}));fb.requestPower(true);assert(fb.blanked==1);return 0;}
 assert(fb.presented==1&&fb.fCompletedID==4&&fb.fCompletedTime==777&&events==std::vector<int>({1,2,3}));
 assert(fb.fPendingValid==(mode==5));if(mode==5)assert(fb.fPendingID==5);
 if(mode==0){fb.requestPower(false);assert(!fb.fPowerOff&&!fb.fOffApplied&&fb.fPendingValid);}
 return 0;}
'''
a=s.index('\tif (fb->fPowerOff) {',s.index('AppleCLCD::vsyncFired'));b=s.index('} else if (present)',a);timer=s[a:b+1];shim+='\nstatic void tick(AppleCLCD *fb){gated=true;'+timer+'gated=false;}\n';code=shim+body+main
mutants=[('drop-accepted-pending-before-off','if (off && !fb->fPowerOff && fb->fPendingValid)', 'if (false && off && !fb->fPowerOff && fb->fPendingValid)'),('fake-completion-without-present','fb->present(pending, id);','(void)pending;'),('bypass-workloop-gate','return fWorkLoop->runAction(&AppleCLCD::powerChangeAction, this,','return AppleCLCD::powerChangeAction(this,'),('drop-concurrent-new-swap','fb->fPowerOff = off;','fb->fPowerOff = off; if(off)fb->fPendingValid=false;')]
with tempfile.TemporaryDirectory(prefix='power-queue-') as tmp:
 t=Path(tmp);src=t/'test.cpp';exe=t/'test';command=['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(src),'-o',str(exe)]
 def compile(text):
  src.write_text(text);p=subprocess.run(command,capture_output=True,text=True);assert p.returncode==0,p.stderr
 compile(code)
 for mode in range(11):subprocess.run([str(exe),str(mode)],check=True,capture_output=True)
 receipts=[]
 for name,before,after in mutants:
  assert before in code;m=code.replace(before,after)
  if name=='bypass-workloop-gate':m=m.replace('(void *)(uintptr_t)off);','(void *)(uintptr_t)off, nullptr, nullptr, nullptr);')
  compile(m);failures=[i for i in range(11) if subprocess.run([str(exe),str(i)],capture_output=True).returncode!=0];assert failures,name;receipts.append(dict(name=name,compiled=True,runtime_rejected_cases=failures))
(w/'host-power-queue-tests.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),actual_power_methods_compiled=True,cases=11,models='WorkLoop gate/locks/time/wakeup/present/blank explicit host models, no real pixels or hardware',asan_ubsan=True,compiled_runtime_mutants=receipts,physical_queue_and_black_gap_not_yet_qualified=True),indent=2)+'\n');print('PASS11 actual power/gate/drain/order/panic/concurrent-swap cases;4 compiled runtime mutants')
