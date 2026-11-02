"""Exercise production TCM -> logical coordinates -> HID bytes on the host."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
os.environ['TMPDIR']='/dev/shm'
names=['kebab_touch_tcm.c','kebab_touch_tcm.h','kebab_hid_bridge.h','kebab_hid_queue.h','kebab_hid_reports.h']
raw={n:(a.source/'bsd/dev'/n).read_bytes() for n in names}
fixture=r'''
#include "kebab_touch_tcm.h"
#include "kebab_hid_reports.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void axis(unsigned max, int y) {
 int last=-1; unsigned logical=y?2436:1125, panel=y?2400:1080;
 for(unsigned v=0;v<=max;v++) {
  kebab_tcm_point p=kebab_tcm_transform(y?0:v,y?v:0,max,max);
  int got=y?p.logical_y:p.logical_x, physical=y?p.panel_y:p.panel_x;
  CHECK(!p.clamped); CHECK(got>=last && got>=0 && got<(int)logical);
  CHECK(physical==(int)(((uint64_t)v*(panel-1)+max/2)/max));
  CHECK(llabs((long long)got*max-(long long)v*(logical-1))*2<=max);
  if(!v) CHECK(got==0); if(v==max) CHECK(got==(int)logical-1);
  last=got;
 }
}
int main(void) {
 unsigned maxima[]={1,2,17,1000,1079,2000,2399,4095};
 for(unsigned i=0;i<sizeof(maxima)/sizeof(maxima[0]);i++){axis(maxima[i],0);axis(maxima[i],1);}
 /* Independent display sampling correspondence: at most one logical pixel
  * from the existing complete-canvas pixel-centre presentation. */
 for(unsigned y=0;y<2400;y++)for(unsigned x=0;x<1080;x++) {
  kebab_tcm_point p=kebab_tcm_transform(x,y,1079,2399);
  CHECK(abs(p.logical_x-(int)(((uint64_t)(2*x+1)*1125)/(2*1080)))<=1);
  CHECK(abs(p.logical_y-(int)(((uint64_t)(2*y+1)*2436)/(2*2400)))<=1);
 }
 /* Check actual report-declared inclusive maxima, not just queue bounds. */
 unsigned maxima_found=0;
 for(unsigned i=0;i+3<sizeof(kebab_hid_touch_descriptor);i++) {
  const uint8_t *d=kebab_hid_touch_descriptor+i;
  if(d[0]==0x26 && d[1]==0x64 && d[2]==0x04)maxima_found++;
  if(d[0]==0x26 && d[1]==0x83 && d[2]==0x09)maxima_found++;
 }
 CHECK(maxima_found==20);
 struct kebab_hid_frame f={0},lift={0};uint8_t bytes[KEBAB_HID_TOUCH_BYTES];
 f.timestamp_ns=1;f.active=1023;
 for(unsigned i=0;i<10;i++) {
  kebab_tcm_point p=kebab_tcm_transform((i&1)?1079:0,(i&2)?2399:0,1079,2399);
  f.x[i]=p.logical_x;f.y[i]=p.logical_y;f.pressure[i]=i+1;
 }
 CHECK(kebab_hid_encode(KEBAB_HID_TOUCH,&f,NULL,bytes,sizeof(bytes))==sizeof(bytes));
 CHECK(bytes[sizeof(bytes)-1]==10);
 for(unsigned i=0;i<10;i++) {
  uint8_t *d=bytes+1+7*i;CHECK(d[0]==3 && d[1]==i);
  CHECK((unsigned)(d[2]|d[3]<<8)==((i&1)?1124:0));
  CHECK((unsigned)(d[4]|d[5]<<8)==((i&2)?2435:0));
 }
 lift.timestamp_ns=2;
 CHECK(kebab_hid_encode(KEBAB_HID_TOUCH,&lift,&f,bytes,sizeof(bytes))==sizeof(bytes));
 for(unsigned i=0;i<10;i++) {uint8_t *d=bytes+1+7*i;CHECK(d[0]==2 && d[1]==i && !d[6]);CHECK((d[2]|d[3]<<8)==f.x[i]);CHECK((d[4]|d[5]<<8)==f.y[i]);}
 for(unsigned i=0;i<3;i++) {
  unsigned max=i==0?0:i==1?0xffff:1079;
  kebab_tcm_point p=kebab_tcm_transform(4095,4095,max,max==1079?2399:max);
  CHECK(p.clamped && p.panel_x==1079 && p.panel_y==2399);
  CHECK(p.logical_x==1124 && p.logical_y==2435);
 }
 printf("PASS %u production mapping/HID checks\n",checks);return 0;
}
'''
source=raw[names[0]].decode().replace('#define KEBAB_TOUCH_KERNEL 1','/* Host header selection only. */')
mutants=[('old-centred-crop',source.replace('(int32_t)(((uint64_t)raw_x * (KEBAB_TCM_LOGICAL_W - 1) +\n\t    max_x / 2u) / max_x)','p.panel_x + 22').replace('(int32_t)(((uint64_t)raw_y * (KEBAB_TCM_LOGICAL_H - 1) +\n\t    max_y / 2u) / max_y)','p.panel_y + 18')),('x-physical-only',source.replace('(KEBAB_TCM_LOGICAL_W - 1)','(KEBAB_TCM_PANEL_W - 1)')),('y-physical-only',source.replace('(KEBAB_TCM_LOGICAL_H - 1)','(KEBAB_TCM_PANEL_H - 1)')),('wrong-inclusive-maximum',source.replace('(KEBAB_TCM_LOGICAL_W - 1)','KEBAB_TCM_LOGICAL_W'))]
results=[]
with tempfile.TemporaryDirectory(prefix='touch-full-canvas-',dir='/dev/shm') as folder:
 w=Path(folder)
 for n in names[1:]:(w/n).write_bytes(raw[n])
 (w/'test.c').write_text(fixture)
 for tag,text,expected in [('production',source,True)]+[(tag,text,False)for tag,text in mutants]:
  assert text!=source or expected,tag
  (w/'production.c').write_text(text)
  subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(w/'test.c'),str(w/'production.c'),'-o',str(w/'test')],check=True)
  result=subprocess.run([str(w/'test')],capture_output=True,text=True,timeout=30)
  assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
  if not expected:assert 'Assertion' in result.stderr, (tag,result.stderr)
  results.append(dict(name=tag,compiled=True,returncode=result.returncode,expected_pass=expected))
  print(result.stdout.strip() if expected else 'Rejected compiled behavior: '+tag,flush=True)
a.output.write_text(json.dumps(dict(passed=True,scope='Host production transform and HID encoder; no hardware/UI gesture claim',source_sha256={n:hashlib.sha256(b).hexdigest()for n,b in raw.items()},fixture_sha256=hashlib.sha256(fixture.encode()).hexdigest(),results=results),indent=2)+'\n')
