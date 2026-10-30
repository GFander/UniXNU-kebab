
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <string>
#include <map>
#define MAXPATHLEN 1024
#define MNTK_KERNEL_MOUNT 0x1000
#define BINDFS_ENTITLEMENT "com.apple.private.bindfs-allow"
#define EAUTH 80
#define VNODE_LOOKUP_NOFOLLOW 1
#define MNT_RDONLY 1
#define MNT_UNION 32
#define MNT_NOSUID 8
#define MNT_NODEV 16
#define NULLVP nullptr
#define KEBAB_CRYPTEX_MOUNT_PATH "/private/preboot/Cryptexes/OS"
struct mount { unsigned mnt_kern_flag=0;unsigned flags=0;const char *name="apfs"; };
using mount_t=struct mount*;using user_addr_t=uintptr_t;using vfs_context_t=int;
static bool entitled=false;static int entitlement_calls=0,user_copies=0,copy_error=0;
static int vfs_context_iskernel(int ctx){return ctx==2;}
static bool IOCurrentTaskHasEntitlement(const char*){entitlement_calls++;return entitled;}
static int copyinstr(user_addr_t input,char*out,size_t cap,size_t*count){
 user_copies++;if(copy_error)return copy_error;
 auto*s=(const char*)input;if(!s)return EFAULT;size_t n=strnlen(s,cap);
 if(n==cap)return ENAMETOOLONG;*count=n+1;memcpy(out,s,*count);return 0;
}

static int
bindfs_mount_path(struct mount *mp, user_addr_t input, vfs_context_t ctx,
    char data[MAXPATHLEN], size_t *count)
{
 if (vfs_context_iskernel(ctx) && (mp->mnt_kern_flag & MNTK_KERNEL_MOUNT)) {
  if (!input) return EFAULT;
  const char *path = (const char *)(uintptr_t)input;
  size_t length = strnlen(path, MAXPATHLEN);
  if (length == MAXPATHLEN) return ENAMETOOLONG;
  if (!length) return EINVAL;
  *count = length + 1;
  memcpy(data, path, *count);
  return 0;
 }
 if (!IOCurrentTaskHasEntitlement(BINDFS_ENTITLEMENT)) return EPERM;
 return copyinstr(input, data, MAXPATHLEN - 1, count);
}

struct vnode { mount_t mp; bool directory; int refs=0; };
using vnode_t=vnode*;
static struct mount sm,om,bm;
static vnode upper{&om,true},lower{&sm,true},visible{&bm,true},file{&bm,false},backfile{&om,false};
static mount_t kcm_system=&sm,kcm_owned_mount=&om;
static int steps=0,fail_at=0,mounts=0,mode=12,policy=0,wrong_source=0,wrong_target=0,wrong_flags=0,wrong_backing=0;
static bool mounted=false;
static int step(){return ++steps==fail_at?EIO:0;}
static void PE_parse_boot_argn(const char*,unsigned*out,size_t){*out=mode;}
static int kdf_policy(){return policy;}
static int kebab_cryptex_mount_recheck(){return step();}
static int vfs_context_kernel(){return 2;}
static int vnode_lookup(const char*path,int flags,vnode_t*out,int ctx){
 assert(flags==VNODE_LOOKUP_NOFOLLOW && ctx==2);int e=step();if(e)return e;
 if(!strcmp(path,KEBAB_CRYPTEX_MOUNT_PATH "/usr/lib"))*out=&upper;
 else if(!strcmp(path,"/usr/lib"))*out=mounted?&visible:&lower;
 else *out=&file;
 (*out)->refs++;return 0;
}
static void vnode_put(vnode_t v){assert(v&&v->refs>0);v->refs--;}
static bool vnode_isdir(vnode_t v){return v->directory;}
static mount_t vnode_mount(vnode_t v){return v->mp;}
static bool kcm_mount_readonly(mount_t mp){return mp->flags&MNT_RDONLY;}
static int kebab_cryptex_vnode_recheck(vnode_t v){
 int e=step();if(e)return e;return v->mp==&om && !wrong_source?0:EAUTH;
}
static int kcm_verify_file_on_mount(const char*path,off_t bytes,const uint8_t hash[32],mount_t mp){
 assert(mp==&om);assert(!strncmp(path,KEBAB_CRYPTEX_MOUNT_PATH "/usr/lib/",35));
 assert(bytes==68080||bytes==51504||bytes==819568);assert(hash[0]);return step();
}
static int kernel_mount(const char*type,vnode_t p,vnode_t v,const char*target,void*data,size_t size,int flags,unsigned kflags,int ctx){
 assert(!strcmp(type,"bindfs") && !p && !v && !strcmp(target,"/usr/lib"));
 assert(!strcmp((char*)data,KEBAB_CRYPTEX_MOUNT_PATH "/usr/lib"));
 assert(size==strlen((char*)data)+1 && flags==(MNT_RDONLY|MNT_UNION|MNT_NOSUID|MNT_NODEV));
 assert(!kflags && ctx==2);mounts++;int e=step();if(!e)mounted=true;return e;
}
struct attr {const char *f_fstypename;};
static attr* vfs_statfs(mount_t mp){static attr a;a.f_fstypename=mp->name;return &a;}
static unsigned vfs_flags(mount_t mp){return mp->flags^(wrong_flags?MNT_RDONLY:0);}
static int bindfs_getbackingvnode(vnode_t v,vnode_t*out){
 int e=step();if(e)return e;*out=v==&visible?&upper:&backfile;
 if(wrong_backing)*out=&lower;(*out)->refs++;return 0;
}
static int kcm_verify_boot_file(const char*path,off_t size,const uint8_t hash[32]){
 assert(!strcmp(path,"/usr/lib/dyld")&&size==922912&&hash[0]==0x58);return step();
}

