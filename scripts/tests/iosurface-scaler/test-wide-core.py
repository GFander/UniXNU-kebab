from pathlib import Path
import csv,hashlib,json,math,subprocess
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=Path(__file__).resolve().parent/'synthetic-xr-fixtures';candidate=args.source/'iokit/Kernel/arm';out=args.receipt_dir;out.mkdir(parents=True,exist_ok=True)
prefix='''#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <vector>
#include "KebabScalerCore.h"
'''
harness=r'''
int main(){
 for(unsigned code=0;code<1024;code++){
  uint8_t p[8]={};for(unsigned ch=0;ch<4;ch++){uint16_t word=(ch==3?894:384)<<6;p[ch*2]=word;p[ch*2+1]=word>>8;}
  uint16_t word=code<<6;p[0]=word;p[1]=word>>8;
  printf("D,%u,%d,%u\n",code,kpf_read(p,KPF_W40A,0),kpf_gop_pixel(p));
  uint8_t target[8]={};kpf_write(target,KPF_W40A,0,kpf_read(p,KPF_W40A,0));assert((kpf_word(target)>>6)==code);
 }
 for(unsigned v=0;v<256;v++){uint8_t p[8]={};kpf_write(p,KPF_W40A,0,kpf_div_round((int64_t)v*65536,255));printf("E,%u,%u\n",v,kpf_word(p));}
 for(unsigned code=0;code<1024;code++){
  uint8_t px[8]={0};uint16_t word=code<<6;px[6]=word;px[7]=word>>8;ksc_image im={1,1,8,KPF_W40A,0,8,0,8,1,1,8};
  bool opaque=ksc_source_opaque(&im,px);assert(opaque==(code>=894));int32_t alpha=kpf_read(px,KPF_W40A,3);if(code==639)assert(alpha==32768);uint8_t canonical[8]={};kpf_write(canonical,KPF_W40A,3,alpha);
  printf("A,%u,%d,%u,%u\n",code,alpha,(unsigned)opaque,kpf_word(canonical+6)>>6);
 }
 for(unsigned sf=0;sf<2;sf++)for(unsigned df=0;df<2;df++)for(unsigned nearest=0;nearest<2;nearest++){
  ksc_image s={3,2,32,sf?KPF_W40A:KPF_BGRA,8,96,0,(uint16_t)(sf?8:4),1,1,96};
  ksc_image d={5,3,48,df?KPF_W40A:KPF_BGRA,8,176,0,(uint16_t)(df?8:4),1,1,176};
  std::vector<uint8_t>a(96,0xa5),b(176,0x69);for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++)for(unsigned ch=0;ch<4;ch++)kpf_write(a.data()+8+y*32+x*s.bpe,s.format,ch,ch==3?65536:((x+y+ch)%3)*32768);
  assert(ksc_extent(&s)==KSC_OK&&ksc_extent(&d)==KSC_OK&&ksc_source_opaque(&s,a.data()));
  uint8_t q[368]={};uint32_t ids[2]={2,3};memcpy(q,ids,8);uint64_t flags=0x3000|(nearest?0x80:0);memcpy(q+32,&flags,8);ksc_plan p={};assert(!ksc_prepare(q,368,&s,&d,&p));ksc_run(&p,&s,&d,a.data(),b.data());
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<5;x++){uint8_t*px=b.data()+8+y*48+x*d.bpe;assert(kpf_read(px,d.format,3)==65536);for(unsigned ch=0;ch<3;ch++)printf("P,%u,%u,%u,%u,%u,%u,%u\n",sf,df,nearest,y,x,ch,df?(kpf_word(px+ch*2)>>6):px[ch]);}
  for(unsigned i=0;i<176;i++){bool touched=false;for(unsigned y=0;y<3;y++)if(i>=8+y*48&&i<8+y*48+5*d.bpe)touched=true;if(!touched)assert(b[i]==0x69);}
  /* A constant fully opaque frame is preserved for all format/size/filter pairs. */
  for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++)for(unsigned ch=0;ch<4;ch++)kpf_write(a.data()+8+y*32+x*s.bpe,s.format,ch,ch==3?65536:(ch==0?65536:0));
  assert(ksc_source_opaque(&s,a.data()));ksc_run(&p,&s,&d,a.data(),b.data());
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<5;x++){uint8_t*px=b.data()+8+y*48+x*d.bpe;assert(kpf_read(px,d.format,0)==65536&&kpf_read(px,d.format,1)==0&&kpf_read(px,d.format,2)==0);}
  kpf_write(a.data()+8,s.format,3,32768);assert(!ksc_source_opaque(&s,a.data()));
  for(unsigned av=0;av<3;av++) {
    for(unsigned yy=0;yy<2;yy++)for(unsigned xx=0;xx<3;xx++)kpf_write(a.data()+8+yy*32+xx*s.bpe,s.format,3,av*32768);
    ksc_run(&p,&s,&d,a.data(),b.data());
    int32_t from_alpha=kpf_read(a.data()+8,s.format,3);
    for(unsigned yy=0;yy<3;yy++)for(unsigned xx=0;xx<5;xx++) {
      uint8_t*px=b.data()+8+yy*48+xx*d.bpe;assert(kpf_read(px,d.format,0)==65536&&kpf_read(px,d.format,1)==0&&kpf_read(px,d.format,2)==0);
      assert(abs(kpf_read(px,d.format,3)-from_alpha)<=129);
    }
  }
 }
 ksc_image valid={3,2,24,KPF_W40A,8,56,0,8,1,1,56};assert(!ksc_extent(&valid));
 for(unsigned fault=0;fault<8;fault++){ksc_image s=valid;if(fault==0)s.stride=23;if(fault==1)s.allocation=55;if(fault==2)s.memory_length=55;if(fault==3)s.offset=9;if(fault==4)s.bpe=4;if(fault==5)s.planes=1;if(fault==6)s.element_width=2;if(fault==7)s.format=0x26773461;assert(ksc_extent(&s)!=KSC_OK);}
 fprintf(stderr,"wide core integration complete\n");return 0;
}
'''
for name in ('KebabPixelFormat.h','KebabScalerCore.h'):(out/name).write_bytes((candidate/name).read_bytes())
(out/'actual.cpp').write_text(prefix+harness)
def execute(name):
 p=subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(out/(name+'.cpp')),'-o',str(out/name)],capture_output=True,text=True);assert not p.returncode,p.stderr
 p=subprocess.run([str(out/name)],capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr);return p
