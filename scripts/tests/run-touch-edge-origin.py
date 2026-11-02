"""Actual edge classifier and wrapper; explicit IOKit/original-service stand-ins."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();os.environ['TMPDIR']='/dev/shm'
src=(a.source/'iokit/Kernel/arm/KebabHIDInput.cpp').read_text();body=src[src.index('class KebabTouchEventDriver :'):src.index('static LCK_GRP_DECLARE')];header=(a.source/'iokit/Kernel/arm/KebabTouchEdge.h').read_text()
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "KebabTouchEdge.h"
using IOOptionBits=unsigned;using SInt32=int32_t;
#define KEBAB_HID_TOUCH 2
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(a,b)
#define IOLog(...) ((void)0)
#define OSDynamicCast(t,v) (v)
struct OSNumber {unsigned v;unsigned unsigned32BitValue(){return v;}};
struct IOService {const char *type;IOService *parent;OSNumber *role;IOService *getProvider(){return parent;}OSNumber *getProperty(const char *key){assert(!strcmp(key,"kebab,hid-role"));return role;}bool metaCast(const char *name){return type && !strcmp(name,type);}};
struct OSObject {const char *className="IOHIDEvent";bool metaCast(const char *name){return !strcmp(className,name);}};
struct OSArray {OSObject *items[32]={};unsigned count=0;unsigned getCount(){return count;}OSObject *getObject(unsigned i){assert(i<count && i<32);return items[i];}};
struct IOHIDEvent : OSObject {
 struct Data {uint64_t time=918;unsigned type=11,id=0;int touch=1,range=1;int32_t x=32768,y=32768;unsigned mask=0x40000003,childMask=0x40000003;int integrated=0,builtin=0;unsigned pressure=178,opaque=0xaabbccdd;} d;
 OSArray *children=nullptr;
 unsigned getType(){return d.type;}OSArray *getChildren(){return children;}
 int getIntegerValue(unsigned key,unsigned options){assert(!options);switch(key){case 4:return d.builtin;case 0xb0005:return d.id;case 0xb0009:return d.touch;case 0xb0008:return d.range;case 0xb0007:return d.mask;case 0xb0018:return d.childMask;case 0xb0019:return d.integrated;default:assert(false);return 0;}}
 int32_t getFixedValue(unsigned key,unsigned options){assert(!options);assert(key==0xb0000 || key==0xb0001);return key==0xb0000?d.x:d.y;}
 void setIntegerValue(unsigned key,int value,unsigned options){assert(!options);switch(key){case 0xb0007:d.mask=value;break;case 0xb0018:d.childMask=value;break;case 0xb0019:d.integrated=value;break;default:assert(false);}}
};
static unsigned starts,dispatched;static bool startResult;static IOHIDEvent *last;static unsigned lastOptions;
struct IOHIDEventDriver {virtual bool handleStart(IOService*){starts++;return startResult;}virtual void dispatchEvent(IOHIDEvent *event,IOOptionBits options){dispatched++;last=event;lastOptions=options;}};
'''
tests=r'''
static unsigned cases;
static void send(KebabTouchEventDriver &d,IOHIDEvent &root,unsigned options){unsigned old=dispatched;d.dispatchEvent(&root,options);assert(dispatched==old+1 && last==&root && lastOptions==options);cases++;}
static void frame(KebabTouchEventDriver &driver,IOHIDEvent &root,IOHIDEvent &child,int x,int y,int touch,bool edge){
 root.d.mask=root.d.childMask=0x40000003;child.d.mask=0x40000003;child.d.x=x;child.d.y=y;child.d.touch=child.d.range=touch;
 auto before=child.d;send(driver,root,0x81234567);before.mask|=edge?0x800:0;before.integrated=1;
 assert(!memcmp(&before,&child.d,sizeof(before)));assert(root.d.mask==(0x40000003u|(edge?0x800u:0)));assert(root.d.childMask==root.d.mask);
}
int main(){
 for(int axis=0;axis<2;axis++)for(int v=0;v<=65536;v++) {
  kebab_touch_edge_state s{};int x=axis?32768:v,y=axis?v:32768;
  bool expected=(double)x/65536.0*375<16 || (1.0-(double)x/65536.0)*375<16 || (double)y/65536.0*812<16 || (1.0-(double)y/65536.0)*812<34;
  assert(!!kebab_touch_edge_update(&s,3,1,1,x,y)==expected);
  assert(!!kebab_touch_edge_update(&s,3,1,1,32768,32768)==expected);
  assert(!!kebab_touch_edge_update(&s,3,0,0,32768,32768)==expected);
  assert(!kebab_touch_edge_update(&s,3,0,0,32768,32768) && !s.active && !s.edge);cases+=4;
 }
 kebab_touch_edge_state state{};auto saved=state;
 for(unsigned bad: {10u,31u,32u,0xffffffffu}){assert(!kebab_touch_edge_update(&state,bad,1,1,0,0));assert(!memcmp(&state,&saved,sizeof(state)));}
 for(int bad: {-1,65537,2147483647}){state={};assert(!kebab_touch_edge_update(&state,1,1,1,bad,0));assert(!kebab_touch_edge_update(&state,1,1,1,0,0));}
 state={};assert(!kebab_touch_edge_update(&state,1,1,0,0,0));
 KebabTouchEventDriver driver;OSNumber role{2};IOService device{"KebabHIDDevice",nullptr,&role},iface{"IOHIDInterface",&device,nullptr};
 for(int result=0;result<2;result++){starts=0;startResult=result;assert(driver.handleStart(&iface)==(bool)result && starts==1);}starts=0;
 assert(!driver.handleStart(nullptr) && !starts);role.v=1;assert(!driver.handleStart(&iface) && !starts);role.v=2;device.type="Other";assert(!driver.handleStart(&iface) && !starts);device.type="KebabHIDDevice";
 for(unsigned type=0;type<42;type++)for(unsigned options:{0u,1u,7u,0x80000000u,0xffffffffu}) {
  IOHIDEvent root;root.d.type=type;auto before=root.d;send(driver,root,options);if(type==11)before.integrated=1;assert(!memcmp(&before,&root.d,sizeof(before)));
 }
 IOHIDEvent root,child;OSArray children;children.items[0]=&child;children.count=1;root.children=&children;
 frame(driver,root,child,0,32768,1,true);frame(driver,root,child,32768,32768,1,true);frame(driver,root,child,32768,32768,0,true);
 frame(driver,root,child,32768,32768,1,false);frame(driver,root,child,0,0,1,false);frame(driver,root,child,0,0,0,false);
 frame(driver,root,child,32768,65536,1,true);root.d.mask=0x80;send(driver,root,3);
 frame(driver,root,child,32768,32768,1,false);frame(driver,root,child,32768,32768,0,false);
 IOHIDEvent ten[10];children.count=10;for(unsigned i=0;i<10;i++){ten[i].d.id=i;ten[i].d.x=i&1?32768:0;children.items[i]=&ten[i];}
 root.d.mask=root.d.childMask=3;send(driver,root,4);assert(root.d.mask==0x803 && root.d.childMask==0x803);
 for(unsigned i=0;i<10;i++)assert(ten[i].d.mask==(0x40000003u|((i&1)?0:0x800)));
 for(auto &e:ten){e.d.touch=e.d.range=0;e.d.mask=3;}root.d.mask=root.d.childMask=3;send(driver,root,4);
 for(unsigned i=0;i<10;i++)assert(ten[i].d.mask==(3u|((i&1)?0:0x800)));
 child.d.id=99;child.d.mask=3;child.d.integrated=0;children.count=1;children.items[0]=&child;root.d.mask=root.d.childMask=3;send(driver,root,4);assert(child.d.mask==3 && !child.d.integrated);
 unsigned old=dispatched;driver.dispatchEvent(nullptr,67);assert(dispatched==old+1 && !last && lastOptions==67);
 printf("PASS %u real-origin, lifetime, multi-ID and original-event preservation checks\n",cases);
}
'''
variants=[('production',body,header,True),('move-becomes-edge',body,header.replace('touch && !(state->active&bit)','touch'),False),('edge-state-after-lift',body,header.replace('state->edge&=(uint16_t)~bit;',''),False),('overbroad-side-zone',body,header.replace('(int64_t)x*375<16LL','(int64_t)x*375<160LL'),False),('wrong-bottom-zone',body,header.replace('(65536-y)*812<34LL','(65536-y)*812<16LL'),False),('foreign-ID',body,header.replace('id>=10','id>10'),False),('no-child-edge-bit',body.replace('if (fromEdge) child->setIntegerValue(0xb0007,mask|0x800,0);','/* dropped */'),header,False),('drop-original-mask',body.replace('setIntegerValue(0xb0007,mask|0x800,0)','setIntegerValue(0xb0007,0x800,0)'),header,False),('cancel-retains-origin',body.replace('if (rootMask&0x80) edgeState={};','if (rootMask&0x80) {}'),header,False)]
results=[]
with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='touch-edge-origin-')as folder:
 w=Path(folder)
 for tag,text,h,expected in variants:
  (w/'KebabTouchEdge.h').write_text(h);(w/'test.cpp').write_text(prefix+text+tests)
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-private-field','-Wno-unused-parameter','-fsanitize=address,undefined',str(w/'test.cpp'),'-o',str(w/'test')],check=True)
  result=subprocess.run([str(w/'test')],capture_output=True,text=True,timeout=20);assert(result.returncode==0)==expected,(tag,result.stdout,result.stderr)
  if not expected:assert'Assertion'in result.stderr,(tag,result.stderr)
  results.append(dict(name=tag,compiled=True,expected_pass=expected,returncode=result.returncode));print(result.stdout.strip()if expected else'Rejected compiled behavior: '+tag,flush=True)
a.output.write_text(json.dumps(dict(passed=True,scope='Actual native edge classifier and wrapper with explicit original IOKit/event substitutes; no hardware/GUI claim',source_sha256=hashlib.sha256(src.encode()).hexdigest(),classifier_sha256=hashlib.sha256(header.encode()).hexdigest(),results=results),indent=2)+'\n')
