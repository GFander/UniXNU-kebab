"""Reproduce full-ring self-overwrite, then test actual snapshot code with sink feedback."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).parent
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean_t;
#define TRUE 1
#define FALSE 0
static int irq_state=1,irq_calls,copy_active,is_panic,reentry,reentered,flush_calls,transport_overflow;
static size_t pending_bytes;
static char output[1024*1024];static size_t outlen;
void kebab_klog_put(const char *,size_t);
void kebab_fwlog_report(uint32_t);
static unsigned panic_active(void){return is_panic;}
static void kebab_usb_log_flush(void){assert(!copy_active);pending_bytes=0;flush_calls++;}
static boolean_t ml_set_interrupts_enabled(boolean_t on){int old=irq_state;irq_state=on;copy_active=(++irq_calls)&1;return old;}
static int test_printf(const char *,...) __attribute__((format(printf,1,2)));
static int test_printf(const char *fmt,...) {
 assert(!copy_active);
 char text[4096];va_list a;va_start(a,fmt);int n=vsnprintf(text,sizeof(text),fmt,a);va_end(a);
 assert(n>=0&&n<(int)sizeof(text)&&outlen+(size_t)n<sizeof(output));
 memcpy(output+outlen,text,n);outlen+=n;output[outlen]=0;kebab_klog_put(text,n);
 pending_bytes+=(size_t)n;if(pending_bytes>=16384)transport_overflow=1;if(is_panic)pending_bytes=0;
 if(reentry&&!reentered){reentered=1;int oldpanic=is_panic,oldirq=irq_state;if(reentry==2){is_panic=1;irq_state=0;}kebab_fwlog_report(32768);is_panic=oldpanic;irq_state=oldirq;}
 return n;
}
#define printf test_printf
'''
main=r'''
static char input[65536*2+137],expected[65536];
static int check(unsigned size,unsigned limit,int panic,int initial_irq,int nested) {
 memset(kebab_klog,0,sizeof(kebab_klog));kebab_klog_head=0;kebab_klog_ready=TRUE;
 outlen=0;output[0]=0;irq_calls=copy_active=reentered=flush_calls=transport_overflow=0;pending_bytes=0;irq_state=initial_irq;is_panic=panic;reentry=nested;
 for(unsigned i=0;i<size;i++)input[i]=i%73==72?'\n':(char)('A'+i%26);
 kebab_klog_put(input,size);unsigned avail=size>65536?65536:size,want=limit&&limit<avail?limit:avail;
 memcpy(expected,input+size-want,want);kebab_fwlog_report(limit);
 assert(irq_state==initial_irq&&!copy_active);
 if(nested==0&&irq_calls){assert(!transport_overflow);assert(panic?flush_calls==0:flush_calls>0);}
 if(nested==2){assert(strstr(output,"---- begin ----")&&strstr(output,"---- end ----")&&!strstr(output,"already active"));return 0;}
 if(!size){assert(strstr(output,"---- empty ----"));return 0;}
 char *begin=strstr(output,"KEBAB FWLOG: ---- begin ----\n");assert(begin);begin+=strlen("KEBAB FWLOG: ---- begin ----\n");
 char *end=strstr(begin,"\nKEBAB FWLOG: ---- end ----\n");assert(end);
 if((size_t)(end-begin)!=want||memcmp(begin,expected,want)){fputs("SELF_OVERWRITE: full FWLOG output corrupted the unread ring\n",stderr);return 4;}
 if(nested==1)assert(strstr(output,"already active"));
 return 0;
}
int main(int argc,char **argv) {
 (void)argv;
 if(argc>1)return check(65536+5000,0,0,1,0);
 unsigned sizes[]={0,31,65536,65536*2+137,65536*2+137,31};
 unsigned limits[]={0,0,0,0,32768,65536};
 for(int panic=0;panic<2;panic++)for(int irq=0;irq<2;irq++)for(unsigned i=0;i<6;i++)assert(!check(sizes[i],limits[i],panic,irq,0));
 assert(!check(65536+5000,0,0,1,1));assert(!check(65536+5000,0,1,0,1));
 assert(!check(65536+5000,0,0,1,2));
 assert(!kebab_klog_dumps[0].busy&&!kebab_klog_dumps[1].busy);
 puts("PASS: 24 exact snapshot/limit/IRQ cases, 2 reentry cases and panic interrupting normal dump");
}
'''
with tempfile.TemporaryDirectory() as t:
    p=Path(t)
    def build(source,name,old=False):
        s=source.read_text();body=s[s.index('#define KEBAB_KLOG_SIZE'):]
        tail=main if not old else main[:main.index('int main(')]+'''int main(void){return check(65536+5000,0,0,1,0);}\n'''
        c=p/(name+'.c');c.write_text('#define KEBAB 1\n'+prefix+body+tail)
        subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(c),'-o',str(p/name)],check=True)
        return subprocess.run([str(p/name)],text=True,capture_output=True)
    old=build(r/'before/bsd/dev/kebab_fwlog.c','old',True)
    assert old.returncode==4 and 'SELF_OVERWRITE' in old.stderr,(old.returncode,old.stdout,old.stderr)
    intermediate=build(r/'transport-before/bsd/dev/kebab_fwlog.c','without-drain',True)
    assert intermediate.returncode and 'transport_overflow' in intermediate.stderr,(intermediate.returncode,intermediate.stderr)
    new=build(Path(sys.argv[1]) if len(sys.argv)>1 else r/'after/bsd/dev/kebab_fwlog.c','new')
    assert new.returncode==0,(new.returncode,new.stdout,new.stderr)
    print('Old self-overwrite and snapshot-only TX overflow reproduced; '+new.stdout.strip())
