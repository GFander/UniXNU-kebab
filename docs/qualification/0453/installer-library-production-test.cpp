
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <sys/types.h>
#define EAUTH 80
#define ENOENT 2
using mount_t=int;using vnode_t=int;
#define NULLVP 0
enum{MNT_WAIT=1};
struct vnode_attr{unsigned active,supported,va_uid,va_gid,va_mode,va_flags,va_dataprotect_class;};
enum{bit_va_uid=1,bit_va_gid=2,bit_va_mode=4,bit_va_flags=8,bit_va_dataprotect_class=16};
#define VATTR_INIT(p) do{(p)->active=(p)->supported=0;}while(0)
#define VATTR_WANTED(p,f) ((p)->active|=bit_##f)
#define VATTR_IS_SUPPORTED(p,f) ((p)->supported&bit_##f)
#define VATTR_SET(p,f,x) do{(p)->f=(x);(p)->active|=bit_##f;}while(0)
static int refs,lookError,getError,setError,syncError,flushError;static unsigned gets,sets,syncs,flushes,mask=31;static bool isDir=true,mismatch=false;
static vnode_attr disk;static std::string output;
static int checked_printf(const char *fmt,...){assert(!refs);char text[2048];va_list args;va_start(args,fmt);int n=vsnprintf(text,sizeof(text),fmt,args);va_end(args);output+=text;return n;}
#define printf checked_printf
static int vfs_context_current(){return 17;}
static int kdi_lookup(const char *path,vnode_t *file,mount_t data,bool source){assert(!refs&&data==18&&!source&&!strcmp(path,"/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library"));if(lookError)return lookError;*file=19;refs=1;return 0;}
static int vnode_getattr(vnode_t file,vnode_attr *a,int context){assert(refs==1&&file==19&&context==17&&a->active==31);gets++;if(getError)return getError;*a=disk;a->supported=mask;if(mismatch&&sets)a->va_uid=0;return 0;}
static bool vnode_isdir(vnode_t file){assert(refs==1&&file==19);return isDir;}
static void vnode_put(vnode_t file){assert(refs==1&&file==19);refs=0;}
static int vnode_setattr(vnode_t file,vnode_attr *a,int context){assert(refs==1&&file==19&&context==17);assert(a->active==23&&a->va_uid==501&&a->va_gid==501&&a->va_mode==0751&&a->va_dataprotect_class==4);sets++;if(setError)return setError;disk.va_uid=a->va_uid;disk.va_gid=a->va_gid;disk.va_mode=a->va_mode;disk.va_dataprotect_class=a->va_dataprotect_class;return 0;}
static int VFS_SYNC(mount_t data,int wait,int context){assert(!refs&&data==18&&wait==1&&context==17);syncs++;return syncError;}
static int kebab_ufs_sync(){assert(!refs&&syncs==1);flushes++;return flushError;}
static int
kdi_installer_library_prepare(mount_t data)
{
 static const char path[]="/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library";
 vnode_t file=NULLVP;int error=kdi_lookup(path,&file,data,false);
 if(error==ENOENT){printf("KEBAB INSTALLER LIBRARY: absent; original initializer retained\n");return 0;}
 if(error)return error;
 struct vnode_attr a={};VATTR_INIT(&a);
 VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);
 VATTR_WANTED(&a,va_flags);VATTR_WANTED(&a,va_dataprotect_class);
 error=vnode_getattr(file,&a,vfs_context_current());
 if(!error&&(!vnode_isdir(file)||!VATTR_IS_SUPPORTED(&a,va_uid)||!VATTR_IS_SUPPORTED(&a,va_gid)||
     !VATTR_IS_SUPPORTED(&a,va_mode)||!VATTR_IS_SUPPORTED(&a,va_flags)||
     !VATTR_IS_SUPPORTED(&a,va_dataprotect_class)))error=EAUTH;
 bool legacy=!error&&a.va_uid==(uid_t)-2&&a.va_gid==(gid_t)-2&&
     (a.va_mode&07777)==0&&a.va_flags==0&&a.va_dataprotect_class==0;
 if(!error&&legacy){
  struct vnode_attr desired={};VATTR_INIT(&desired);
  VATTR_SET(&desired,va_uid,501);VATTR_SET(&desired,va_gid,501);
  VATTR_SET(&desired,va_mode,0751);VATTR_SET(&desired,va_dataprotect_class,4);
  error=vnode_setattr(file,&desired,vfs_context_current());
  if(!error){
   VATTR_INIT(&a);VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);
   VATTR_WANTED(&a,va_flags);VATTR_WANTED(&a,va_dataprotect_class);
   error=vnode_getattr(file,&a,vfs_context_current());
   if(!error&&(!VATTR_IS_SUPPORTED(&a,va_uid)||!VATTR_IS_SUPPORTED(&a,va_gid)||
       !VATTR_IS_SUPPORTED(&a,va_mode)||!VATTR_IS_SUPPORTED(&a,va_flags)||
       !VATTR_IS_SUPPORTED(&a,va_dataprotect_class)||a.va_uid!=501||a.va_gid!=501||
       (a.va_mode&07777)!=0751||a.va_flags||a.va_dataprotect_class!=4))error=EAUTH;
  }
 }
 vnode_put(file);
 if(!error&&legacy){error=VFS_SYNC(data,MNT_WAIT,vfs_context_current());if(!error)error=kebab_ufs_sync();}
 printf("KEBAB INSTALLER LIBRARY: legacy=%u error=%d uid=%u gid=%u mode=0%o class=%u; fixed Data directory only\n",
     legacy,error,a.va_uid,a.va_gid,a.va_mode&07777,a.va_dataprotect_class);
 return error;
}

