
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define KEBAB 1
#define MNT_RDONLY 1
#define MNT_CPROTECT 2
#define MNT_WAIT 1
#define PROTECTION_CLASS_B 2
#define NODEV (-1)
typedef int dev_t;
typedef void *vfs_context_t;
struct vfsstatfs{char f_fstypename[8],f_mntonname[32];};
struct mount{struct vfsstatfs stat;unsigned flags;dev_t device;};typedef struct mount*mount_t;
struct node{mount_t mount;bool reg;dev_t dev;};typedef struct node*vnode_t;
static struct node rootnode,device;static vnode_t rootvnode=&rootnode;
struct vfs_attr{uint8_t f_uuid[16];bool supported;};
struct vnode_attr{unsigned va_dataprotect_class;bool supported;};
struct kebab_volume_identity{unsigned major_number,minor_number;uint64_t backing_id;char bsd_name[32];};
#define VFSATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VFSATTR_WANTED(p,f) ((void)(p))
#define VFSATTR_IS_SUPPORTED(p,f) ((p)->supported)
#define VATTR_INIT VFSATTR_INIT
#define VATTR_WANTED VFSATTR_WANTED
#define VATTR_IS_SUPPORTED VFSATTR_IS_SUPPORTED
static unsigned calls_sync,calls_recycle,calls_lookup,calls_put;static int volume_error,attribute_error,sync_error,lookup_error,busy;
static bool seen,volume_support,class_support,volume_match;static unsigned cls;
static bool kebab_apfs_b_volume_seen(void){return seen;}
static bool vnode_isreg(vnode_t v){return v->reg;}
static mount_t vnode_mount(vnode_t v){return v->mount;}
static struct vfsstatfs*vfs_statfs(mount_t m){return &m->stat;}
static unsigned vfs_flags(mount_t m){return m->flags;}
static int vfs_getattr(mount_t m,struct vfs_attr*a,vfs_context_t c){(void)m;(void)c;a->supported=volume_support;return volume_error;}
static bool kebab_apfs_b_volume_matches(const uint8_t v[16]){(void)v;return volume_match;}
static int vnode_getattr(vnode_t v,struct vnode_attr*a,vfs_context_t c){(void)v;(void)c;a->supported=class_support;a->va_dataprotect_class=cls;return attribute_error;}
static int vnode_isinuse(vnode_t v,int n){(void)v;(void)n;return busy;}
static int VNOP_FSYNC(vnode_t v,int n,vfs_context_t c){(void)v;(void)c;assert(n==MNT_WAIT);calls_sync++;return sync_error;}
static int vnode_recycle(vnode_t v){(void)v;calls_recycle++;return 0;/* deferred because caller owns iocount */}
static int kebab_lookup_apfs_live_data(struct kebab_volume_identity*s,struct kebab_volume_identity*d){calls_lookup++;s->major_number=1;s->minor_number=2;d->major_number=1;d->minor_number=4;return lookup_error;}
static vnode_t vfs_devvp(mount_t m){device.dev=m->device;return &device;}
static dev_t vnode_specrdev(vnode_t v){return v->dev;}
static void vnode_put(vnode_t v){assert(v==&device);calls_put++;}
static dev_t makedev(unsigned a,unsigned b){return (int)((a<<8)|b);}
/* Software CPX class-B lifecycle for the one authenticated HiOS Data volume.
 * Called after VNOP_CLOSE and its use-reference drop, while the caller still
 * owns an iocount. Recycle marks a live vnode; standard VFS reclaim waits for
 * other descriptors/mappings/I/O, then original APFS frees its CPX objects.
 * Native cpx_zone/AES-context zones clear their memory on free. */
#if defined(KEBAB)

