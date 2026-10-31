
#include <cassert>
#include <cstring>
#include <cstdio>
enum {EAUTH=80,MNT_CPROTECT=2,MNT_RDONLY=1,VNODE_LOOKUP_NOFOLLOW=1};
using dev_t=unsigned;
struct Mount {unsigned flags=2,device=0;const char *type="apfs";};using mount_t=Mount *;
struct Vnode {mount_t mount;bool directory=true;};using vnode_t=Vnode *;
#define NULLVP nullptr
struct Stat {const char *f_fstypename;};
struct kebab_volume_identity {unsigned major_number,minor_number;};
static Mount root{1,0x10002,"apfs"},data,other;
static Vnode rootnode{&root},point{&data};static vnode_t rootvnode=&rootnode;
static mount_t kcm_system=&root;static unsigned targetMinor=3,putCount=0;static int lookupError=0,pointError=0;
static unsigned makedev(unsigned major,unsigned minor){return major<<16|minor;}
static unsigned vfs_flags(mount_t m){return m->flags;}
static Stat *vfs_statfs(mount_t m){static Stat s;s.f_fstypename=m->type;return &s;}
static unsigned kcm_device(mount_t m){return m->device;}
static mount_t vnode_mount(vnode_t v){return v->mount;}
static bool vnode_isdir(vnode_t v){return v->directory;}
static bool kcm_mount_readonly(mount_t m){return m&&(m->flags&MNT_RDONLY)&&!strcmp(m->type,"apfs");}
static void *vfs_context_current(){return nullptr;}
static int vnode_lookup(const char *p,int f,vnode_t *v,void *){assert(!strcmp(p,"/private/var")&&f==1);*v=&point;return pointError;}
static void vnode_put(vnode_t v){assert(v==&point);putCount++;}
static int kebab_lookup_apfs_live_data(kebab_volume_identity *s,kebab_volume_identity *d){*s={1,2};*d={1,targetMinor};return lookupError;}
/* Data dev_t minors are allocated at runtime. Do not confuse the APFS volume
 * BSD name with its current device number. UUID/backing and mounted-vnode
 * identity remain mandatory; a different live volume is never accepted. */
static int
kcm_data_mount_identity(mount_t data)
{
 if(!data||!kcm_system||data==kcm_system||
     !(vfs_flags(data)&MNT_CPROTECT)||strcmp(vfs_statfs(data)->f_fstypename,"apfs"))return EAUTH;
 struct kebab_volume_identity system,target;
 int error=kebab_lookup_apfs_live_data(&system,&target);if(error)return error;
 if(kcm_device(data)!=makedev(target.major_number,target.minor_number)||
     vnode_mount(rootvnode)!=kcm_system||!kcm_mount_readonly(kcm_system)||
     kcm_device(kcm_system)!=makedev(system.major_number,system.minor_number))return EAUTH;
 vnode_t point=NULLVP;
 error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&point,vfs_context_current());
 if(error)return error;
 if(!vnode_isdir(point)||vnode_mount(point)!=data)error=EAUTH;
 vnode_put(point);return error;
}

int main(){
 for(unsigned minor=3;minor<256;minor++){
  targetMinor=minor;data.device=makedev(1,minor);point.mount=&data;
  unsigned before=putCount;assert(!kcm_data_mount_identity(&data)&&putCount==before+1);
  data.device=makedev(1,minor+1);assert(kcm_data_mount_identity(&data)==EAUTH);
 }
 data.device=makedev(1,targetMinor);assert(!kcm_data_mount_identity(&data));
 lookupError=EAUTH;assert(kcm_data_mount_identity(&data)==EAUTH);lookupError=0;
 point.mount=&other;assert(kcm_data_mount_identity(&data)==EAUTH);point.mount=&data;
 point.directory=false;assert(kcm_data_mount_identity(&data)==EAUTH);point.directory=true;
 pointError=5;assert(kcm_data_mount_identity(&data)==5);pointError=0;
 data.flags=0;assert(kcm_data_mount_identity(&data)==EAUTH);data.flags=2;
 data.type="hfs";assert(kcm_data_mount_identity(&data)==EAUTH);data.type="apfs";
 root.flags=0;assert(kcm_data_mount_identity(&data)==EAUTH);root.flags=1;
 root.device++;assert(kcm_data_mount_identity(&data)==EAUTH);root.device--;
 rootnode.mount=&other;assert(kcm_data_mount_identity(&data)==EAUTH);rootnode.mount=&root;
 assert(kcm_data_mount_identity(nullptr)==EAUTH&&kcm_data_mount_identity(&root)==EAUTH);
 printf("PASS actual Data identity helper:253 valid runtime minors, stale device/UUID/mount/root/class-policy refusals; registry/VFS explicit stand-ins\n");
}
