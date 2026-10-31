#!/usr/bin/env python3
"""Execute actual bridge methods against bounded descriptors and a faulting backend."""
from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(sys.argv[1]).read_text()
def body(name):
    start=source.index('KebabBlockStorageDevice::'+name+'(')
    start=source.rfind('\n',0,start-1)+1
    opening=source.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>
#include <future>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cerrno>
#include <cstdio>
using UInt64=uint64_t;using UInt32=uint32_t;using IOByteCount=uint64_t;
using IOReturn=int;using IOStorageSynchronizeOptions=int;
constexpr int kIOReturnSuccess=0,kIOReturnNotWritable=1,kIOReturnNoDevice=2,
 kIOReturnBadArgument=3,kIOReturnBusy=4,kIOReturnIOError=5,kIOReturnError=6,
 kIOReturnTimeout=7,kIOReturnUnderrun=8,kIOReturnOverrun=9;
constexpr int kIODirectionIn=1,kIODirectionOut=2,kIODirectionInOut=3;
#define KEBAB_BLOCK_CHUNK (64*1024)
#define PAGE_SIZE 16384
#define IOLog(...) ((void)0)
struct OSDictionary {};struct IOStorageAttributes {};
static bool superFail,allocFail,lockFail,writable;
static int allocations,locks,superStops,rawReads,rawWrites,syncs,ioError,errorAt;
static uint64_t blockSize=4096,mediaBytes=4096ULL*4096;
static std::promise<void> *entered,*proceed;
struct IOService{};
struct IOBlockStorageDevice {
 bool init(OSDictionary*){return !superFail;}bool start(IOService*){return !superFail;}
 void stop(IOService*){++superStops;}void free(){}
};
struct IOLock {std::mutex m;};
static IOLock *IOLockAlloc(){if(lockFail)return nullptr;++locks;return new IOLock;}
static void IOLockFree(IOLock*p){--locks;delete p;}
static void IOLockLock(IOLock*p){p->m.lock();}
static void IOLockUnlock(IOLock*p){p->m.unlock();}
static void *IOMallocAligned(size_t n,size_t){if(allocFail)return nullptr;++allocations;return new char[n];}
static void IOFreeAligned(void*p,size_t){--allocations;delete[] (char*)p;}
static uint64_t kebab_ufs_block_size(){return blockSize;}
static uint64_t kebab_ufs_partition_bytes(){return mediaBytes;}
static bool kebab_ufs_writable(){return writable;}
static int kebab_ufs_sync(){++syncs;return ioError;}
static int io(){
 if(entered){auto e=entered;entered=nullptr;e->set_value();proceed->get_future().wait();}
 return errorAt && rawReads+rawWrites==errorAt ? ioError:0;
}
static int kebab_ufs_raw_read(uint64_t,void*p,uint32_t n){++rawReads;int e=io();if(!e)memset(p,0x5a,n);return e;}
static int kebab_ufs_raw_write(uint64_t,const void*,uint32_t){++rawWrites;return io();}
struct IOMemoryDescriptor {
 std::vector<uint8_t> bytes;int direction;bool shortCopy=false;
 IOMemoryDescriptor(size_t n,int d):bytes(n),direction(d){}
 int getDirection(){return direction;}uint64_t getLength(){return bytes.size();}
 size_t readBytes(uint64_t off,void*p,size_t n){n=std::min(n,bytes.size()-off);if(shortCopy)--n;memcpy(p,bytes.data()+off,n);return n;}
 size_t writeBytes(uint64_t off,const void*p,size_t n){n=std::min(n,bytes.size()-off);if(shortCopy)--n;memcpy(bytes.data()+off,p,n);return n;}
};
struct IOStorageCompletion {void*target;void(*action)(void*,void*,IOReturn,UInt64);void*parameter;};
struct KebabBlockStorageDevice:IOBlockStorageDevice {
 UInt64 fBlockSize=0,fBlockCount=0;bool fWritable=false,fMediaStateReported=false,fOnline=false;
 void*fBounce=nullptr;IOLock*fLock=nullptr;
 static IOReturn statusForError(int);
 bool init(OSDictionary*);bool start(IOService*);void stop(IOService*);void free();
 IOReturn doAsyncReadWrite(IOMemoryDescriptor*,UInt64,UInt64,IOStorageAttributes*,IOStorageCompletion*);
 IOReturn doSynchronize(UInt64,UInt64,IOStorageSynchronizeOptions);
 IOReturn reportMediaState(bool*,bool* = nullptr);IOReturn requestIdle();
};
struct Result {KebabBlockStorageDevice*d;unsigned calls=0;IOReturn status=-1;UInt64 done=0;};
static void complete(void*t,void*,IOReturn s,UInt64 n){
 auto*r=(Result*)t;++r->calls;r->status=s;r->done=n;
 bool present;r->d->reportMediaState(&present); // deadlocks if called while lock held
}
'''
code+='\n'.join(body(n) for n in ('statusForError','init','start','stop','free',
    'doAsyncReadWrite','doSynchronize','reportMediaState','requestIdle'))
code+=r'''
int main(){
 // Partial init allocation failures balance in free().
 for(int failure=0;failure<5;failure++){
  superFail=failure==0;allocFail=failure==1;lockFail=failure==2;
  blockSize=failure==3?131072:4096;mediaBytes=failure==4?4097:4096ULL*4096;
  KebabBlockStorageDevice d;assert(!d.init(nullptr));d.free();assert(!allocations&&!locks);
 }
 superFail=allocFail=lockFail=false;blockSize=4096;mediaBytes=4096ULL*4096;
 writable=true;KebabBlockStorageDevice d;assert(d.init(nullptr));assert(d.start(nullptr));
 Result r{&d};IOStorageCompletion c{&r,complete,nullptr};
 IOMemoryDescriptor padded(70000,kIODirectionIn);
 assert(d.doAsyncReadWrite(&padded,0,16,nullptr,&c)==0);assert(r.calls==1&&r.done==65536&&r.status==0&&rawReads==1);
 // A descriptor that covers the first 64K but not the full request must never
 // partially write the medium before being rejected.
 padded.direction=kIODirectionOut;
 assert(d.doAsyncReadWrite(&padded,0,32,nullptr,&c)==kIOReturnBadArgument);
 assert(rawWrites==0&&r.calls==1);
 assert(d.doAsyncReadWrite(&padded,4095,2,nullptr,&c)==kIOReturnBadArgument);
 assert(d.doAsyncReadWrite(&padded,UINT64_MAX,1,nullptr,&c)==kIOReturnBadArgument);
 assert(d.doAsyncReadWrite(&padded,0,0,nullptr,&c)==kIOReturnBadArgument);
 padded.direction=kIODirectionInOut;
 assert(d.doAsyncReadWrite(&padded,0,1,nullptr,&c)==kIOReturnBadArgument);
 assert(r.calls==1);
 // Error on chunk 2: callback once with the committed byte count and timeout.
 IOMemoryDescriptor large(131072,kIODirectionIn);errorAt=rawReads+2;ioError=ETIMEDOUT;
 assert(d.doAsyncReadWrite(&large,0,32,nullptr,&c)==0);
 assert(r.calls==2&&r.done==65536&&r.status==kIOReturnTimeout);
 errorAt=rawReads+1;assert(d.doAsyncReadWrite(&large,0,1,nullptr,nullptr)==kIOReturnTimeout);
 errorAt=0;padded.direction=kIODirectionIn;padded.shortCopy=true;
 assert(d.doAsyncReadWrite(&padded,0,1,nullptr,&c)==0);
 assert(r.calls==3&&r.status==kIOReturnOverrun&&r.done==0);
 padded.shortCopy=false;
 // Stop waits for a real in-flight request and prevents any later backend I/O.
 std::promise<void> began,allow;entered=&began;proceed=&allow;
 auto transfer=std::async(std::launch::async,[&]{return d.doAsyncReadWrite(&padded,0,1,nullptr,&c);});
 began.get_future().wait();
 auto stop=std::async(std::launch::async,[&]{d.stop(nullptr);});
 assert(stop.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout);
 allow.set_value();assert(transfer.get()==0);stop.get();int reads=rawReads;
 assert(d.doAsyncReadWrite(&padded,0,1,nullptr,&c)==kIOReturnNoDevice&&reads==rawReads&&r.calls==4);
 bool present=true,changed=false;assert(d.reportMediaState(&present,&changed)==0&&!present&&changed);
 assert(d.doSynchronize(0,0,0)==kIOReturnNoDevice&&d.requestIdle()==kIOReturnNoDevice);
 d.free();assert(!allocations&&!locks);
 writable=false;KebabBlockStorageDevice ro;assert(ro.init(nullptr)&&ro.start(nullptr));
 padded.direction=kIODirectionOut;r.d=&ro;
 assert(ro.doAsyncReadWrite(&padded,0,1,nullptr,&c)==kIOReturnNotWritable&&rawWrites==0&&r.calls==4);
 assert(ro.doSynchronize(0,0,0)==0&&syncs==0);ro.stop(nullptr);ro.free();
 assert(!allocations&&!locks);
 puts("PASS: actual bridge allocation, boundaries, partial completion, timeout, reentry, concurrent stop and read-only contracts");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-pthread','-O1','-g','-Wall','-Wextra',
        '-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=20)