p=execute('actual');assert p.returncode==0,p.stdout+p.stderr
decode=[];encoded=[]
for line in p.stdout.splitlines():
 if line.startswith('D,'):_,code,v,gop=line.split(',');decode.append((int(code),int(v),int(gop)))
 if line.startswith('E,'):_,v,code=line.split(',');encoded.append((int(v),int(code)))
ref=list(csv.DictReader((w/'reference-ci-w40a-float.csv').open()));assert len(decode)==1024
maxerr=max(abs(q16/65536-float(ref[code]['B']))for code,q16,_ in decode);assert maxerr<1/16384+5e-9
ref8=list(csv.DictReader((w/'reference-ci-w40a-storage.csv').open()));decode8_errors=[abs((gop&255)-int(ref8[code]['B']))for code,_,gop in decode]
# CoreImage's software path quantizes through half precision before its final
# 8-bit rounding; compare within one destination code, while the float/Q16
# comparison above separately checks the underlying transfer function.
assert max(decode8_errors)<=1 and all((gop>>24)==255 and(gop&0x00ffff00)==0 for _,_,gop in decode)
alpharef=list(csv.DictReader((w/'reference-ci-w40a-alpha.csv').open()));alpha_checks=0
for line in p.stdout.splitlines():
 if not line.startswith('A,'):continue
 _,code,alpha,opaque,canonical=line.split(',');code,alpha,opaque,canonical=map(int,(code,alpha,opaque,canonical));assert abs(alpha/65536-float(alpharef[code]['A']))<1/65536;assert bool(opaque)==(float(alpharef[code]['A'])==1);assert 384<=canonical<=894;alpha_checks+=1
