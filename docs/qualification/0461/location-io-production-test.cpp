#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
struct process { char p_comm[17]="locationd"; char name[33]="locationd"; unsigned uid=0; uint8_t uuid[16]={0x72,0x36,0xf5,0x20,0xb3,0x31,0x38,0x3c,0x8b,0x98,0x5c,0x4e,0x2c,0xc5,0x9c,0x06}; };
using proc_t=process*;using proc_name_t=char[33];using user_addr_t=uintptr_t;
#define PROC_NULL nullptr
process own,other; proc_t current=&own;
bool panicking=false,irq=false,interrupts=true; int preemption=0,locks=0,refs=0,procReads=0,fdReads=0,putCalls=0,copies=0;
bool panic_active(){return panicking;} bool ml_at_interrupt_context(){return irq;}
int get_preemption_level(){return preemption;}bool ml_get_interrupts_enabled(){return interrupts;}
proc_t current_proc(){return current;}void proc_lock(proc_t){assert(!locks);locks++;procReads++;}
void proc_unlock(proc_t){assert(locks==1);locks--;}const char*proc_best_name(proc_t p){assert(locks);return p->name;}
void proc_getexecutableuuid(proc_t p,uint8_t*out,size_t n){assert(locks&&n==16);memcpy(out,p->uuid,n);}
int proc_pid(proc_t p){assert(p==&own);return 63;}
size_t strlcpy(char*out,const char*in,size_t n){size_t z=strlen(in);if(n){memcpy(out,in,std::min(z,n-1));out[std::min(z,n-1)]=0;}return z;}
struct context {proc_t process=&own;}ctx;using vfs_context_t=context*;
vfs_context_t vfs_context_current(){return &ctx;}proc_t vfs_context_proc(vfs_context_t c){return c->process;}
enum uio_seg {UIO_USERSPACE,UIO_SYSSPACE};
enum {F_GETPROTECTIONCLASS=63,F_SETPROTECTIONCLASS=64,F_FULLFSYNC=51,F_BARRIERFSYNC=85,F_GETPATH=50,F_GETFD=1};
struct vnode_attr {uint64_t va_supported=0;unsigned va_uid=0,va_gid=0,va_mode=0,va_dataprotect_class=0;};
enum{bit_va_uid=1,bit_va_gid=2,bit_va_mode=4,bit_va_dataprotect_class=8};
#define VATTR_INIT(p) (*(p)=vnode_attr{})
#define VATTR_WANTED(p,f) ((void)(p))
#define VATTR_IS_SUPPORTED(p,f) (((p)->va_supported & bit_##f)!=0)
struct vnode_state {bool regular=true;std::string path="/private/var/root/Library/Caches/locationd/cells.db";}node;
using vnode_t=vnode_state*;
#define NULLVP nullptr
int lookupError=0,pathError=0,attrError=0,copyError=0;size_t copyCountOverride=0;
int vnode_getfromfd(vfs_context_t c,int fd,vnode_t*out){assert(c==&ctx&&!locks&&fd>=-1);fdReads++;if(lookupError)return lookupError;refs++;*out=&node;return 0;}
bool vnode_isreg(vnode_t p){assert(p==&node&&refs==1);return p->regular;}
int vn_getpath(vnode_t p,char*out,int*n){assert(p==&node&&refs==1&&*n==512);if(pathError)return pathError;assert(node.path.size()<512);memcpy(out,node.path.c_str(),node.path.size()+1);*n=int(node.path.size()+1);return 0;}
int vnode_getattr(vnode_t p,vnode_attr*a,vfs_context_t c){assert(p==&node&&c==&ctx&&refs==1);if(attrError)return attrError;*a={15,501,501,0600,3};return 0;}
void vnode_put(vnode_t p){assert(p==&node&&refs==1);refs--;putCalls++;}
std::string supplied="/var/db/locationd/cells.db";
int copyinstr(user_addr_t address,char*out,size_t limit,size_t*count){assert(address==0x1234&&!locks);copies++;if(copyError)return copyError;if(supplied.size()+1>limit)return 63;memcpy(out,supplied.c_str(),supplied.size()+1);*count=copyCountOverride?copyCountOverride:supplied.size()+1;return 0;}
std::vector<std::string> output;
int capture_printf(const char*format,...){assert(!locks&&!refs);char line[2048];va_list a;va_start(a,format);int n=vsnprintf(line,sizeof(line),format,a);va_end(a);assert(n>0&&n<int(sizeof(line)));output.emplace_back(line);return n;}
#define printf capture_printf

