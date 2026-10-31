"""Execute production wrappers concurrently; panic core must progress with lock held."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

r=Path(sys.argv[1]);old=(r/'before/bsd/dev/kebab_spmi.c').read_text();s=(r/'after/bsd/dev/kebab_spmi.c').read_text()
def fn(source,name):
    m=re.search(r'^(?:static )?(?:int|void|bool|uint32_t|const char \*)\n'+name+r'\([^;{]*\)\n\{',source,re.M)
    assert m,name
    return source[m.start():source.index('\n}',m.end())+2].removeprefix('static ')
names=['kebab_spmi_keys_sample','kebab_spmi_keys_config','kebab_spmi_report',
       'kebab_spmi_pon_report','kebab_spmi_power_report','kebab_spmi_vrm_boot_power']
for name in names:
    assert fn(old,name).replace(name,name+'_unserialized',1)==fn(s,name+'_unserialized')
assert fn(old,'kebab_spmi_pon_reason_write')==fn(s,'kebab_spmi_pon_reason_write')
core=fn(s,'kebab_spmi_pon_reason_write')
assert not re.search(r'\b(lck_|IOLock|IOService|malloc|IOMalloc)',core)
constants='\n'.join(re.findall(r'^#define KEBAB_SPMI_PON_\w+[^\n]*',s,re.M))
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include <future>
#include <atomic>
#define printf(...) ((void)0)
static std::mutex kebab_spmi_normal_lock;
static thread_local bool held,panicCall;
static std::atomic<unsigned> active,visits;
static bool fail;
static uint8_t spare;
static void lck_mtx_lock(std::mutex*p){assert(!held);p->lock();held=true;}
static void lck_mtx_unlock(std::mutex*p){assert(held);held=false;p->unlock();}
static int transfer(){
 assert(held);assert(active.fetch_add(1)==0);
 for(int i=0;i<20;i++)std::this_thread::yield();
 ++visits;assert(active.fetch_sub(1)==1);return fail?-1:0;
}
static int kebab_spmi_keys_sample_unserialized(uint8_t raw[4]){int rc=transfer();memset(raw,1,4);return rc;}
static int kebab_spmi_keys_config_unserialized(uint8_t a[3],uint8_t b[3]){memset(a,1,3);memset(b,1,3);return transfer();}
static void kebab_spmi_report_unserialized(){transfer();}
static void kebab_spmi_pon_report_unserialized(){transfer();}
static void kebab_spmi_power_report_unserialized(){transfer();}
static int kebab_spmi_vrm_boot_power_unserialized(){return transfer();}
static bool kebab_spmi_pon_writes_enabled(){assert(held||panicCall);return true;}
static int kebab_spmi_pon_apid(uint32_t*p){*p=13;return 0;}
static int kebab_spmi_pon_read(uint32_t p,uint32_t addr,uint8_t*v){assert(p==13&&addr==0x88f);*v=spare;return 0;}
static int kebab_spmi_rw_write(uint32_t p,uint32_t sid,uint32_t addr,uint8_t v){assert(p==13&&sid==0&&addr==0x88f);spare=v;return 0;}
static void delay(unsigned n){assert(n<=1000);}
'''
code+=constants+'\n'+core+'\n'+'\n'.join(fn(s,n) for n in names+['kebab_spmi_pon_reason_write_normal'])
code+=r'''
int main(){
 std::vector<std::thread> threads;
 for(unsigned i=0;i<12;i++)threads.emplace_back([]{
  for(unsigned j=0;j<80;j++){
   uint8_t raw[4],a[3],b[3];
   assert(kebab_spmi_keys_sample(raw)==0&&raw[0]==1);
   assert(kebab_spmi_keys_config(a,b)==0&&a[0]==1&&b[0]==1);
   kebab_spmi_report();kebab_spmi_pon_report();kebab_spmi_power_report();
   assert(kebab_spmi_vrm_boot_power()==0);
   assert(kebab_spmi_pon_reason_write_normal()==0);
  }
 });
 for(auto&t:threads){t.join();}
 assert(visits==12*80*6&&!active);
 fail=true;uint8_t raw[4];assert(kebab_spmi_keys_sample(raw)==-1&&!held);
 assert(kebab_spmi_normal_lock.try_lock());kebab_spmi_normal_lock.unlock();
 // Simulate the interrupted owner retaining the normal mutex. The original
 // PON core must still execute without it, just as the panic caller does.
 kebab_spmi_normal_lock.lock();
 auto panic=std::async(std::launch::async,[]{panicCall=true;return kebab_spmi_pon_reason_write();});
 assert(panic.wait_for(std::chrono::seconds(1))==std::future_status::ready);
 assert(panic.get()==0&&(spare&0xfe)==4);kebab_spmi_normal_lock.unlock();
 puts("PASS: production wrappers serialize 12 callers, release on errors; unchanged panic PON completes while normal lock held");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-pthread','-Wall','-Wextra','-Wno-unused-variable',
        '-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=20)
