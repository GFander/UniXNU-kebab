#!/usr/bin/env python3
"""Exercise the actual KEBAB root branch for legacy, media and UUID modes."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('\t/* ufsmedia_root=2 initializes the physical provider')
branch = source[start:source.index('\n#endif', start)]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
struct Dictionary {};
struct IOService {};
static Dictionary chosen;
static IOService media;
static unsigned mode;
static int init_error, media_error, initialized, releases, media_lookups;
#define IOLog(...) ((void)0)
#define major(d) ((d)>>8)
#define minor(d) ((d)&255)
#define OSSafeReleaseNULL(p) do { if (p) ++releases; (p)=nullptr; } while (0)
[[noreturn]] static void panic(const char *, ...) { throw 1; }
static int kebab_ufs_rootdev(int *dev) { ++initialized; *dev=3<<8; return init_error; }
static int kebab_blockstorage_rootdev(int *dev, char *name, size_t size, IOService **service) {
    ++media_lookups;
    if (media_error) return media_error;
    *dev=1<<8; snprintf(name,size,"disk0"); *service=&media; return 0;
}
static bool PE_parse_boot_argn(const char *key, void *out, unsigned size) {
    assert(!strcmp(key,"ufsmedia_root") && size==sizeof(mode));
    *static_cast<uint32_t *>(out)=mode; return mode!=0;
}
static size_t fixture_strlcpy(char *out,const char *in,size_t n) {
    size_t length=strlen(in); if (n) { size_t copy=length<n-1?length:n-1;
        memcpy(out,in,copy);out[copy]=0; } return length;
}
#define strlcpy fixture_strlcpy
static int run(bool with_matching) {
    char rdBootVar[32]="ufsroot0", rootName[32]="unset";
    unsigned rootNameSize=sizeof(rootName), flags=999, *oflags=&flags;
    int value=0, *root=&value;
    Dictionary *matching=with_matching?&chosen:nullptr;
    IOService *service=nullptr;
'''
suffix = r'''
    assert(mode==2 && matching==&chosen && rdBootVar[0]==0);
    assert(service==nullptr && releases==0 && flags==999);
    return 2;
iofrootx:
    assert(mode!=2 && matching==nullptr && flags==0);
    assert(!strcmp(rootName,mode==1?"disk0":"ufsroot0"));
    assert(value==(mode==1?1:3)<<8);
    assert(service==(mode==1?&media:nullptr));
    return 1;
}
int main() {
    for (unsigned value=0;value<3;++value) {
        mode=value; initialized=releases=media_lookups=0;
        init_error=media_error=0;
        assert(run(true)==(mode==2?2:1));
        assert(initialized==1 && media_lookups==(mode==1) && releases==(mode!=2));
    }
    mode=2; initialized=releases=media_lookups=0;
    bool refused=false;
    try { run(false); } catch(int) { refused=true; }
    assert(refused && releases==0 && initialized==1);
    mode=2; init_error=5; refused=false;
    try { run(true); } catch(int) { refused=true; }
    assert(refused);
    puts("legacy disk, whole-media diagnostic, preserved UUID matcher and refusals: PASS");
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='root-routing-', dir=cache) as folder:
    cpp = Path(folder) / 'test.cpp'
    binary = Path(folder) / 'test'
    cpp.write_text(prefix + branch + suffix)
    subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
