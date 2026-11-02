"""Run actual board wrapper bodies with explicit original-IOKit stand-ins."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();os.environ['TMPDIR']='/dev/shm'
raw=(a.source/'iokit/Kernel/arm/KebabHIDInput.cpp').read_text();body=raw[raw.index('class KebabTouchEventDriver :'):raw.index('static LCK_GRP_DECLARE')]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using IOOptionBits=unsigned;using SInt32=int32_t;
#define KEBAB_HID_TOUCH 2
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(a,b)
#define IOLog(...) ((void)0)
#define OSDynamicCast(t,v) (v)
struct OSNumber {unsigned v;unsigned unsigned32BitValue(){return v;}};
struct IOService {
 const char *type;IOService *parent;OSNumber *role;
 IOService *getProvider(){return parent;}
 OSNumber *getProperty(const char *key){assert(!strcmp(key,"kebab,hid-role"));return role;}
 bool metaCast(const char *name){return type && !strcmp(name,type);}
};
struct IOHIDEvent {
 unsigned type=11;int integrated=0;uint64_t timestamp=891;unsigned id=9,x=1124,y=2435,mask=7,children=3;
 unsigned getType(){return type;}
 int getIntegerValue(unsigned key,unsigned options){assert(key==0xb0019 && !options);return integrated;}
 void setIntegerValue(unsigned key,int value,unsigned options){assert(key==0xb0019 && !options);integrated=value;}
};
static unsigned starts,dispatched;static bool startResult;static IOHIDEvent *last;static unsigned lastOptions;
struct IOHIDEventDriver {
 virtual bool handleStart(IOService*){starts++;return startResult;}
 virtual void dispatchEvent(IOHIDEvent *event,IOOptionBits options){dispatched++;last=event;lastOptions=options;}
};
'''
tests=r'''
int main(){
 KebabTouchEventDriver driver;OSNumber role{2};IOService device{"KebabHIDDevice",nullptr,&role},interface{"IOHIDInterface",&device,nullptr};
 for(int result=0;result<2;result++){startResult=result;starts=0;assert(driver.handleStart(&interface)==(bool)result && starts==1);}
 starts=0;assert(!driver.handleStart(nullptr) && !starts);
 interface.parent=nullptr;assert(!driver.handleStart(&interface) && !starts);interface.parent=&device;
 interface.type="Other";assert(!driver.handleStart(&interface) && !starts);interface.type="IOHIDInterface";
 device.type="Other";assert(!driver.handleStart(&interface) && !starts);device.type="KebabHIDDevice";
 device.role=nullptr;assert(!driver.handleStart(&interface) && !starts);device.role=&role;
 for(unsigned r=0;r<8;r++)if(r!=2){role.v=r;assert(!driver.handleStart(&interface) && !starts);}role.v=2;
 unsigned checks=0;
 for(unsigned type=0;type<42;type++)for(unsigned option: {0u,1u,7u,0x80000000u,0xffffffffu}) {
  IOHIDEvent e;e.type=type;IOHIDEvent before=e;unsigned old=dispatched;
  driver.dispatchEvent(&e,option);assert(dispatched==old+1 && last==&e && lastOptions==option);
  if(type==11)before.integrated=1;
  assert(!memcmp(&e,&before,sizeof(e)));checks++;
 }
 assert(checks==210);unsigned old=dispatched;driver.dispatchEvent(nullptr,67);assert(dispatched==old+1 && !last && lastOptions==67);
 puts("PASS physical provider gating, base start failure,210 event/option preservation cases and null forwarding");
}
'''
tests=tests.replace('#include <initializer_list>','')
prefix+='#include <initializer_list>\n'
variants=[('production',body,True),('foreign-provider',body.replace('!device->metaCast("KebabHIDDevice")','false'),False),('foreign-role',body.replace('role->unsigned32BitValue()!=KEBAB_HID_TOUCH','false'),False),('wrong-event-field',body.replace('setIntegerValue(0xb0019,1,0)','setIntegerValue(0xb0018,1,0)'),False),('all-event-types',body.replace('event && event->getType()==11','event'),False),('drop-options',body.replace('dispatchEvent(event,options);','dispatchEvent(event,0);'),False),('drop-original-dispatch',body.replace('IOHIDEventDriver::dispatchEvent(event,options);','/* dropped */'),False),('hide-start-failure',body.replace('return result;','return true;'),False)]
results=[]
with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='touch-panel-')as temp:
 w=Path(temp)
 for tag,text,expected in variants:
  assert text!=body or expected
  (w/'test.cpp').write_text(prefix+text+tests)
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-private-field','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined',str(w/'test.cpp'),'-o',str(w/'test')],check=True)
  result=subprocess.run([str(w/'test')],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==expected,(tag,result.stdout,result.stderr)
  if not expected:assert 'Assertion' in result.stderr,(tag,result.stderr)
  results.append(dict(name=tag,compiled=True,expected_pass=expected,returncode=result.returncode));print(result.stdout.strip()if expected else'Rejected compiled behavior: '+tag,flush=True)
a.output.write_text(json.dumps(dict(passed=True,scope='Actual production wrapper with explicit original IOKit/event stand-ins; no physical GUI proof',source_sha256=hashlib.sha256(raw.encode()).hexdigest(),results=results),indent=2)+'\n')
