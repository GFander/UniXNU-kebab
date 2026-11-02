from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--receipt-dir',type=Path,required=True);options=parser.parse_args();options.receipt_dir.mkdir(parents=True,exist_ok=True);w=options.receipt_dir
from pathlib import Path
import hashlib,json,subprocess,tempfile
p=options.source/'osfmk/console/video_console.c';s=p.read_text();a=s.index('int\nkebab_boot_logo_matches_framebuffer(');b=s.index('\nstatic void\nkebab_boot_graphics(',a);body=s[a:b]
keys=(options.source/'bsd/dev/kebab_keys.c').read_text();ka=keys.index('static uint32_t kk_boot_keys_ready');kb=keys.index('/* ---------------------------------------------------------------- record */',ka);keybody=keys[ka:kb]
shim=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int DTEntry;enum{kSuccess=0,KEBAB_KEYS_KEY_POWER=0};
typedef struct{unsigned key,pressed;}kebab_keys_event;
#define os_atomic_load(p,o) (*(p))
#define os_atomic_store(p,v,o) (*(p)=(v))
static int panicking,present=1;static uint8_t asset[4096];static unsigned length;
static int panic_active(){return panicking;}
static int SecureDTLookupEntry(void*r,const char*n,DTEntry*out){*out=1;return present?0:1;}
static int SecureDTGetProperty(DTEntry d,const char*n,const void **p,unsigned *s){*p=asset;*s=length;return 0;}
'''
main=r'''
int main(int argc,char **argv){int mode=argc>1?atoi(argv[1]):0;const unsigned stride=4352;uint8_t *raw=malloc((size_t)stride*2400);memset(raw,0xa5,(size_t)stride*2400);
 for(unsigned y=0;y<2400;y++)for(unsigned x=0;x<1080;x++)((uint32_t*)(raw+(size_t)y*stride))[x]=0xff000000;
 uint32_t *h=(uint32_t*)asset;h[0]=0x314c424b;h[1]=4;h[2]=3;h[3]=16;h[4]=443;h[5]=1025;h[6]=1080;h[7]=2400;length=80;
 for(unsigned i=32;i<length;i++)asset[i]=(uint8_t)i;
 for(unsigned y=0;y<3;y++)memcpy(raw+(size_t)(1025+y)*stride+443*4,asset+32+y*16,16);
 uintptr_t base=(uintptr_t)raw;unsigned rowbytes=stride;int expect=0;
 switch(mode){case 0:expect=1;break;case 1:raw[(size_t)1900*stride+20]=1;break;case 2:raw[(size_t)1026*stride+444*4]^=1;break;case 3:present=0;break;case 4:h[0]^=1;break;case 5:h[6]++;break;case 6:length--;break;case 7:h[3]--;break;case 8:h[4]=1079;break;case 9:base++;break;case 10:rowbytes=1;break;case 11:panicking=1;break;case 12:memset(asset+32,0,48);for(unsigned y=0;y<3;y++)memset(raw+(size_t)(1025+y)*stride+443*4,0,16);break;case 13:for(unsigned y=0;y<2400;y++)for(unsigned x=0;x<1080;x++)raw[(size_t)y*stride+x*4+3]^=0x5a;expect=1;break;case 14:rowbytes=4351;break;}
 assert(kebab_boot_logo_matches_framebuffer(base,1080,2400,rowbytes)==expect);
 assert(!kebab_keys_boot_logo_idle_allowed());kk_boot_keys_ready=1;assert(kebab_keys_boot_logo_idle_allowed());kebab_keys_event e={1,1};kk_note_boot_power(&e);assert(kebab_keys_boot_logo_idle_allowed());e.key=0;e.pressed=0;kk_note_boot_power(&e);assert(kebab_keys_boot_logo_idle_allowed());e.pressed=1;kk_note_boot_power(&e);assert(!kebab_keys_boot_logo_idle_allowed());kk_boot_keys_ready=0;assert(!kebab_keys_boot_logo_idle_allowed());kk_boot_keys_ready=1;assert(!kebab_keys_boot_logo_idle_allowed());free(raw);return 0;}
'''
code=shim+body+keybody+main
mutants=[('retain-private-pixel-outside-logo','if ((row[x] & 0xffffff)', 'if (x >= h[4] && x - h[4] < h[1] && y >= h[5] && y - h[5] < h[2] && (row[x] & 0xffffff)'),('accept-empty-branding','return nonblack && !panic_active();','return !panic_active();'),('ignore-power-press','os_atomic_store(&kk_boot_power_seen, 1u, release);','os_atomic_store(&kk_boot_power_seen, 0u, release);')]
with tempfile.TemporaryDirectory(prefix='boot-owner-') as tmp:
 t=Path(tmp);src=t/'test.c';exe=t/'test';command=['clang','-std=c11','-O1','-fsanitize=address,undefined',str(src),'-o',str(exe)]
 def compile(text):
  src.write_text(text);p=subprocess.run(command,capture_output=True,text=True);assert p.returncode==0,p.stderr
 compile(code)
 for mode in range(15):subprocess.run([str(exe),str(mode)],check=True,capture_output=True)
 receipts=[]
 for name,before,after in mutants:
  assert before in code;compile(code.replace(before,after));fails=[i for i in range(15) if subprocess.run([str(exe),str(i)],capture_output=True).returncode!=0];assert fails,name;receipts.append(dict(name=name,compiled=True,runtime_rejected_cases=fails))
(w/'host-logo-match-tests.json').write_text(json.dumps(dict(passed=True,actual_source_matcher_and_key_functions=True,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),key_source_sha256=hashlib.sha256((options.source/'bsd/dev/kebab_keys.c').read_bytes()).hexdigest(),cases=15,synthetic_logo_only=True,atomic_and_DT_are_models=True,asan_ubsan=True,compiled_runtime_mutants=receipts,physical_power_key_regression_pending=True),indent=2)+'\n');print('PASS15 actual strict-logo/padding/privacy/panic/key-eligibility cases;3 compiled runtime mutants')
