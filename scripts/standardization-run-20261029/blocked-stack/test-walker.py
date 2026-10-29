"""Run the actual stopped-thread frame walker with guarded no-fault copies."""
from pathlib import Path
import subprocess
import tempfile

r=Path(__file__).parent;s=(r/'after/osfmk/kern/kebab_bootdiag.c').read_text()
at=s.index('static unsigned\nkebab_blocked_kernel_trace');fn=s[at:s.index('\n}\n',at)+3]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
using vm_offset_t=uintptr_t;
using vm_size_t=size_t;
struct arm_kernel_saved_state_t{uint64_t fp,lr;};
struct thread_kernel_state{struct {arm_kernel_saved_state_t ss;} machine;};
struct thread{uintptr_t kernel_stack;unsigned state;void *continuation;};using thread_t=thread*;
static uintptr_t kernel_stack_size=1024;
static alignas(16) unsigned char storage[1024];
static bool lockHeld=true;
static unsigned copies,failCopy;
enum{TH_WAIT=1,TH_RUN=4};
#define thread_get_kernel_state(t) ((thread_kernel_state*)((t)->kernel_stack+kernel_stack_size-sizeof(thread_kernel_state)))
static size_t ml_nofault_copy(uintptr_t source,uintptr_t destination,size_t size){
 assert(lockHeld);copies++;if(copies==failCopy)return 0;
 assert(source>=(uintptr_t)storage&&source+size<=(uintptr_t)storage+sizeof(storage));
 memcpy((void*)destination,(void*)source,size);return size;
}
static thread t;
static void reset(){
 memset(storage,0,sizeof(storage));kernel_stack_size=sizeof(storage);
 t={(uintptr_t)storage,TH_WAIT,nullptr};copies=failCopy=0;
 auto *saved=&thread_get_kernel_state(&t)->machine.ss;
 saved->lr=10;saved->fp=(uintptr_t)storage+32;
}
'''.replace('static alignas(16)','alignas(16) static')
main=r'''
int main(){
 uint64_t frames[16];
 reset();auto*saved=&thread_get_kernel_state(&t)->machine.ss;
 for(unsigned i=0;i<20;i++){auto *pair=(uint64_t*)(storage+32+i*16);pair[0]=(uintptr_t)pair+16;pair[1]=20+i;}
 assert(kebab_blocked_kernel_trace(&t,frames)==16&&frames[0]==10&&frames[15]==34&&copies==16);
 reset();saved=&thread_get_kernel_state(&t)->machine.ss;
 auto*pair=(uint64_t*)(storage+32);pair[0]=(uintptr_t)pair;pair[1]=20;
 assert(kebab_blocked_kernel_trace(&t,frames)==2&&copies==2);
 reset();saved=&thread_get_kernel_state(&t)->machine.ss;saved->fp=(uintptr_t)storage+17;
 assert(kebab_blocked_kernel_trace(&t,frames)==1&&copies==1);
 reset();saved=&thread_get_kernel_state(&t)->machine.ss;saved->fp=(uintptr_t)storage+sizeof(storage)-8;
 assert(kebab_blocked_kernel_trace(&t,frames)==1&&copies==1);
 reset();failCopy=1;assert(kebab_blocked_kernel_trace(&t,frames)==0);
 reset();failCopy=2;assert(kebab_blocked_kernel_trace(&t,frames)==1);
 for(unsigned mode=0;mode<5;mode++){
   reset();if(mode==0)t.state=TH_WAIT|TH_RUN;if(mode==1)t.continuation=(void*)1;
   if(mode==2)t.kernel_stack=0;if(mode==3)kernel_stack_size=1;
   if(mode==4)t.kernel_stack=std::numeric_limits<uintptr_t>::max()-16;
   assert(kebab_blocked_kernel_trace(&t,frames)==0&&copies==0);
 }
 puts("actual stopped-stack walker: cap16, cycles, alignment, stack bounds, failed/partial reads, running/continuation/empty/overflow skip PASS");
}
'''
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('');p.write_text(prefix+fn+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
