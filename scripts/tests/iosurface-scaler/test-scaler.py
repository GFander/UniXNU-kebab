"""Compile the actual integer CPU core; compare every output byte to Fraction math."""
from pathlib import Path
from fractions import Fraction as F
import hashlib,json,random,struct,subprocess,tempfile
w=Path(__file__).resolve().parent
core=(w/'KebabScalerCore.h').read_text()
prefix='#include <cstdint>\n#include <cstddef>\n#include <cstring>\n#include <cstdio>\n#include <cstdlib>\n'
harness=r'''
int main(int argc,char **argv){
 if(argc!=3)return 2;FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 ksc_image s={},d={};unsigned char q[KSC_REQUEST_BYTES];
 if(fread(&s,sizeof(s),1,f)!=1 || fread(&d,sizeof(d),1,f)!=1 || fread(q,sizeof(q),1,f)!=1)return 2;
 unsigned char *src=new unsigned char[s.allocation],*dst=new unsigned char[d.allocation];
 if(fread(src,s.allocation,1,f)!=1 || fread(dst,d.allocation,1,f)!=1)return 2;fclose(f);
 ksc_plan p={};int err=ksc_prepare(q,sizeof(q),&s,&d,&p);
 if(!err)ksc_run(&p,&s,&d,src,dst);
 f=fopen(argv[2],"wb");fwrite(&err,4,1,f);fwrite(dst,d.allocation,1,f);fclose(f);
 delete[]src;delete[]dst;return 0;
}
'''
def desc(width,height,stride,offset=0,fmt=0x42475241,allocation=None,length=None):
 a=allocation if allocation is not None else offset+height*stride+17
 return struct.pack('<7IH2B Q',width,height,stride,fmt,offset,a,0,4,1,1,a if length is None else length)
def request(flags=0x3000,rect=None):
 q=bytearray(368);struct.pack_into('<II',q,0,2,3);struct.pack_into('<Q',q,32,flags)
 if rect:
  struct.pack_into('<4Q',q,40,*rect[:4]);struct.pack_into('<4I',q,88,*rect[4:])
 return q
def oracle(sw,sh,ss,so,dw,dh,ds,do,src,dst,rect,nearest):
 sx,sy,rw,rh,dx,dy,ow,oh=rect or (0,0,sw<<16,sh<<16,0,0,dw,dh)
 sx,sy,rw,rh=[F(x,65536) for x in (sx,sy,rw,rh)];out=bytearray(dst)
 for y in range(oh):
  py=max(sy,min(sy+max(F(0),rh-1),sy+(F(y)+F(1,2))*rh/oh-F(1,2)))
  # Production quantizes a coordinate to 16 fractional bits before blending.
  py=F(int(py*65536),65536);y0=int(py);y1=min(y0+1,sh-1);wy=py-y0
  for x in range(ow):
   px=max(sx,min(sx+max(F(0),rw-1),sx+(F(x)+F(1,2))*rw/ow-F(1,2)))
   px=F(int(px*65536),65536);x0=int(px);x1=min(x0+1,sw-1);wx=px-x0
   if nearest:x0=x1=min(int(px+F(1,2)),sw-1);y0=y1=min(int(py+F(1,2)),sh-1);wx=wy=F(0)
   for ch in range(4):
    a,b,c,e=[src[so+iy*ss+ix*4+ch] for ix,iy in ((x0,y0),(x1,y0),(x0,y1),(x1,y1))]
    v=(a*(1-wx)+b*wx)*(1-wy)+(c*(1-wx)+e*wx)*wy
    out[do+(dy+y)*ds+(dx+x)*4+ch]=int(v+F(1,2))
 return bytes(out)
cases=[];rng=random.Random(500)
for sw,sh,dw,dh in ((1,1,7,9),(2,2,3,3),(9,5,9,5),(13,11,7,8),(7,8,19,17)):
 for nearest in (False,True):
  ss=sw*4+12;ds=dw*4+20;so=7;do=9;sd=desc(sw,sh,ss,so);dd=desc(dw,dh,ds,do)
  src=bytes(rng.randrange(256) for _ in range(so+sh*ss+17));dst=bytes([0xa5])*(do+dh*ds+17)
  rect=None
  q=request(0x3000|(0x80 if nearest else 0));expected=oracle(sw,sh,ss,so,dw,dh,ds,do,src,dst,rect,nearest)
  cases.append((f'full-{sw}x{sh}-{dw}x{dh}-{nearest}',sd,dd,q,src,dst,0,expected))
sw,sh,dw,dh=13,11,19,17;ss=64;ds=88;so=7;do=9
src=bytes(rng.randrange(256) for _ in range(so+sh*ss+17));dst=bytes([0xa5])*(do+dh*ds+17)
for rect in ((2<<16,3<<16,7<<16,6<<16,3,4,11,10),(163840,98304,491520,425984,2,3,9,8)):
 cases.append(('crop-'+str(rect),desc(sw,sh,ss,so),desc(dw,dh,ds,do),request(rect=rect),src,dst,0,oracle(sw,sh,ss,so,dw,dh,ds,do,src,dst,rect,False)))
