"""Exercise production query/context logic with hostile SMCCC return registers."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).parent
s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'kebab_scm_probe.c').read_text();s=s[s.index('struct kebab_scm_regs'):s.rindex('#endif')]
a=s.index('/* SMCCC 1.0');b=s.index('/* Bound interrupted-call')
s=s[:a]+'static void kebab_scm_raw(struct kebab_scm_regs *r);\n'+s[b:]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
static int enabled, panicking, irq, preempt, interrupts=1;
static int mode, calls, resumes, convention, queries;
static uint64_t cookie;
static char output[8192];
static int outlen;
static int test_printf(const char *fmt,...) { va_list a;va_start(a,fmt);int n=vsnprintf(output+outlen,sizeof(output)-outlen,fmt,a);va_end(a);outlen+=n;assert(outlen<(int)sizeof(output));return n; }
#define printf test_printf
static int PE_parse_boot_argn(const char *key,void *p,unsigned size) { assert(!strcmp(key,"kebab_scm_probe")&&size==sizeof(int));*(int*)p=enabled;return enabled!=0; }
static unsigned panic_active(void){return panicking;}
static bool ml_at_interrupt_context(void){return irq;}
static int get_preemption_level(void){return preempt;}
static bool ml_get_interrupts_enabled(void){return interrupts;}
'''
tests=r'''
static void kebab_scm_raw(struct kebab_scm_regs *r) {
 calls++;
 if(r->x[0]==0x80000000) { for(int i=1;i<8;i++)assert(!r->x[i]);r->x[0]=0x10001;return; }
 assert(r->x[0]==0xc2000601||r->x[0]==0x82000601||r->x[0]==1);
 assert(r->x[1]==1);
 assert(r->x[2]==0x02000601||r->x[2]==0x02001003||r->x[2]==0x02001004||r->x[2]==0x32000603);
 assert(!r->x[3]&&!r->x[4]&&!r->x[5]&&!r->x[7]);
 if(r->x[0]==1) {assert(r->x[6]==cookie);resumes++;} else {assert(!r->x[6]);queries++;}
 uint64_t fn=r->x[0],target=r->x[2];
 for(int i=0;i<8;i++)r->x[i]=0xabc000+i;
 if(mode==1) {r->x[0]=(uint32_t)-4;return;}
 if(mode==2) {r->x[0]=(uint32_t)-12;return;}
 if(mode==3&&(calls<=2)) {r->x[0]=1;r->x[6]=cookie=0x123400+calls;return;}
 if(mode==4) {r->x[0]=1;r->x[6]=cookie=0x567800+calls;return;}
 r->x[0]=0;
 r->x[1]=(convention==32&&fn==0xc2000601)?0:1;
 if(mode==5&&target==0x02001003)r->x[0]=(uint32_t)-4;
}
static void reset(void){enabled=1;panicking=irq=preempt=mode=calls=resumes=queries=convention=outlen=0;interrupts=1;cookie=0;output[0]=0;}
int main(void) {
 struct kebab_scm_result r;
 reset();r=kebab_scm_query(0xc2000601,0x02000601);assert(r.status==0&&r.value==1&&r.attempts==1&&calls==1);
 reset();mode=1;r=kebab_scm_query(0xc2000601,0x02000601);assert(r.status==-4&&calls==1);
 reset();mode=2;r=kebab_scm_query(0xc2000601,0x02000601);assert(r.status==-12&&calls==1);
 reset();mode=3;r=kebab_scm_query(0xc2000601,0x02000601);assert(r.status==0&&r.attempts==3&&resumes==2);
 reset();mode=4;r=kebab_scm_query(0xc2000601,0x02000601);assert(r.status==1&&r.attempts==32&&calls==32&&resumes==31);
 reset();enabled=0;kebab_scm_probe();assert(!calls&&!outlen);
 reset();enabled=2;kebab_scm_probe();assert(!calls&&!outlen);
 reset();panicking=1;kebab_scm_probe();assert(!calls&&strstr(output,"refused"));
 reset();irq=1;kebab_scm_probe();assert(!calls&&strstr(output,"refused"));
 reset();preempt=1;kebab_scm_probe();assert(!calls&&strstr(output,"refused"));
 reset();interrupts=0;kebab_scm_probe();assert(!calls&&strstr(output,"refused"));
 reset();kebab_scm_probe();assert(calls==5&&queries==4&&strstr(output,"survey end"));
 reset();convention=32;kebab_scm_probe();assert(calls==6&&queries==5&&strstr(output,"convention32")&&strstr(output,"survey end"));
 reset();mode=1;kebab_scm_probe();assert(calls==3&&strstr(output,"no supported query")&&!strstr(output,"survey end"));
 reset();mode=5;kebab_scm_probe();assert(calls==3&&strstr(output,"query failure")&&!strstr(output,"survey end"));
 puts("PASS: 15 production query/context cases; GPR reset, x6 resumes, cap32, signed errors, fallback, fixed queries");
}
'''
with tempfile.TemporaryDirectory() as t:
    p=Path(t);(p/'test.c').write_text(prefix+s+tests)
    subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
