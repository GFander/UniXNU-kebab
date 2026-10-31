"""Run the actual registry report under allocation/no-match/invalid/large sets."""
from pathlib import Path
import subprocess
import tempfile

r=Path(__file__).parent
s=(r/'after/iokit/Kernel/arm/KebabDriverDiagnostics.cpp').read_text()
s=s[s.index('struct KebabDriverManifestEntry'):s.index('#endif /* KEBAB */')]
prefix=r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using uint64_t=unsigned long long;
static int live, mode, returned;
static std::string output;
static void IOLog(const char *fmt,...) {
    char text[1024]; va_list args; va_start(args,fmt); vsnprintf(text,sizeof(text),fmt,args); va_end(args); output+=text;
}
struct OSObject { OSObject(){live++;} virtual ~OSObject(){live--;} void release(){delete this;} };
struct IORegistryEntry: OSObject { const char *getName(){return "test-provider";} };
struct OSDictionary: OSObject {};
struct OSIterator;
struct Class {const char *getClassName(){return "test-class";}};
struct IOService:OSObject {
    static OSDictionary *serviceMatching(const char *) { return mode==1 ? nullptr : new OSDictionary; }
    static OSIterator *getMatchingServices(OSDictionary *);
    IORegistryEntry *copyParentEntry(const char *){ return mode==4 ? nullptr : new IORegistryEntry; }
    Class *getMetaClass(){static Class c;return &c;}
    uint64_t getRegistryEntryID(){return 1;}
    unsigned getState(){return 6;}
    bool isInactive(){return mode==5;}
};
struct OSIterator:OSObject {
    IOService service;
    unsigned i=0;
    OSObject *getNextObject(){if(i++<(mode==6 ? 20u:1u)){returned++; return &service;} return nullptr;}
    bool isValid(){return mode!=3;}
};
OSIterator *IOService::getMatchingServices(OSDictionary *){return mode==2 ? nullptr:new OSIterator;}
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
#define OSSafeReleaseNULL(object) do {if(object) object->release();object=nullptr;}while(0)
static const char *gIOServicePlane="IOService";
'''
manifest=(r/'after/iokit/Kernel/arm/KebabDriverManifest.h').read_text()
main=r'''
int main(){
 for(mode=0;mode<7;mode++){
   output.clear();returned=0;kebab_driver_report();assert(live==0);
   assert(output.find("usb backend=C service=none")!=std::string::npos);
   assert(output.find("touch backend=C service=none")!=std::string::npos);
   if(mode==1) assert(output.find("allocation-failed")!=std::string::npos);
   if(mode==2) assert(output.find("query=unavailable")!=std::string::npos);
   if(mode==3) assert(output.find("valid=0")!=std::string::npos);
   if(mode==4) assert(output.find("provider=none")!=std::string::npos);
   if(mode==5) assert(output.find("inactive=1")!=std::string::npos);
   if(mode==6) {assert(returned==9*8);assert(output.find("matched=8+")!=std::string::npos);}
 }
 puts("production DRV report: allocation, missing result, invalid iterator, detached provider, inactive and cap8; all references balanced PASS");
}
'''
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.cpp';exe=p.with_suffix('')
    p.write_text(prefix+manifest+'\n'+s+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
