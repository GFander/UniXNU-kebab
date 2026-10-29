#!/usr/bin/env python3
"""Run the shared production validator and lock guard with real concurrent calls."""
from pathlib import Path
import subprocess
import sys
import tempfile

header = Path(sys.argv[1]).read_text()
cache=Path.home()/'.cache'
cache.mkdir(exist_ok=True)
stub=r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <mutex>
#include <thread>
#include <future>
using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnUnsupported=2;
constexpr unsigned kIOExternalMethodScalarInputCountMax=16,kIOExternalMethodScalarOutputCountMax=16;
struct IOMemoryDescriptor { uint64_t length; uint64_t getLength() const {return length;} };
struct IOExternalMethodArguments {
    const uint64_t *scalarInput=nullptr; uint32_t scalarInputCount=0;
    uint64_t *scalarOutput=nullptr; uint32_t scalarOutputCount=0;
    const void *structureInput=nullptr; uint32_t structureInputSize=0;
    void *structureOutput=nullptr; uint32_t structureOutputSize=0;
    IOMemoryDescriptor *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
};
using IOLock=std::mutex;
inline void IOLockLock(IOLock *l){l->lock();}
inline void IOLockUnlock(IOLock *l){l->unlock();}
'''
main=r'''
int main(){
    KebabUserClientMethod methods[]={{8,0,2},{12,1,0},{74,0,0}};
    uint64_t in[16]={},out[16]={};
    IOExternalMethodArguments a; a.scalarInput=in;a.scalarOutput=out;a.scalarOutputCount=2;
    assert(kebab_user_client_validate(8,&a,methods,3)==0);
    a.scalarInputCount=16;a.scalarOutputCount=16;
    assert(kebab_user_client_validate(8,&a,methods,3)==0); // Existing padded callers.
    a.scalarInputCount=0;a.scalarOutputCount=1;
    assert(kebab_user_client_validate(8,&a,methods,3)==1);
    assert(kebab_user_client_validate(12,&a,methods,3)==1);
    assert(kebab_user_client_validate(999,&a,methods,3)==2);
    assert(kebab_user_client_validate(8,nullptr,methods,3)==1);
    a.scalarOutputCount=2;a.scalarInputCount=17;
    assert(kebab_user_client_validate(8,&a,methods,3)==1);
    a.scalarInputCount=0;a.scalarOutput=nullptr;
    assert(kebab_user_client_validate(8,&a,methods,3)==1);
    a.scalarOutput=out;a.structureInputSize=1;
    assert(kebab_user_client_validate(8,&a,methods,3)==1);
    IOMemoryDescriptor md{4096};a.structureInputDescriptor=&md;
    assert(kebab_user_client_validate(8,&a,methods,3)==0);
    md.length=1ULL<<32;
    assert(kebab_user_client_validate(8,&a,methods,3)==1); // No 64->32 truncation.
    a.structureInputDescriptor=nullptr;a.structureInputSize=0;
    a.structureOutput=&md;a.structureOutputSize=1;
    assert(kebab_user_client_validate(74,&a,methods,3)==0); // Partial ABI reply remains valid.
    IOLock lock;std::promise<void> entered,release,closed;
    auto allow=release.get_future();
    std::thread method([&]{KebabUserClientCall call(&lock);entered.set_value();allow.wait();});
    entered.get_future().wait();
    std::thread close([&]{KebabUserClientCall call(&lock);closed.set_value();});
    auto done=closed.get_future();
    assert(done.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    release.set_value();done.wait();method.join();close.join();
    puts("Valid/padded/partial ABI calls, malformed input, truncation and close serialization: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='client-contract-',dir=cache) as temp:
    folder=Path(temp)
    (folder/'IOKit').mkdir()
    (folder/'stub.h').write_text(stub)
    for name in ('IOUserClient.h','IOMemoryDescriptor.h','IOLocks.h'):
        (folder/'IOKit'/name).write_text('#include "../stub.h"\n')
    (folder/'KebabUserClientContract.h').write_text(header)
    (folder/'test.cpp').write_text('#include "KebabUserClientContract.h"\n'+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I',str(folder),
                    str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)