static void reset(){assert(!refs);lookError=getError=setError=syncError=flushError=0;gets=sets=syncs=flushes=0;mask=31;isDir=true;mismatch=false;output.clear();disk={0,31,(unsigned)-2,(unsigned)-2,0,0,0};}
int main(){unsigned cases=0;
 reset();assert(!kdi_installer_library_prepare(18)&&sets==1&&syncs==1&&flushes==1&&gets==2&&disk.va_uid==501&&disk.va_gid==501&&disk.va_mode==0751&&disk.va_dataprotect_class==4);cases++;
 // Idempotent cold-state path: no metadata or sync call after exact repair.
 sets=syncs=flushes=gets=0;assert(!kdi_installer_library_prepare(18)&&!sets&&!syncs&&!flushes&&gets==1);cases++;
 for(int error:{2,13,40,80}){reset();lookError=error;assert(kdi_installer_library_prepare(18)==(error==2?0:error)&&!refs&&!sets&&!gets&&!syncs);cases++;}
 for(int error:{5,13}){reset();getError=error;assert(kdi_installer_library_prepare(18)==error&&!refs&&!sets&&!syncs);cases++;}
 for(unsigned bit=1;bit<=16;bit*=2){reset();mask^=bit;assert(kdi_installer_library_prepare(18)==80&&!refs&&!sets&&!syncs);cases++;}
 reset();isDir=false;assert(kdi_installer_library_prepare(18)==80&&!refs&&!sets&&!syncs);cases++;
 // Every non-observed metadata variant must stay unchanged, including a
 // deliberately restricted ordinary owner, flags and other protection class.
 for(unsigned field=0;field<6;field++){reset();unsigned *p=field==0?&disk.va_uid:field==1?&disk.va_gid:field==2?&disk.va_mode:field==3?&disk.va_flags:&disk.va_dataprotect_class;*p=field==2?0755:field==4?3:field==5?4:501;auto before=disk;assert(!kdi_installer_library_prepare(18)&&!refs&&!sets&&!syncs&&!memcmp(&before,&disk,sizeof(disk)));cases++;}
 reset();disk.va_uid=disk.va_gid=501;assert(!kdi_installer_library_prepare(18)&&!sets&&disk.va_mode==0);cases++;
 for(int error:{5,13,45}){reset();setError=error;assert(kdi_installer_library_prepare(18)==error&&!refs&&sets==1&&!syncs&&!flushes);cases++;}
 reset();mismatch=true;assert(kdi_installer_library_prepare(18)==80&&!refs&&sets==1&&!syncs&&!flushes);cases++;
 reset();syncError=5;assert(kdi_installer_library_prepare(18)==5&&!refs&&sets==1&&syncs==1&&!flushes);cases++;
 reset();flushError=5;assert(kdi_installer_library_prepare(18)==5&&!refs&&sets==1&&syncs==1&&flushes==1);cases++;
 fprintf(stdout,"PASS %u exact-production installer Library states/faults; strict observed-state write gate, normal metadata untouched, required UID/GID/mode/class, reread, reference cleanup and ordered durability; VFS/lookup models, not hardware persistence proof\n",cases);
}
