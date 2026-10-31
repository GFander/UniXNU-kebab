"""Execute the actual publisher and vnode qualification with concurrent callers."""
from pathlib import Path
import re
import subprocess
import tempfile

r=Path(__file__).parent;s=(r/'after/bsd/dev/kebab_keys.c').read_text()
def function(name):
    at=s.index('\n'+name+'(');start=s.rfind('\n',0,at)+1
    return s[start:s.index('\n}\n',at)+3]
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
struct {bool polling,dev_ready;int cmajor;}kk;
struct kk_record{char bytes[24];};
static int kebab_keys_cdevsw;
static std::mutex kk_publish_lock;
static void lck_mtx_lock(std::mutex*p){p->lock();}
static void lck_mtx_unlock(std::mutex*p){p->unlock();}
static int add,removeSlot,nodes,lookups,putCount,mode;
static int cdevsw_add(int n,int*p){assert(n==-1&&p==&kebab_keys_cdevsw);add++;return mode==1 ? -1:9;}
static void cdevsw_remove(int n,int*p){assert(n==9&&p==&kebab_keys_cdevsw);removeSlot++;}
static unsigned makedev(int major,int minor){return unsigned(major*256+minor);}
enum {DEVFS_CHAR=1,UID_ROOT=0,GID_OPERATOR=5,VCHR=4};
static void*devfs_make_node(unsigned dev,int type,int uid,int gid,int permissions,const char*name){
 assert(dev==makedev(9,0)&&type==DEVFS_CHAR&&uid==UID_ROOT&&gid==GID_OPERATOR&&permissions==0440&&!strcmp(name,"kebab_keys"));
 nodes++;return mode==2?nullptr:(void*)1;
}
struct vnode{};using vnode_t=vnode*;
static int vfs_context_kernel(){return 17;}
static int vnode_lookup(const char*path,int flags,vnode_t*p,int context){
 assert(!strcmp(path,"/dev/kebab_keys")&&!flags&&context==17);lookups++;
 static vnode node;if(mode==3)return 2;*p=&node;return 0;
}
static int vnode_vtype(vnode_t){return mode==4 ? 0:VCHR;}
static unsigned vnode_specrdev(vnode_t){return makedev(mode==5 ? 8:9,0);}
static void vnode_put(vnode_t p){assert(p);putCount++;}
#define KK_QUEUE_LEN 64
'''
main=r'''
int main(){
 kebab_keys_publish();assert(!add&&!nodes);assert(kebab_keys_node_selftest()&&!lookups);
 kk.polling=true;mode=1;kebab_keys_publish();assert(add==1&&!nodes&&!kk.dev_ready);
 mode=2;kebab_keys_publish();assert(add==2&&nodes==1&&removeSlot==1&&kk.cmajor==-1&&!kk.dev_ready);
 mode=0;std::vector<std::thread>threads;
 for(int i=0;i<24;i++)threads.emplace_back(kebab_keys_publish);
 for(auto&t:threads)t.join();
 assert(add==3&&nodes==2&&removeSlot==1&&kk.dev_ready&&kk.cmajor==9);
 assert(kebab_keys_node_selftest()&&lookups==1&&putCount==1);
 for(mode=3;mode<6;mode++)assert(!kebab_keys_node_selftest());
 assert(lookups==4&&putCount==3);
 putCount=0;mode=0;kk.dev_ready=false;assert(!kebab_keys_node_selftest()&&putCount==1);
 printf("actual keys publisher: disabled, allocation rollback/retry, 24 concurrent calls, vnode type/dev and iocount balance PASS\n");
}
'''
# Check the actual normal hook occurs after devfs mount and before PID1.
init=(r/'after/bsd/kern/bsd_init.c').read_text()
assert init.index('devfs_kernel_mount(')<init.index('kebab_keys_publish();')<init.index('load_init_program(p);')
start=function('kebab_keys_start')
assert 'kk_dev_register();' not in start and 'kk.polling = true;' in start
assert start.index('kernel_thread_start(')<start.index('kk.polling = true;')
old=(r/'before/bsd/dev/kebab_keys.c').read_text()
for name in ['kk_sample','kk_thread','kkq_push','kkq_pop','kebab_keys_read']:
    if '\n'+name+'(' not in old:continue
    at=old.index('\n'+name+'(');end=old.index('\n}\n',at)+3
    assert old[at:end]==s[s.index('\n'+name+'('):s.index('\n}\n',s.index('\n'+name+'('))+3],name
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('')
    p.write_text(prefix+'\n'.join(function(n) for n in ['kk_dev_register','kebab_keys_publish','kebab_keys_node_selftest'])+main)
    subprocess.run(['clang++','-std=c++17','-pthread','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

