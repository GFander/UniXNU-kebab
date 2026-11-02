from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--receipt-dir',type=Path,required=True);options=parser.parse_args();options.receipt_dir.mkdir(parents=True,exist_ok=True);w=options.receipt_dir
from pathlib import Path
import hashlib,json,re,subprocess,tempfile
p=options.source/'osfmk/arm64/kebab_early.c';raw=p.read_text();source=re.sub(r'^#include .*\n','',raw,flags=re.M);source=source.replace('__asm__ volatile("dsb sy" ::: "memory");','barriers++;')
shim=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define KEBAB 1
int kebab_console_muted=1,barriers=0;unsigned char iso_font[256*16];
static char output[32768];static size_t used;
static int mock_printf(const char *fmt,...){va_list a;va_start(a,fmt);int n=vsnprintf(output+used,sizeof(output)-used,fmt,a);va_end(a);assert(n>=0 && (size_t)n<sizeof(output)-used);used+=n;return n;}
static void _doprnt(const char *fmt,va_list *a,void (*putc)(char),int radix){char buf[128];vsnprintf(buf,sizeof(buf),fmt,*a);for(char *p=buf;*p;p++)putc(*p);}
#define printf mock_printf
void kebab_early_mark(unsigned,const char *);void kebab_early_putc(char);void kebab_early_flush(void);
'''
main=r'''
static void panic_message(const char *fmt,...){va_list a;va_start(a,fmt);kebab_early_panic(fmt,&a);va_end(a);}
int main(){uint32_t *pixels=calloc(1080*2400,4);assert(pixels);early_fb=pixels;memset(iso_font,0xff,sizeof(iso_font));
 kebab_early_mark(1,"first");kebab_early_mark(2,"second");kebab_early_putc('Q');assert(early_record_count==2);for(unsigned i=0;i<1080*2400;i++)assert(!pixels[i]);
 kebab_early_flush();assert(strstr(output,"KEBAB EARLY 1: first") && strstr(output,"KEBAB EARLY 2: second"));size_t n=used;kebab_early_flush();assert(used==n);
 for(unsigned i=0;i<80;i++)kebab_early_record(3,"bounded");assert(early_record_count==64 && early_record_dropped==18);
 char longlabel[160];memset(longlabel,'L',159);longlabel[159]=0;early_record_count=0;kebab_early_record(9,longlabel);assert(strlen(early_records[0].label)==79);
 panic_message("fault %d",42);assert(early_panicking);bool white=false;for(unsigned i=0;i<1080*2400;i++)if(pixels[i]==0xffffffff)white=true;assert(white);
 kebab_early_disable();pixels[0]=0x55;kebab_early_putc('X');assert(pixels[0]==0x55);free(pixels);return 0;}
'''
shim=shim.replace('#include <stdint.h>','#include <stdint.h>\n#include <stdbool.h>')
mutants=[('normal-early-screen-leak','early_disabled || (kebab_console_muted && !early_panicking)','early_disabled'),('hide-early-panic','early_disabled || (kebab_console_muted && !early_panicking)','early_disabled || kebab_console_muted'),('omit-USB-replay','for(unsigned i=0;i<early_record_count;i++)printf','for(unsigned i=early_record_count;i<early_record_count;i++)printf')]
with tempfile.TemporaryDirectory(prefix='early-') as tmp:
 t=Path(tmp);cpp=t/'test.c';exe=t/'test';code=shim+source+main;cpp.write_text(code);cmd=['clang','-std=c11','-O1','-fsanitize=address,undefined',str(cpp),'-o',str(exe)]
 result=subprocess.run(cmd,capture_output=True)
 if result.returncode:print(result.stderr.decode());raise SystemExit(result.returncode)
 subprocess.run([str(exe)],check=True,capture_output=True);receipts=[]
 for name,before,after in mutants:
  assert before in code;cpp.write_text(code.replace(before,after));subprocess.run(cmd,check=True,capture_output=True);result=subprocess.run([str(exe)],capture_output=True);assert result.returncode!=0,name;receipts.append(dict(name=name,compiled=True,runtime_return=result.returncode))
(w/'host-early-tests.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),actual_early_mark_putc_panic_record_flush_bodies=True,bounded_replay_and_truncation_tested=True,asan_ubsan=True,compiled_runtime_mutants=receipts,ARM_barrier_doprnt_and_framebuffer_explicit_models=True,physical_panic_not_induced=True),indent=2)+'\n');print('PASS actual early-screen suppression, USB replay, bounded records and panic override;3 compiled runtime mutants')