assert alpha_checks==1024
refenc=list(csv.DictReader((w/'reference-ci-w40a-roundtrip.csv').open()))[:256]
differences=[abs((word>>6)-(int(refenc[v]['word0'])>>6))for v,word in encoded];assert max(differences)<=1
from fractions import Fraction as F
def xr_decode(code):
 t=(code-384)/510;return (-1 if t<0 else 1)*(abs(t)/12.92 if abs(t)<=.04045 else((abs(t)+.055)/1.055)**2.4)
def xr_encode(value):
 sign=-1 if value<0 else 1;t=abs(value);encoded=sign*(12.92*t if t<=.0031308 else 1.055*t**(1/2.4)-.055);return max(0,min(1023,round(384+510*encoded)))
pixel_checks=0
for line in p.stdout.splitlines():
 if not line.startswith('P,'):continue
 sf,df,nearest,y,x,ch,got=map(int,line.split(',')[1:]);fx=max(F(0),min(F(2),F((2*x+1)*3,10)-F(1,2)));fy=max(F(0),min(F(1),F((2*y+1)*2,6)-F(1,2)))
 if nearest:fx=F(min(2,(fx+F(1,2)).numerator//(fx+F(1,2)).denominator));fy=F(min(1,(fy+F(1,2)).numerator//(fy+F(1,2)).denominator))
 x0=int(fx);y0=int(fy);wx=fx-x0;wy=fy-y0;value=0
 for xx,yy,weight in ((x0,y0,(1-wx)*(1-wy)),(min(2,x0+1),y0,wx*(1-wy)),(x0,min(1,y0+1),(1-wx)*wy),(min(2,x0+1),min(1,y0+1),wx*wy)):
  raw=((xx+yy+ch)%3)/2;sample=xr_decode(xr_encode(raw))if sf else round(raw*255)/255;value+=float(weight)*sample
 want=xr_encode(value)if df else round(max(0,min(1,value))*255);assert abs(got-want)<=1,(sf,df,nearest,y,x,ch,got,want);pixel_checks+=1
assert pixel_checks==8*5*3*3
mutants=[('wrong-xr-unity',',510)',',511)'),('wrong-channel-order','p+ch*2','p+(3-ch)*2'),('drop-opaque-check','(kpf_word(p+6)>>6)<894','false')]
results=[]
for name,before,after in mutants:
 target='KebabPixelFormat.h'if name!='drop-opaque-check'else'KebabScalerCore.h';original=(out/target).read_text();assert before in original
 (out/target).write_text(original.replace(before,after));(out/(name+'.cpp')).write_text(prefix+harness)
 result=execute(name);assert result.returncode!=0,(name,result.stdout[-100:]);results.append(dict(name=name,rejected=True));(out/target).write_text(original)
q=dict(passed=True,actual_decode_samples=1024,actual_alpha_cases=alpha_checks,actual_encode_samples=256,actual_scale_filter_format_pairs=8,independent_gradient_channel_checks=pixel_checks,malformed_extent_cases=8,nonopaque_observed=True,actual_transparent_format_pairs=8,all_padding_untouched=True,compiled_runtime_mutants=results,Apple_CoreImage_decode_maximum_error=maxerr,Apple_CoreImage_decode8_maximum_code_error=max(decode8_errors),Apple_CoreImage_encode_maximum_code_error=max(differences),source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in (candidate/'KebabPixelFormat.h',candidate/'KebabScalerCore.h')},host_models_not_device_proof=True)
(out/'wide-tests.json').write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
