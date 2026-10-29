#!/usr/bin/env python3
"""Run the actual singleton publisher under failures and concurrent callers."""
from pathlib import Path
import subprocess
import sys
import tempfile

s=Path(sys.argv[1]).read_text()
def function(name):
    at=s.index('\n'+name+'(void)\n{')+1;start=s.rfind('\n',0,at-1)+1
    opening=s.index('{',at);end=opening+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]+'\n'
code=r'''
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#define IOLog(...) ((void)0)
#define kIOBlockStorageDeviceTypeKey "device-type"
#define kIOBlockStorageDeviceTypeGeneric "Generic"
static unsigned enabled=1,failure,live,publications,attached;
static std::mutex kebab_block_publish_lock;
static void lck_mtx_lock(std::mutex*p){p->lock();}
static void lck_mtx_unlock(std::mutex*p){p->unlock();}
static bool PE_parse_boot_argn(const char*,uint32_t*p,unsigned){*p=enabled;return true;}
static uint64_t kebab_ufs_partition_bytes(){return failure==1?0:241008750592ULL;}
static uint64_t kebab_ufs_block_size(){return 4096;}
[[maybe_unused]] static bool kebab_ufs_writable(){return false;}
struct IOService {static IOService*getServiceRoot(){static IOService root;return failure==2?nullptr:&root;}};
struct KebabBlockStorageDevice {
 bool isAttached=false;
 KebabBlockStorageDevice(){++live;}~KebabBlockStorageDevice(){if(isAttached)--attached;--live;}
 bool init(void*){return failure!=3;}void release(){delete this;}
 void setName(const char*){}void setProperty(const char*,const char*){}
 bool attach(IOService*){if(failure==4)return false;isAttached=true;++attached;return true;}
 bool start(IOService*){return failure!=5;}
 void detach(IOService*){assert(isAttached);isAttached=false;--attached;}
 void registerService(){std::this_thread::yield();++publications;}
};
static KebabBlockStorageDevice*kebab_block_device;
'''
code+=function('kebab_blockstorage_publish')+function('kebab_blockstorage_start')
code+=r'''
int main(){
 enabled=0;assert(kebab_blockstorage_start()==0&&!live&&!publications);enabled=1;
 for(failure=1;failure<=5;failure++){
  assert(kebab_blockstorage_start()!=0);assert(!live&&!attached&&!kebab_block_device&&!publications);
 }
 failure=0;std::vector<std::thread> callers;
 for(unsigned i=0;i<24;i++)callers.emplace_back([]{assert(kebab_blockstorage_start()==0);});
 for(auto&t:callers){t.join();}
 assert(live==1&&attached==1&&publications==1);
 kebab_block_device->detach(nullptr);kebab_block_device->release();kebab_block_device=nullptr;
 assert(!live&&!attached);puts("PASS: actual publisher rollback, disabled path and 24 concurrent callers publish exactly once");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-pthread','-Wall','-Wextra',
        '-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=20)