int kebab_b_file_close(vnode_t,vfs_context_t);
int
kebab_b_file_close(vnode_t vnode,vfs_context_t context)
{
 if(!vnode||!kebab_apfs_b_volume_seen()||!vnode_isreg(vnode))return 0;
 mount_t mount=vnode_mount(vnode);if(!mount)return 0;
 const struct vfsstatfs *stat=vfs_statfs(mount);
 if(!stat||strcmp(stat->f_fstypename,"apfs")||!(vfs_flags(mount)&MNT_CPROTECT)||
     strcmp(stat->f_mntonname,"/private/var"))return 0;
 struct vfs_attr volume;VFSATTR_INIT(&volume);VFSATTR_WANTED(&volume,f_uuid);
 int volume_error=vfs_getattr(mount,&volume,context);
 bool identified=!volume_error&&VFSATTR_IS_SUPPORTED(&volume,f_uuid);
 if(identified){if(!kebab_apfs_b_volume_matches(volume.f_uuid))return 0;}
 else{
  /* The mount path alone is not identity. On attribute failure, resolve
   * the pinned live Data/System IOMedia UUIDs and common backing, then
   * compare both runtime dev_t values and the original readonly root. */
  struct kebab_volume_identity system,data;
  if(kebab_lookup_apfs_live_data(&system,&data)||!rootvnode)return 0;
  mount_t root=vnode_mount(rootvnode);
  if(!root||root==mount||!(vfs_flags(root)&MNT_RDONLY))return 0;
  vnode_t device=vfs_devvp(mount);dev_t actual=device?vnode_specrdev(device):NODEV;
  if(device)vnode_put(device);
  if(actual!=makedev(data.major_number,data.minor_number))return 0;
  device=vfs_devvp(root);actual=device?vnode_specrdev(device):NODEV;
  if(device)vnode_put(device);
  if(actual!=makedev(system.major_number,system.minor_number))return 0;
 }
 struct vnode_attr attributes;VATTR_INIT(&attributes);VATTR_WANTED(&attributes,va_dataprotect_class);
 int attribute_error=vnode_getattr(vnode,&attributes,context);
 bool class_known=!attribute_error&&VATTR_IS_SUPPORTED(&attributes,va_dataprotect_class);
 if(class_known&&attributes.va_dataprotect_class!=PROTECTION_CLASS_B)return 0;
 /* Keep pending writes and open mappings usable. If this is the last user,
  * sync successfully before asking VFS to reclaim; preserve real errors.
  * A marked but still-used vnode is reclaimed after its final reference. */
 int busy=vnode_isinuse(vnode,0),sync_error=0;
 if(!busy&&!(vfs_flags(mount)&MNT_RDONLY))sync_error=VNOP_FSYNC(vnode,MNT_WAIT,context);
 if(sync_error){printf("KEBAB B CLOSE: sync-error=%d; raw key retained for pending I/O\n",sync_error);return sync_error;}
 int immediate=vnode_recycle(vnode);
 static uint32_t reports;uint32_t n=__atomic_fetch_add(&reports,1u,__ATOMIC_RELAXED);
 if(n<64)printf("KEBAB B CLOSE: class-known=%u class=%u busy=%d volume-error=%d attr-error=%d recycle-immediate=%d; final VFS release owns reclaim\n",
     class_known?1u:0u,class_known?attributes.va_dataprotect_class:0u,busy,volume_error,attribute_error,immediate);
 return 0;
}
#endif

static struct mount data,system;static struct node file;
static void setup(void){
 memset(&data,0,sizeof(data));memset(&system,0,sizeof(system));strcpy(data.stat.f_fstypename,"apfs");strcpy(data.stat.f_mntonname,"/private/var");data.flags=MNT_CPROTECT;data.device=makedev(1,4);system.flags=MNT_RDONLY;system.device=makedev(1,2);rootnode.mount=&system;
 file=(struct node){&data,true,0};seen=volume_support=class_support=volume_match=true;cls=2;volume_error=attribute_error=sync_error=lookup_error=busy=0;calls_sync=calls_recycle=calls_lookup=calls_put=0;
}
static void no_action(void){assert(!calls_sync&&!calls_recycle);}
int main(void){unsigned cases=0;
 setup();assert(!kebab_b_file_close(&file,NULL)&&calls_sync==1&&calls_recycle==1&&!calls_lookup);cases++;
 setup();busy=1;assert(!kebab_b_file_close(&file,NULL)&&!calls_sync&&calls_recycle==1);cases++;
 setup();data.flags|=MNT_RDONLY;assert(!kebab_b_file_close(&file,NULL)&&!calls_sync&&calls_recycle==1);cases++;
 setup();sync_error=5;assert(kebab_b_file_close(&file,NULL)==5&&calls_sync==1&&!calls_recycle);cases++;
 for(unsigned c=0;c<32;c++)if(c!=2){setup();cls=c;assert(!kebab_b_file_close(&file,NULL));no_action();cases++;}
 setup();attribute_error=5;assert(!kebab_b_file_close(&file,NULL)&&calls_recycle==1);cases++;
 setup();class_support=false;assert(!kebab_b_file_close(&file,NULL)&&calls_recycle==1);cases++;
 setup();volume_error=5;assert(!kebab_b_file_close(&file,NULL)&&calls_recycle==1&&calls_lookup==1&&calls_put==2);cases++;
 setup();volume_support=false;assert(!kebab_b_file_close(&file,NULL)&&calls_recycle==1&&calls_lookup==1);cases++;
 for(unsigned fault=0;fault<14;fault++){
  setup();
  switch(fault){case 0:seen=false;break;case 1:file.reg=false;break;case 2:file.mount=NULL;break;case 3:strcpy(data.stat.f_fstypename,"hfs");break;case 4:data.flags=0;break;case 5:strcpy(data.stat.f_mntonname,"/fake");break;case 6:volume_match=false;break;case 7:volume_error=5;lookup_error=80;break;case 8:volume_error=5;rootnode.mount=NULL;break;case 9:volume_error=5;rootnode.mount=&data;break;case 10:volume_error=5;system.flags=0;break;case 11:volume_error=5;data.device=makedev(1,3);break;case 12:volume_error=5;system.device=makedev(2,2);break;case 13:volume_error=5;rootvnode=NULL;break;}
  assert(!kebab_b_file_close(&file,NULL));no_action();rootvnode=&rootnode;cases++;
 }
 setup();assert(!kebab_b_file_close(NULL,NULL));no_action();cases++;
 printf("PASS %u actual B close policy cases; APFS/class/UUID/runtime dev_t/root identity, sync errors, deferred recycle; native VFS/reference teardown explicitly modeled, not physical proof\n",cases);
}
