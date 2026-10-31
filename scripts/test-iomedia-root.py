#!/usr/bin/env python3
"""Run the actual IOMedia root-selection helper against registry fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
name = 'kebab_blockstorage_rootdev(dev_t *dev, char *name, size_t name_size, IOService **service)\n'
body = source.split(name, 1)[1].split('\n}\n', 1)[0] + '\n}\n'
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <map>
#include <string>
#include <vector>
#include <sys/types.h>
#define makedev(a,b) ((dev_t)(((uint32_t)(a)<<24)|((uint32_t)(b)&0xffffff)))
#define major(d) (((uint32_t)(d)>>24)&0xff)
#define minor(d) ((uint32_t)(d)&0xffffff)
#define kSecondScale 1000000000ULL
#define kIOReturnSuccess 0
#define kIORegistryIterateRecursively 1
#define kIOBSDNameKey "BSD Name"
#define kIOBSDMajorKey "BSD Major"
#define kIOBSDMinorKey "BSD Minor"
#define IOLog(...) ((void)0)
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
typedef int IOReturn;
static void *gIOServicePlane;
static int quiet_result, retains, iterator_releases;
static bool fail_iterator;
class OSObject { public: virtual ~OSObject() = default; };
class OSBoolean : public OSObject {
    bool value;
public: explicit OSBoolean(bool v) : value(v) {} bool isTrue() const { return value; }
};
class OSNumber : public OSObject {
    uint64_t value;
public: explicit OSNumber(uint64_t v) : value(v) {}
    uint64_t unsigned64BitValue() const { return value; }
    uint32_t unsigned32BitValue() const { return (uint32_t)value; }
};
class OSString : public OSObject {
    std::string value;
public: explicit OSString(const char *v) : value(v) {}
    size_t getLength() const { return value.size(); }
    const char *getCStringNoCopy() const { return value.c_str(); }
};
struct Meta { const char *getClassName() const { return "fixture"; } };
class IORegistryEntry : public OSObject {
public:
    bool media = true;
    std::map<std::string, OSObject *> properties;
    const char *getName() const { return "fixture"; }
    const Meta *getMetaClass() const { static Meta m; return &m; }
    OSObject *metaCast(const char *n) { assert(!strcmp(n,"IOMedia")); return media ? this : nullptr; }
    OSObject *getProperty(const char *n) { return properties[n]; }
};
class IOService : public IORegistryEntry {
public:
    IOReturn waitQuiet(uint64_t timeout) { assert(timeout == 5000000000ULL); return quiet_result; }
    void retain() { ++retains; }
};
static IOService device, whole_media;
static IOService *kebab_block_device;
static std::vector<IORegistryEntry *> children;
class IORegistryIterator {
    size_t index = 0;
public:
    static IORegistryIterator *iterateOver(IORegistryEntry *root, void *plane, unsigned options) {
        assert(root == kebab_block_device && plane == gIOServicePlane && options == 1);
        return fail_iterator ? nullptr : new IORegistryIterator;
    }
    IORegistryEntry *getNextObject() { return index < children.size() ? children[index++] : nullptr; }
    void release() { ++iterator_releases; delete this; }
};
static uint64_t kebab_ufs_partition_bytes() { return 1048576; }
#define strlcpy fixture_strlcpy
static size_t strlcpy(char *out, const char *in, size_t size) {
    size_t length = strlen(in); if (size) { size_t n = length < size-1 ? length : size-1;
        memcpy(out,in,n); out[n]=0; } return length;
}
static OSBoolean yes(true), no(false);
static OSNumber bytes(1048576), wrong_bytes(1044480), maj(6), min(1), invalid_major(0x100);
static OSString disk("disk0"), empty("");
static void reset() {
    kebab_block_device = &device; fail_iterator = false; quiet_result = 0;
    retains = iterator_releases = 0; children = {&whole_media}; whole_media.media = true;
    whole_media.properties = {{"Whole",&yes},{"Size",&bytes},{kIOBSDNameKey,&disk},
        {kIOBSDMajorKey,&maj},{kIOBSDMinorKey,&min}};
}
'''
main = r'''
int main() {
    for (int scenario = 0; scenario < 10; ++scenario) {
        reset();
        if (scenario == 0) kebab_block_device = nullptr;
        if (scenario == 1) quiet_result = -1;
        if (scenario == 2) fail_iterator = true;
        if (scenario == 3) children.clear();
        if (scenario == 4) whole_media.media = false;
        if (scenario == 5) whole_media.properties["Whole"] = &no;
        if (scenario == 6) whole_media.properties["Size"] = &wrong_bytes;
        if (scenario == 7) whole_media.properties[kIOBSDNameKey] = nullptr;
        if (scenario == 8) whole_media.properties[kIOBSDNameKey] = &empty;
        if (scenario == 9) whole_media.properties[kIOBSDMajorKey] = &invalid_major;
        char name[32] = "unchanged"; dev_t dev = 123; IOService *service = &device;
        assert(kebab_blockstorage_rootdev(&dev,name,sizeof(name),&service) != 0);
        assert(dev == 123 && !strcmp(name,"unchanged") && service == &device && retains == 0);
    }
    reset(); char name[32] = {}; dev_t dev = 0; IOService *service = nullptr;
    assert(kebab_blockstorage_rootdev(&dev,name,sizeof(name),&service) == 0);
    assert(major(dev)==6 && minor(dev)==1 && !strcmp(name,"disk0"));
    assert(service==&whole_media && retains==1 && iterator_releases==1);
    reset(); char short_name[3] = "no";
    assert(kebab_blockstorage_rootdev(&dev,short_name,sizeof(short_name),&service) == EINVAL);
    assert(!strcmp(short_name,"no") && retains==0);
    puts("IOMedia subtree, geometry, whole media, BSD identity, failure and lifetime fixtures: PASS");
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='iomedia-root-', dir=cache) as folder:
    cpp = Path(folder) / 'test.cpp'
    binary = Path(folder) / 'test'
    cpp.write_text(prefix + '\nint\n' + name + body + main)
    subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