int
kebab_cryptex_libraries_prepare(void)
{
 static const char source[] = KEBAB_CRYPTEX_MOUNT_PATH "/usr/lib";
 static const char target[] = "/usr/lib";
 static const struct { const char *name; off_t bytes; uint8_t hash[32]; } files[] = {
  {"libobjc-trampolines.dylib",68080,{0x16,0xa4,0x8e,0x77,0x84,0x63,0x0f,0xaa,0x0a,0xa8,0x93,0x44,0x30,0x33,0xa1,0x3f,0x6b,0xb4,0x65,0xce,0x98,0x6b,0x11,0xbf,0x0b,0xbd,0x8d,0x9b,0x9b,0x77,0xe4,0x6c}},
  {"libffi-trampolines.dylib",51504,{0x9f,0x0a,0x51,0x5a,0x88,0xa5,0xac,0x23,0xc4,0x34,0xac,0x70,0xc9,0x9b,0x49,0xc9,0xe0,0x02,0x92,0x91,0x5b,0x8c,0xb1,0x19,0xd1,0x81,0x16,0xd4,0x78,0x7d,0x26,0xdd}},
  {"libstdc++.6.0.9.dylib",819568,{0x33,0x97,0x28,0xe8,0x58,0xb4,0x9f,0x94,0x83,0x1f,0xf9,0x97,0x75,0xdd,0x99,0xb4,0xa4,0xbc,0x17,0x8a,0x09,0x41,0x61,0x46,0x21,0x47,0xbd,0x30,0xa4,0xdb,0x84,0x95}}
 };
 unsigned mode=0;PE_parse_boot_argn("kebab_apfs_tool",&mode,sizeof(mode));
 if(mode!=12 || kdf_policy())return EPERM;
 int error=kebab_cryptex_mount_recheck();
 vfs_context_t ctx=vfs_context_kernel();
 vnode_t upper=NULLVP,lower=NULLVP,visible=NULLVP,backing=NULLVP;
 if(!error)error=vnode_lookup(source,VNODE_LOOKUP_NOFOLLOW,&upper,ctx);
 if(!error && (!vnode_isdir(upper)||kebab_cryptex_vnode_recheck(upper)))error=EAUTH;
 if(!error)error=vnode_lookup(target,VNODE_LOOKUP_NOFOLLOW,&lower,ctx);
 if(!error && (!vnode_isdir(lower)||vnode_mount(lower)!=kcm_system||
     !kcm_mount_readonly(kcm_system)))error=EAUTH;
 for(unsigned i=0;!error&&i<sizeof(files)/sizeof(*files);i++){
  char path[MAXPATHLEN];snprintf(path,sizeof(path),"%s/%s",source,files[i].name);
  error=kcm_verify_file_on_mount(path,files[i].bytes,files[i].hash,kcm_owned_mount);
 }
 if(!error)error=kernel_mount("bindfs",NULLVP,NULLVP,target,(void *)source,
     sizeof(source),MNT_RDONLY|MNT_UNION|MNT_NOSUID|MNT_NODEV,0,ctx);
 if(!error)error=vnode_lookup(target,VNODE_LOOKUP_NOFOLLOW,&visible,ctx);
 if(!error){
  mount_t mp=vnode_mount(visible);
  if(strcmp(vfs_statfs(mp)->f_fstypename,"bindfs") ||
      (vfs_flags(mp)&(MNT_RDONLY|MNT_UNION|MNT_NOSUID|MNT_NODEV))!=
      (MNT_RDONLY|MNT_UNION|MNT_NOSUID|MNT_NODEV))error=EAUTH;
  if(!error)error=bindfs_getbackingvnode(visible,&backing);
  if(!error && backing!=upper)error=EAUTH;
  if(backing){vnode_put(backing);backing=NULLVP;}
 }
 for(unsigned i=0;!error&&i<sizeof(files)/sizeof(*files);i++){
  char path[MAXPATHLEN];snprintf(path,sizeof(path),"%s/%s",target,files[i].name);
  vnode_t file=NULLVP;error=vnode_lookup(path,VNODE_LOOKUP_NOFOLLOW,&file,ctx);
  if(!error)error=bindfs_getbackingvnode(file,&backing);
  if(!error)error=kebab_cryptex_vnode_recheck(backing);
  if(backing){vnode_put(backing);backing=NULLVP;}
  if(file)vnode_put(file);
 }
 /* Missing upper dyld must fall through the native union to the exact lower
  * file, not a replacement linker. Rehash only this small existing boot pin. */
 static const uint8_t dyld[32]={0x58,0x06,0x83,0x45,0x64,0xfe,0x64,0x62,0xae,0x98,0x1d,0x09,0x15,0xd6,0x12,0xb1,0xeb,0x2f,0x6f,0x16,0x88,0x22,0xb5,0xd2,0x78,0xbe,0xe5,0x62,0x54,0x93,0x37,0x57};
 if(!error)error=kcm_verify_boot_file("/usr/lib/dyld",922912,dyld);
 if(!error)error=kebab_cryptex_mount_recheck();
 if(visible)vnode_put(visible);
 if(lower)vnode_put(lower);
 if(upper)vnode_put(upper);
 printf("KEBAB CRYPTEX LIBRARIES: original3 SHA/RO union + original lower dyld/root identity %s (%d)\n",error?"FAIL":"PASS",error);
 return error;
}