/* Normal, bounded original locationd database I/O evidence. These observers
 * run only after original operations release their FD/vnode locks/references.
 * They never modify an operation's arguments, retval, error or authorization.
 * Paths/metadata only: no database contents, file keys or user buffers read. */
static uint32_t kebab_location_open_reports, kebab_location_fd_reports;
static bool
kebab_location_io_selected(proc_t process)
{
    if (panic_active() || ml_at_interrupt_context() || get_preemption_level() != 0 ||
        !ml_get_interrupts_enabled() || process == PROC_NULL || process != current_proc() ||
        strncmp(process->p_comm, "locationd", sizeof("locationd"))) return false;
    static const uint8_t expected[16] = {0x72,0x36,0xf5,0x20,0xb3,0x31,0x38,0x3c,0x8b,0x98,0x5c,0x4e,0x2c,0xc5,0x9c,0x06};
    uint8_t uuid[16] = {};
    char name[sizeof(proc_name_t)] = {};
    proc_lock(process);
    strlcpy(name, proc_best_name(process), sizeof(name));
    proc_getexecutableuuid(process, uuid, sizeof(uuid));
    proc_unlock(process);
    return !strcmp(name, "locationd") && !memcmp(uuid, expected, sizeof(uuid));
}
static bool
kebab_location_io_contains(const char *path, const char *needle, size_t needle_bytes)
{
    size_t bytes = 0;
    while (bytes < 512 && path[bytes]) bytes++;
    if (bytes == 512 || needle_bytes > bytes) return false;
    for (size_t i = 0; i <= bytes - needle_bytes; i++)
        if (!memcmp(path + i, needle, needle_bytes)) return true;
    return false;
}
static bool
kebab_location_io_path(const char *path)
{
    return kebab_location_io_contains(path, "/locationd/", sizeof("/locationd/") - 1) ||
        kebab_location_io_contains(path, ".db", sizeof(".db") - 1) ||
        kebab_location_io_contains(path, ".sqlite", sizeof(".sqlite") - 1) ||
        kebab_location_io_contains(path, "-wal", sizeof("-wal") - 1) ||
        kebab_location_io_contains(path, "-shm", sizeof("-shm") - 1);
}
static void
kebab_location_io_sanitize(char *path)
{
    for (unsigned i = 0; path[i]; i++) if ((unsigned char)path[i] < 32 || path[i] == 127) path[i] = '?';
}
static bool
kebab_location_io_claim(uint32_t *counter, unsigned cap)
{
    if (__atomic_load_n(counter, __ATOMIC_RELAXED) >= cap) return false;
    return __atomic_fetch_add(counter, 1u, __ATOMIC_RELAXED) < cap;
}
void kebab_locationd_fd_result(proc_t, const char *, int, int, int, int);
void
kebab_locationd_fd_result(proc_t process, const char *operation, int fd, int command, int error, int result)
{
    if (!kebab_location_io_selected(process) || operation == NULL ||
        (strcmp(operation, "open") && strcmp(operation, "fcntl") && strcmp(operation, "fsync")) ||
        __atomic_load_n(&kebab_location_fd_reports, __ATOMIC_RELAXED) >= 128) return;
    if (!strcmp(operation, "fcntl") && !error && command != F_GETPROTECTIONCLASS &&
        command != F_SETPROTECTIONCLASS && command != F_FULLFSYNC &&
        command != F_BARRIERFSYNC && command != F_GETPATH) return;
    vnode_t vnode = NULLVP;
    vfs_context_t context = vfs_context_current();
    int lookup = vnode_getfromfd(context, fd, &vnode), path_error = lookup, attr_error = lookup;
    char path[512] = {};
    struct vnode_attr attr = {};
    bool regular = false;
    if (!lookup) {
        regular = vnode_isreg(vnode);
        if (regular) {
            int bytes = sizeof(path);
            path_error = vn_getpath(vnode, path, &bytes);
            path[sizeof(path) - 1] = 0;
            VATTR_INIT(&attr);
            VATTR_WANTED(&attr, va_uid);VATTR_WANTED(&attr, va_gid);
            VATTR_WANTED(&attr, va_mode);VATTR_WANTED(&attr, va_dataprotect_class);
            attr_error = vnode_getattr(vnode, &attr, context);
        }
        vnode_put(vnode);
    }
    if ((!regular || path_error || !kebab_location_io_path(path)) && !(error && (lookup || path_error))) return;
    if (!kebab_location_io_claim(&kebab_location_fd_reports, 128)) return;
    kebab_location_io_sanitize(path);
    unsigned supported = (VATTR_IS_SUPPORTED(&attr, va_uid) ? 1u : 0u) |
        (VATTR_IS_SUPPORTED(&attr, va_gid) ? 2u : 0u) |
        (VATTR_IS_SUPPORTED(&attr, va_mode) ? 4u : 0u) |
        (VATTR_IS_SUPPORTED(&attr, va_dataprotect_class) ? 8u : 0u);
    printf("KEBAB LOC-IO: op=%s pid=%d fd=%d command=%d error=%d result=%d lookup=%d path-error=%d attr-error=%d supported=0x%x uid=%u gid=%u mode=0%o class=%u path=%s\n",
        operation, proc_pid(process), fd, command, error, result, lookup, path_error, attr_error, supported,
        attr.va_uid, attr.va_gid, attr.va_mode, attr.va_dataprotect_class, path[0] ? path : "<unavailable>");
}
void kebab_locationd_open_result(vfs_context_t, user_addr_t, enum uio_seg, int, int, int, int, int);
void
kebab_locationd_open_result(vfs_context_t context, user_addr_t address, enum uio_seg segment,
    int flags, int requested_class, int directory_fd, int error, int result_fd)
{
    proc_t process = vfs_context_proc(context);
    if (segment != UIO_USERSPACE || !kebab_location_io_selected(process) ||
        __atomic_load_n(&kebab_location_open_reports, __ATOMIC_RELAXED) >= 64) return;
    char path[512] = {};
    size_t copied = 0;
    int copied_error = copyinstr(address, path, sizeof(path), &copied);
    if (copied_error || !copied || copied > sizeof(path)) return;
    path[sizeof(path) - 1] = 0;
    if (!kebab_location_io_path(path) || !kebab_location_io_claim(&kebab_location_open_reports, 64)) return;
    kebab_location_io_sanitize(path);
    printf("KEBAB LOC-OPEN: pid=%d dirfd=%d flags=0x%x requested-class=%d error=%d fd=%d path=%s\n",
        proc_pid(process), directory_fd, flags, requested_class, error, error ? -1 : result_fd, path);
    if (!error) kebab_locationd_fd_result(process, "open", result_fd, 0, 0, 0);
}

