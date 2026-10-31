#!/usr/bin/env python3
"""Exercise the real development nub's gating, lifetime and failure paths."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
name = 'kebab_publish_development_pmgr(IOService *platform)\n'
body = source.split(name, 1)[1].split('\n}\n', 1)[0] + '\n}\n'
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
static bool enabled, init_ok, attach_ok, provider_init, provider_attach;
static int allocation_mode, allocations, releases, attaches, detaches, publications;
static unsigned donor_size;
class OSObject {
public: virtual ~OSObject() = default;
    void release() { ++releases; }
};
class OSMetaClass {
public:
    unsigned getClassSize() const { return donor_size; }
    static OSObject *allocClassWithName(const char *);
};
static OSMetaClass donor_meta;
class IOService : public OSObject {
public:
    bool init() {
        if (provider_init) *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this)+0x88) = this;
        return init_ok;
    }
    const OSMetaClass *getMetaClass() const { return &donor_meta; }
    void setProperty(const char *, bool) {}
    bool attach(IOService *) {
        ++attaches;
        if (provider_attach) *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this)+0x88) = this;
        return attach_ok;
    }
    void detach(IOService *) { ++detaches; }
    void registerService() { ++publications; }
};
struct Donor : IOService { unsigned char padding[0x740]; };
static Donor nub;
static OSObject wrong;
static IOService platform;
OSObject *OSMetaClass::allocClassWithName(const char *name) {
    assert(!strcmp(name, "ApplePMGRNub")); ++allocations;
    return allocation_mode == 0 ? nullptr : allocation_mode == 1 ? &wrong : &nub;
}
static bool PE_parse_boot_argn(const char *name, void *out, unsigned size) {
    assert(!strcmp(name, "kebab_pmgr_nub") && size == 4);
    *static_cast<uint32_t *>(out) = enabled;
    return enabled;
}
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
#define OSSafeReleaseNULL(object) do { if (object) (object)->release(); (object)=nullptr; } while(0)
#define IOLog(...) ((void)0)
static IOService *kebab_development_pmgr;
static void reset() {
    enabled = false; init_ok = attach_ok = true; provider_init = provider_attach = false;
    allocation_mode = 2; donor_size = 0x740;
    allocations = releases = attaches = detaches = publications = 0;
    kebab_development_pmgr = nullptr;
    *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(&nub)+0x88) = nullptr;
}
'''
main = r'''
int main() {
    reset(); kebab_publish_development_pmgr(&platform); assert(allocations == 0);
    reset(); enabled = true; allocation_mode = 0;
    kebab_publish_development_pmgr(&platform); assert(publications == 0 && releases == 0);
    reset(); enabled = true; allocation_mode = 1;
    kebab_publish_development_pmgr(&platform); assert(publications == 0 && releases == 1);
    for (int failure = 0; failure < 5; ++failure) {
        reset(); enabled = true;
        if (failure == 0) init_ok = false;
        if (failure == 1) donor_size = 0x750;
        if (failure == 2) provider_init = true;
        if (failure == 3) attach_ok = false;
        if (failure == 4) provider_attach = true;
        kebab_publish_development_pmgr(&platform);
        assert(publications == 0 && releases == 1 && kebab_development_pmgr == nullptr);
        assert(detaches == (failure == 4));
    }
    reset(); enabled = true;
    kebab_publish_development_pmgr(&platform);
    assert(allocations == 1 && publications == 1 && attaches == 1 && releases == 0);
    assert(kebab_development_pmgr == &nub);
    kebab_publish_development_pmgr(&platform);
    assert(allocations == 1 && publications == 1);
    puts("development PMGR gate, donor ABI, failures, lifetime and single publication: PASS");
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='pmgr-nub-test-', dir=cache) as directory:
    cpp = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    cpp.write_text(prefix + '\nstatic void\n' + name + body + main)
    subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