base=cases[-1]
for bit in (0,1,2,6,8,12+20,48,63):
 q=request(flags=0x3000|(1<<bit));cases.append((f'unsupported-flag-{bit}',base[1],base[2],q,src,dst,2,dst))
q=request();q[0x100]=1;cases.append(('unsupported-reserved',base[1],base[2],q,src,dst,2,dst))
q=request();struct.pack_into('<I',q,4,2);cases.append(('same-surface',base[1],base[2],q,src,dst,1,dst))
for field,value,expected in [('format',0x26424741,2),('stride',1,3),('length',1,3),('width',0,1)]:
 sd=bytearray(base[1]);off={'format':12,'stride':8,'length':32,'width':0}[field]
 struct.pack_into('<Q' if field=='length' else '<I',sd,off,value)
 cases.append(('invalid-'+field,sd,base[2],request(),src,dst,expected,dst))
q=request(rect=(0,0,(14<<16),1<<16,0,0,1,1));cases.append(('source-range',base[1],base[2],q,src,dst,3,dst))
q=request(rect=(0,0,1<<16,1<<16,18,16,2,1));cases.append(('destination-range',base[1],base[2],q,src,dst,3,dst))
fixture=w/'original-abi-request-fixtures.json'
if fixture.exists():
 original=json.loads(fixture.read_text());assert original['host_emulation'] and not original['device_execution']
 for row in original['rows']:
  assert row['result']==0 and len(row['captures'])==1
  cap=row['captures'][0];assert (cap['selector'],cap['size'])==(1,368)
  q=bytes.fromhex(cap['hex']);assert struct.unpack_from('<3Q',q,8)==(0x12340000,0x76540000,0x54670000)
  sr=struct.unpack_from('<4Q',q,40);dr=struct.unpack_from('<4I',q,88)
  rect=sr+dr if any(sr+dr) else None
  fs=bytes(rng.randrange(256) for _ in range(7+16*80+17));fd=bytes([0xa5])*(9+20*96+17)
  cases.append(('original-library-'+row['case'],desc(16,16,80,7),desc(20,20,96,9),q,fs,fd,0,oracle(16,16,80,7,20,20,96,9,fs,fd,rect,False)))
mutants=[('ignore-source-offset','source+s->offset+','source+0+'),('wrong-destination-stride','(uint64_t)(p->dy+y)*d->stride','(uint64_t)(p->dy+y)*s->stride'),('alpha-discard','for(unsigned ch=0;ch<4;ch++)','for(unsigned ch=0;ch<3;ch++)'),('no-format-check','!((im->format==KSC_BGRA && im->bpe==4) || (im->format==KPF_W40A && im->bpe==8)) || ','')]
with tempfile.TemporaryDirectory(prefix='ksc-') as tmp:
 t=Path(tmp);cpp=t/'test.cpp';cpp.write_text(prefix+core+harness);exe=t/'test'
 subprocess.run(['clang++','-I',str(w),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)],check=True)
 def check(executable,case):
  name,sd,dd,q,src,dst,err,want=case;(t/'in').write_bytes(sd+dd+q+src+dst)
  result=subprocess.run([str(executable),str(t/'in'),str(t/'out')],capture_output=True)
  if result.returncode:return False
  actual=(t/'out').read_bytes();return struct.unpack_from('<i',actual)[0]==err and actual[4:]==want
 for case in cases:
  if not check(exe,case):
   raw=(t/'out').read_bytes();print('FAILED',case[0],'result',struct.unpack_from('<i',raw)[0], 'differences',[(i,a,b) for i,(a,b) in enumerate(zip(raw[4:],case[-1])) if a!=b][:20]);raise AssertionError(case[0])
 receipts=[]
 for name,before,after in mutants:
  assert before in core
  cpp.write_text(prefix+core.replace(before,after)+harness)
  subprocess.run(['clang++','-I',str(w),'-std=c++17','-O1','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True,capture_output=True)
  detected=[case[0] for case in cases if not check(exe,case)]
  assert detected,name;receipts.append(dict(name=name,compiled=True,runtime_rejected_by=detected))
out=dict(passed=True,production_core_sha256=hashlib.sha256((w/'KebabScalerCore.h').read_bytes()).hexdigest(),cases=len(cases),all_channels_padding_and_untouched_bytes_checked=True,asan_ubsan=True,compiled_runtime_mutants=receipts,original_framework_request_fixtures=3 if fixture.exists() else 0,physical_test=False)
(w/'host-scaler-tests.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS',len(cases),'actual CPU core cases and',len(mutants),'compiled runtime-failing mutants')