static void reset(){steps=mounts=fail_at=policy=wrong_source=wrong_target=wrong_flags=wrong_backing=0;mode=12;mounted=false;
 sm.flags=om.flags=MNT_RDONLY;bm.flags=MNT_RDONLY|MNT_UNION|MNT_NOSUID|MNT_NODEV;bm.name="bindfs";
 upper.directory=lower.directory=true;lower.mp=&sm;
 assert(!upper.refs&&!lower.refs&&!visible.refs&&!file.refs&&!backfile.refs);
}
static void refs(){assert(!upper.refs&&!lower.refs&&!visible.refs&&!file.refs&&!backfile.refs);}
int main(){int cases=0;struct mount mp;char out[MAXPATHLEN];size_t count;
 for(int ctx:{1,2})for(int flag:{0,MNTK_KERNEL_MOUNT})for(bool ent:{false,true}){
  entitlement_calls=user_copies=0;entitled=ent;mp.mnt_kern_flag=flag;
  const char*src="/genuine/source";int e=bindfs_mount_path(&mp,(uintptr_t)src,ctx,out,&count);
  bool kernel=ctx==2&&flag;assert(e==(kernel||ent?0:EPERM));
  assert(entitlement_calls==!kernel && user_copies==(!kernel&&ent));
  if(!e)assert(count==strlen(src)+1&&!strcmp(src,out));cases++;
 }
 mp.mnt_kern_flag=MNTK_KERNEL_MOUNT;
 assert(bindfs_mount_path(&mp,0,2,out,&count)==EFAULT);cases++;
 for(size_t n:{0u,1u,1022u,1023u,1024u,2048u}){
  std::string s(n,'a');int e=bindfs_mount_path(&mp,(uintptr_t)s.c_str(),2,out,&count);
  assert(e==(n==0?EINVAL:n>=1024?ENAMETOOLONG:0));if(!e)assert(count==n+1);cases++;
 }
 entitled=true;copy_error=EFAULT;mp.mnt_kern_flag=0;assert(bindfs_mount_path(&mp,1,1,out,&count)==EFAULT);cases++;copy_error=0;
 reset();assert(kebab_cryptex_libraries_prepare()==0);int total=steps;refs();cases++;
 for(int i=1;i<=total;i++){reset();fail_at=i;assert(kebab_cryptex_libraries_prepare()!=0);refs();cases++;}
 for(int i=0;i<8;i++){reset();switch(i){case 0:mode=7;break;case 1:policy=1;break;case 2:wrong_source=1;break;case 3:lower.mp=&om;break;case 4:wrong_flags=1;break;case 5:wrong_backing=1;break;case 6:upper.directory=false;break;case 7:bm.name="unexpectedfs";break;}
  assert(kebab_cryptex_libraries_prepare()!=0);refs();if(i<4||i==6)assert(mounts==0);cases++;
 }
 printf("PASS: %d actual kernel/user path and native RO library mount cases; VFS/IOKit calls mocked\n",cases);
}
