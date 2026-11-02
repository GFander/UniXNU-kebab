from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--receipt-dir',type=Path,required=True);options=parser.parse_args();options.receipt_dir.mkdir(parents=True,exist_ok=True);w=options.receipt_dir
from pathlib import Path
import hashlib,json,re,subprocess,tempfile
native=(options.source/'osfmk/console/video_console.c').read_text()
start=native.index('/* Keep the original Apple progress-meter shape')
art=native[start:native.index('\nvoid\nvc_progress_initialize(',start)].strip()+'\n'
asset=options.source/'osfmk/console/progress_meter_data.c';assert asset.exists()
shim=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define KEBAB 1
#define XNU_TARGET_OS_OSX 1
#define TRUE 1
#define FALSE 0
typedef bool boolean_t;typedef int DTEntry;
static struct {uintptr_t v_baseaddr;unsigned v_width,v_height,v_depth,v_rowbytes;} vinfo;
static int vc_uiscale=3,flushes=0,records=0,present=1;static unsigned length;static unsigned char property[2048];
static bool panicking;static unsigned panic_active(){return panicking;}
static void kebab_early_flush(){flushes++;}static int SecureDTLookupEntry(void *r,const char *name,DTEntry *out){*out=1;return present?0:1;}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **out,unsigned *size){assert(!strcmp(name,"hios-boot-logo"));*out=property;*size=length;return 0;}
#define kSuccess 0
static int barrier_count;
'''
portable=art.replace('__asm__ volatile("dsb sy" ::: "memory");','barrier_count++;')
main=r'''
int main(int argc,char **argv){int mode=argc>1?atoi(argv[1]):0;
 kebab_progress_art3x();assert(kebab_progress_art_ready);
 for(unsigned state=0;state<2;state++)for(unsigned y=0;y<54;y++){
  assert(kebab_progress_middle[state][y]==progressmeter_middle[1][state][y*2/3]);
  for(unsigned x=0;x<27;x++){unsigned from=(y*2/3)*18+x*2/3,to=y*27+x;assert(kebab_progress_left[state][to]==progressmeter_leftcap[1][state][from]);assert(kebab_progress_right[state][to]==progressmeter_rightcap[1][state][from]);}}
 assert(234*3==702 && 702<1080);kebab_progress_art3x();
 vinfo.v_width=1080;vinfo.v_height=2400;vinfo.v_depth=32;vinfo.v_rowbytes=4352;
 unsigned char *frame=malloc((size_t)vinfo.v_rowbytes*vinfo.v_height);memset(frame,0xa5,(size_t)vinfo.v_rowbytes*vinfo.v_height);vinfo.v_baseaddr=(uintptr_t)frame;
 uint32_t *h=(uint32_t *)property;h[0]=0x314c424b;h[1]=4;h[2]=3;h[3]=16;h[4]=15;h[5]=18;h[6]=1080;h[7]=2400;length=32+48;for(unsigned i=32;i<length;i++)property[i]=(unsigned char)(i+1);
 bool logo=true,clear=true;
 switch(mode){case 1:present=0;logo=false;break;case 2:length=31;logo=false;break;case 3:h[0]^=1;logo=false;break;case 4:h[4]=1079;logo=false;break;case 5:h[5]=2399;logo=false;break;case 6:h[3]=15;logo=false;break;case 7:h[6]=1081;logo=false;break;case 8:length--;logo=false;break;case 9:vinfo.v_depth=16;logo=clear=false;break;case 10:vinfo.v_rowbytes=1;logo=clear=false;break;case 11:h[1]=513;h[2]=1;h[3]=2052;length=32+2052;logo=false;break;case 12:panicking=true;logo=clear=false;break;case 13:vinfo.v_baseaddr++;logo=clear=false;break;case 14:vinfo.v_rowbytes=4351;logo=clear=false;break;}
 kebab_boot_graphics();assert(flushes==(int)clear && barrier_count==(int)clear);
 if(clear)for(unsigned y=0;y<2400;y++){
  for(unsigned x=0;x<1080;x++){
   uint32_t expected=0xff000000;if(logo && x>=15 && x<19 && y>=18 && y<21)memcpy(&expected,property+32+(y-18)*16+(x-15)*4,4);
   uint32_t value;memcpy(&value,frame+(size_t)y*4352+x*4,4);assert(value==expected);
  }for(unsigned x=4320;x<4352;x++)assert(frame[(size_t)y*4352+x]==0xa5);
 }
 else assert(frame[0]==0xa5);
 frame[0]=0x77;kebab_boot_graphics();assert(frame[0]==0x77);free(frame);return 0;
}
'''
# Case11 uses a separately bounded property buffer so malformed lengths stay a
# parser test; do not introduce an out-of-range write in the test itself.
shim=shim.replace('property[2048]','property[4096]')
mutants=[('wrong2x-source-stride','sy*kProgressBarCapWidth*2+x*2/3','sy*kProgressBarCapWidth*3+x*2/3'),('keep-debug-background','row[x]=0xff000000;','row[x]=row[x];'),('allow-bitmap-past-right-edge','h[1]<=h[6]-h[4]','h[1]<=1080'),('allow-wrong-canvas','h[6]==vinfo.v_width','h[6]>0')]
with tempfile.TemporaryDirectory(prefix='boot-art-') as tmp:
 t=Path(tmp);src=t/'test.c';exe=t/'test';code=shim+asset.read_text()+portable+main;src.write_text(code)
 command=['clang','-std=c11','-O1','-fsanitize=address,undefined',str(src),'-o',str(exe)];compiled=subprocess.run(command,capture_output=True)
 if compiled.returncode:print(compiled.stderr.decode());raise SystemExit(compiled.returncode)
 for mode in range(15):subprocess.run([str(exe),str(mode)],check=True,capture_output=True)
 receipts=[]
 for name,before,after in mutants:
  assert before in code;src.write_text(code.replace(before,after));subprocess.run(command,check=True,capture_output=True)
  rejected=[mode for mode in range(15) if subprocess.run([str(exe),str(mode)],capture_output=True).returncode!=0];assert rejected,name;receipts.append(dict(name=name,compiled=True,runtime_rejected_cases=rejected))
(w/'host-boot-art-tests.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256((options.source/'osfmk/console/video_console.c').read_bytes()).hexdigest(),cases=15,all_original_alpha_expansion_pixels_checked=True,private_logo_parser_synthetic_input_only=True,frame_padding_checked=True,asan_ubsan=True,compiled_runtime_mutants=receipts,ARM_barrier_and_DT_explicit_models=True,physical_drawing=False),indent=2)+'\n');print('PASS15 actual boot-art/bitmap bounds cases, all3x original alpha bytes and4 runtime mutants')
