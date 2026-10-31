"""Run actual normal entry wrappers and frame boundary under concurrent callers."""
from pathlib import Path
import re
import subprocess
import tempfile

r=Path(__file__).parent
touch=(r/'after/bsd/dev/kebab_touch.c').read_text();bus=(r/'after/bsd/dev/kebab_geni.c').read_text()
def function(s,name):
    at=s.index('\n'+name+'(');start=s.rfind('\n',0,at)+1
    return s[start:s.index('\n}\n',at)+3]
names=['kebab_touch_probe','kebab_touch_init_now','kebab_touch_start','kebab_touch_se','kebab_touch_ping','kebab_touch_clock_enable']
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#define __unused
using wait_result_t=int;
static std::mutex wire;
static thread_local bool locked;
static std::atomic<unsigned>calls,errors;
static void kebab_geni_transaction_lock(){wire.lock();assert(!locked);locked=true;}
static void kebab_geni_transaction_unlock(){assert(locked);locked=false;wire.unlock();}
static int transaction(){assert(locked);calls++;std::this_thread::yield();return -1;}
static void kebab_geni_sleep_ms(unsigned n){assert(!locked&&(n==100||n==5));}
static uint32_t kebab_geni_record_poll_error(){assert(locked);return ++errors;}
static int kt_poll_once(){assert(locked);return transaction();}
'''
code=prefix
for name in names:
    result='int' if name=='kebab_touch_start' else 'void'
    code+='static '+result+' '+name+'_locked(){'+('return ' if result=='int' else '')+'transaction();}\n'
for name in names:code+=function(touch if name in names[:3] else bus,name)
frame=function(touch,'kt_thread').replace('for (;;) {','for (unsigned frame=0; frame<2; frame++) {').replace('static void','static void',1)
assert 'kebab_geni_transaction_lock();' in frame and 'kebab_geni_transaction_unlock();' in frame
code+=frame
code+=r'''
int main(){
 std::vector<std::thread> threads;
 for(int i=0;i<12;i++)threads.emplace_back([]{
   kebab_touch_probe();kebab_touch_init_now();assert(kebab_touch_start()==-1);
   kebab_touch_se();kebab_touch_ping();kebab_touch_clock_enable();kt_thread(nullptr,0);
 });
 for(auto&t:threads)t.join();assert(calls==12*8&&errors==24);
 assert(wire.try_lock());wire.unlock();
 puts("actual normal wrappers: 12 concurrent protocol/console callers, error return unlock, complete frame boundary; sleep outside lock PASS");
}
'''
assert 'kebab_touch_clock_enable_locked();' in bus
assert '\t\tkebab_touch_probe_locked();' in touch
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('');p.write_text(code)
    subprocess.run(['clang++','-std=c++17','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