#undef printf
static void reset(){
 own=process{};other=process{};current=&own;ctx.process=&own;node=vnode_state{};
 panicking=irq=false;interrupts=true;preemption=locks=refs=procReads=fdReads=putCalls=copies=0;
 lookupError=pathError=attrError=copyError=0;copyCountOverride=0;
 supplied="/var/db/locationd/cells.db";output.clear();kebab_location_open_reports=kebab_location_fd_reports=0;
}
static bool has(const char*s){return std::any_of(output.begin(),output.end(),[&](const std::string&line){return line.find(s)!=std::string::npos;});}
static void openResult(int error=0){kebab_locationd_open_result(&ctx,0x1234,UIO_USERSPACE,0x202,3,-2,error,7);assert(!locks&&!refs);}
int main(){
 reset();openResult();assert(output.size()==2&&has("requested-class=3 error=0 fd=7")&&has("supported=0xf uid=501 gid=501 mode=0600 class=3"));assert(copies==1&&fdReads==1&&putCalls==1);
 reset();openResult(13);assert(output.size()==1&&has("error=13 fd=-1")&&!fdReads);
 for(int gate=0;gate<8;gate++){
  reset();if(gate==0)panicking=true;if(gate==1)irq=true;if(gate==2)preemption=1;if(gate==3)interrupts=false;
  if(gate==4)ctx.process=nullptr;if(gate==5)ctx.process=&other;if(gate==6)strcpy(own.p_comm,"locationd-extra");if(gate==7)current=&other;
  openResult();assert(output.empty()&&!procReads&&!copies&&!fdReads);
 }
 reset();strcpy(own.name,"locationd.extra");openResult();assert(output.empty()&&procReads==1&&!copies);
 reset();own.uuid[0]^=1;openResult();assert(output.empty()&&procReads==1&&!copies);
 for(unsigned uid:{0u,501u,205u}){reset();own.uid=uid;openResult();assert(output.size()==2&&own.uid==uid);}
 reset();kebab_locationd_open_result(&ctx,0x1234,UIO_SYSSPACE,0,0,0,0,7);assert(!copies&&output.empty());
 for(auto path:{"/tmp/plain", "cells.db", "x.sqlite", "x-wal", "x-shm", "/locationd/x"}){
  reset();supplied=path;openResult(2);assert(output.size()==(supplied=="/tmp/plain"?0u:1u));
 }
 for(int error:{14,63}){reset();copyError=error;openResult();assert(output.empty()&&!fdReads);}
 reset();copyCountOverride=513;openResult();assert(output.empty());
 reset();supplied=std::string(512,'a')+".db";openResult();assert(output.empty());
 char unterm[512];memset(unterm,'a',sizeof(unterm));assert(!kebab_location_io_path(unterm));
 char tiny[1]={0};assert(!kebab_location_io_path(tiny));
 reset();supplied="/var/db/locationd/a\n\r\177.db";openResult(5);assert(output.size()==1&&has("a???.db"));assert(std::count(output[0].begin(),output[0].end(),'\n')==1);
 reset();kebab_locationd_fd_result(&own,"fcntl",7,F_GETFD,0,0);assert(output.empty()&&!fdReads);
 for(int command:{F_GETPROTECTIONCLASS,F_SETPROTECTIONCLASS,F_FULLFSYNC,F_BARRIERFSYNC,F_GETPATH}){
  reset();kebab_locationd_fd_result(&own,"fcntl",7,command,0,3);assert(output.size()==1&&has("result=3")&&putCalls==1);
 }
 reset();kebab_locationd_fd_result(&own,"fcntl",7,F_GETFD,9,0);assert(output.size()==1&&has("error=9 result=0"));
 reset();lookupError=9;kebab_locationd_fd_result(&own,"fcntl",7,F_GETPATH,9,0);assert(output.size()==1&&has("lookup=9 path-error=9 attr-error=9 supported=0x0")&&has("path=<unavailable>")&&!putCalls);
 reset();pathError=2;kebab_locationd_fd_result(&own,"fcntl",7,F_GETPATH,5,0);assert(output.size()==1&&has("error=5")&&has("path-error=2")&&has("path=<unavailable>")&&putCalls==1);
 reset();lookupError=9;kebab_locationd_fd_result(&own,"fcntl",7,F_GETPATH,0,0);assert(output.empty());
 reset();node.regular=false;kebab_locationd_fd_result(&own,"fsync",7,0,5,0);assert(output.empty()&&putCalls==1);
 reset();node.path="/private/var/plain";kebab_locationd_fd_result(&own,"fsync",7,0,5,0);assert(output.empty()&&putCalls==1);
 reset();attrError=13;kebab_locationd_fd_result(&own,"fsync",7,0,5,0);assert(output.size()==1&&has("attr-error=13 supported=0x0"));
 reset();for(int i=0;i<256;i++)openResult(5);assert(output.size()==64&&copies==64&&kebab_location_open_reports==64);
 reset();for(int i=0;i<256;i++)kebab_locationd_fd_result(&own,"fsync",7,0,5,0);assert(output.size()==128&&fdReads==128&&putCalls==128);
 reset();kebab_locationd_fd_result(&own,nullptr,7,0,5,0);kebab_locationd_fd_result(&own,"read-user-buffer",7,0,5,0);assert(!fdReads&&output.empty());
 // Exercise the production atomic claim itself without thread-unsafe VFS stubs.
 uint32_t claims=0;std::atomic<unsigned> accepted{0};std::vector<std::thread> threads;
 for(unsigned i=0;i<8;i++)threads.emplace_back([&]{for(unsigned j=0;j<1024;j++)if(kebab_location_io_claim(&claims,128))accepted++;});
 for(auto&t:threads)t.join();assert(accepted==128&&claims>=128&&claims<=135);
 std::puts("PASS complete production location I/O observer gates/path/error/reference/cap cases; APIs modeled, not device proof");
}
