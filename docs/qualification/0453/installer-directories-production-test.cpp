
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
static unsigned references,ndActive,indexNow;static int failLookup=-1,failAttr=-1;static bool unsupported=false;static std::string output;
static int checked_printf(const char *fmt,...){assert(!references&&!ndActive);char text[2048];va_list args;va_start(args,fmt);int n=vsnprintf(text,sizeof(text),fmt,args);va_end(args);output+=text;return n;}
#define printf checked_printf
enum{LOOKUP=1,OP_LOOKUP=2,NOFOLLOW=4,UIO_SYSSPACE=8,NAMEI_NOFOLLOW_ANY=16};
using vfs_context_t=int;struct vnode{unsigned index;};static vnode node;using vnode_t=vnode *;
struct nameidata{unsigned ni_flag;vnode_t ni_vp;const char *path;};
static vfs_context_t vfs_context_current(){return 17;}
static void initialize(nameidata *nd,int look,int op,int nofollow,int segment,const char *path,vfs_context_t context){assert(look==1&&op==2&&nofollow==4&&segment==8&&context==17&&!references&&!ndActive);assert(!strncmp(path,"/private/var",12)&&!strstr(path,".."));nd->ni_flag=0;nd->path=path;nd->ni_vp=nullptr;}
#define NDINIT initialize
#define CAST_USER_ADDR_T(p) (p)
static int namei(nameidata *nd){assert(nd->ni_flag==16&&!references&&!ndActive);node.index=indexNow++;if((int)node.index==failLookup)return 40;ndActive=references=1;nd->ni_vp=&node;return 0;}
static void nameidone(nameidata *nd){assert(nd->ni_vp==&node&&ndActive==1&&references==1);ndActive=0;}
struct vnode_attr{unsigned wanted,supported,va_uid,va_gid,va_mode,va_flags,va_dataprotect_class;};
enum{bit_va_uid=1,bit_va_gid=2,bit_va_mode=4,bit_va_flags=8,bit_va_dataprotect_class=16};
#define VATTR_INIT(p) do{(p)->wanted=(p)->supported=0;}while(0)
#define VATTR_WANTED(p,f) ((p)->wanted|=bit_##f)
#define VATTR_IS_SUPPORTED(p,f) ((p)->supported&bit_##f)
static int vnode_getattr(vnode_t p,vnode_attr *attr,vfs_context_t context){assert(p==&node&&references==1&&!ndActive&&context==17&&attr->wanted==31);assert(!attr->va_uid&&!attr->va_gid&&!attr->va_mode&&!attr->va_flags&&!attr->va_dataprotect_class);if((int)p->index==failAttr)return 5;if(!unsupported){attr->supported=31;attr->va_uid=501;attr->va_gid=501;attr->va_mode=0751;attr->va_flags=0;attr->va_dataprotect_class=4;}return 0;}
static bool vnode_isdir(vnode_t p){assert(p==&node&&references==1&&!ndActive);return true;}
static void vnode_put(vnode_t p){assert(p==&node&&references==1&&!ndActive);references=0;}
static void
kebab_installer_directories_report(void)
{
    static const char *const paths[] = {
        "/private/var",
        "/private/var/containers",
        "/private/var/containers/Shared",
        "/private/var/containers/Shared/SystemGroup",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library",
        "/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library/InstallCoordination"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        struct nameidata nd;
        vfs_context_t context = vfs_context_current();
        NDINIT(&nd, LOOKUP, OP_LOOKUP, NOFOLLOW, UIO_SYSSPACE,
            CAST_USER_ADDR_T(paths[i]), context);
        nd.ni_flag |= NAMEI_NOFOLLOW_ANY;
        int error = namei(&nd);
        if (error) {
            printf("KEBAB MIG-DIR: index=%u lookup=%d path=%s (fixed nofollow)\n", i, error, paths[i]);
            continue;
        }
        vnode_t vnode = nd.ni_vp;
        nameidone(&nd);
        struct vnode_attr attr = {};
        VATTR_INIT(&attr);
        VATTR_WANTED(&attr, va_uid);VATTR_WANTED(&attr, va_gid);
        VATTR_WANTED(&attr, va_mode);VATTR_WANTED(&attr, va_flags);
        VATTR_WANTED(&attr, va_dataprotect_class);
        error = vnode_getattr(vnode, &attr, context);
        bool directory = vnode_isdir(vnode);
        vnode_put(vnode);
        unsigned supported = (VATTR_IS_SUPPORTED(&attr, va_uid) ? 1u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_gid) ? 2u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_mode) ? 4u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_flags) ? 8u : 0u) |
            (VATTR_IS_SUPPORTED(&attr, va_dataprotect_class) ? 16u : 0u);
        printf("KEBAB MIG-DIR: index=%u getattr=%d dir=%u supported=0x%x uid=%u gid=%u mode=0%o flags=0x%x class=%u path=%s\n",
            i, error, directory, supported, attr.va_uid, attr.va_gid,
            attr.va_mode & 07777, attr.va_flags, attr.va_dataprotect_class, paths[i]);
    }
}

static void reset(){assert(!references&&!ndActive);indexNow=0;output.clear();}
int main(){
 reset();kebab_installer_directories_report();assert(indexNow==7&&!references&&!ndActive&&output.find("supported=0x1f uid=501 gid=501 mode=0751 flags=0x0 class=4")!=std::string::npos);
 for(int i=0;i<7;i++){reset();failLookup=i;kebab_installer_directories_report();assert(indexNow==7&&!references&&!ndActive&&output.find("lookup=40")!=std::string::npos);}failLookup=-1;
 for(int i=0;i<7;i++){reset();failAttr=i;kebab_installer_directories_report();assert(indexNow==7&&!references&&!ndActive&&output.find("getattr=5 dir=1 supported=0x0 uid=0 gid=0 mode=00 flags=0x0 class=0")!=std::string::npos);}failAttr=-1;
 reset();unsupported=true;kebab_installer_directories_report();assert(indexNow==7&&output.find("supported=0x0 uid=0 gid=0 mode=00 flags=0x0 class=0")!=std::string::npos);
 fprintf(stdout,"PASS exact read-only metadata helper: balanced vnode/namei on success/every lookup and getattr fault; nofollow, supported masks, zero unsupported/failed fields, print after release; explicit VFS models, not actual disk proof\n");
}
