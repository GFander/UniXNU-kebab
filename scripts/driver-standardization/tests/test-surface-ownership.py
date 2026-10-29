#!/usr/bin/env python3
"""Exercise actual Surface creator release and client-close bodies with live users."""
from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(sys.argv[1]).read_text()
def function(signature):
    begin=source.index(signature)
    begin=source.rfind('\n',0,begin-1)+1
    opening=source.index('{',begin);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]+'\n'
bodies='\n'.join(function(n) for n in (
    'kebab_surface_put(KebabSurface *s)',
    'kebab_surface_release_physical(KebabSurface *surface)',
    'IOSurfaceRootUserClient::closeConnection()'))
stub=r'''
#include <cassert>
#include <cstdint>
#include <mutex>
#include <future>
#include <thread>
#include <cstdio>
using IOLock=std::mutex;
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
class KebabUserClientCall {
    IOLock *lock;
public: explicit KebabUserClientCall(IOLock *p):lock(p){IOLockLock(lock);}
    ~KebabUserClientCall(){IOLockUnlock(lock);}
};
struct KebabSurface {unsigned id=1,refs=2;void release(){assert(refs);--refs;}};
static IOLock tableLock;
static IOLock *kebab_surface_lock=&tableLock;
static KebabSurface *kebab_surface_table[16];static uint32_t kebab_surface_users[16];
struct OSArray {unsigned flushes=0;void flushCollection(){++flushes;}};
struct IOSurfaceRootUserClient {
    IOLock *fCallLock=nullptr;OSArray *fRefs=nullptr;bool fClosed=false;
    void closeConnection();
};
'''
main=r'''
int main(){
    KebabSurface sole; kebab_surface_table[1]=&sole;kebab_surface_users[1]=1;
    kebab_surface_release_physical(&sole);
    assert(sole.refs==0&&kebab_surface_table[1]==nullptr&&kebab_surface_users[1]==0);
    KebabSurface shared;shared.refs=3;
    kebab_surface_table[1]=&shared;kebab_surface_users[1]=2;
    kebab_surface_release_physical(&shared);
    assert(shared.refs==2&&kebab_surface_table[1]==&shared&&kebab_surface_users[1]==1);
    kebab_surface_put(&shared);shared.release();
    assert(shared.refs==0&&kebab_surface_table[1]==nullptr);
    kebab_surface_release_physical(nullptr);
    IOLock calls;OSArray refs;IOSurfaceRootUserClient client;client.fCallLock=&calls;client.fRefs=&refs;
    std::promise<void> active,release,closed;auto go=release.get_future();
    std::thread call([&]{KebabUserClientCall held(&calls);active.set_value();go.wait();assert(refs.flushes==0);});
    active.get_future().wait();
    std::thread close([&]{client.closeConnection();closed.set_value();});auto done=closed.get_future();
    assert(done.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    release.set_value();done.wait();call.join();close.join();
    assert(client.fClosed&&refs.flushes==1);client.closeConnection();assert(refs.flushes==1);
    IOSurfaceRootUserClient failed;failed.closeConnection();
    puts("Creator/table ownership, surviving client, serialized and idempotent close: PASS");
}
'''
cache=Path.home()/'.cache';cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='surface-ownership-',dir=cache) as temp:
    cpp,binary=Path(temp)/'test.cpp',Path(temp)/'test'
    cpp.write_text(stub+'\n'+bodies+'\n'+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
