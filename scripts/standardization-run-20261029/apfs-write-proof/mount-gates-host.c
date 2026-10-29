
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define MNT_RDONLY 1
#define NODEV UINT32_MAX
#define NULLVP NULL
#define dev_t uint32_t
/* Match Darwin's uint64_t format type on this Linux host. */
#define uint64_t unsigned long long
typedef uintptr_t user_addr_t,user64_addr_t;
typedef void *proc_t;
struct fake_statfs { char f_fstypename[16]; };
struct fake_mount { unsigned flags; struct fake_statfs stat; uint32_t device; };
typedef struct fake_mount *mount_t;
struct fake_node { mount_t mp; int held; };
typedef struct fake_node *vnode_t;
struct mount_args {uintptr_t type,path;unsigned flags;uintptr_t data;};
static struct fake_mount root_mount,data_mount;
static struct fake_node root_node,root_dev,data_node,data_dev;
static vnode_t rootvnode=&root_node;
static int scenario,mount_calls,copy_calls;
static uint32_t test_mode,test_rw;
static unsigned char *scratch_base;
#define makedev(a,b) (((uint32_t)(a)<<24)|(b))
#define major(d) ((d)>>24)
#define minor(d) ((d)&0xffffff)
struct kebab_volume_identity {
 uint32_t major_number, minor_number;
 uint64_t backing_id;
 char bsd_name[32];
};

static int PE_parse_boot_argn(const char *s,void *p,size_t n){
 assert(n==4);*(uint32_t*)p=!strcmp(s,"kebab_apfs_probe")?test_mode:test_rw;return 1;
}
static mount_t vnode_mount(vnode_t v){return v->mp;}
static unsigned vfs_flags(mount_t m){return m->flags;}
static struct fake_statfs *vfs_statfs(mount_t m){return &m->stat;}
static vnode_t vfs_devvp(mount_t m){vnode_t v=m==&root_mount?&root_dev:&data_dev;v->held++;return v;}
static dev_t vnode_specrdev(vnode_t v){return v->mp->device;}
static void vnode_put(vnode_t v){assert(v->held>0);v->held--;}
static void *vfs_context_current(void){return NULL;}
static int vnode_lookup(const char *p,int flags,vnode_t *v,void *ctx){
 (void)flags;(void)ctx;assert(!strcmp(p,"/private/preboot"));if(scenario==10)return ENOENT;
 *v=&data_node;data_node.held++;return 0;
}
static int kebab_lookup_apfs_preboot(struct kebab_volume_identity *s,struct kebab_volume_identity *d){
 memset(s,0,sizeof(*s));memset(d,0,sizeof(*d));if(scenario==6)return ENOENT;
 s->major_number=d->major_number=1;s->minor_number=2;d->minor_number=3;s->backing_id=d->backing_id=0x123;
 strcpy(s->bsd_name,"disk1s1");strcpy(d->bsd_name,"disk1s2");return 0;
}
static int copyout(const void *from,uintptr_t to,size_t n){
 ++copy_calls;if(scenario==8)return EFAULT;
 assert(to>=(uintptr_t)scratch_base && to+n<=(uintptr_t)scratch_base+4096);
 memcpy((void*)to,from,n);return 0;
}
static int mount(proc_t p,struct mount_args *a,int32_t *rv){
 (void)p;(void)rv;++mount_calls;
 assert(!strcmp((char*)a->type,"apfs")&&!strcmp((char*)a->path,"/private/preboot")&&a->flags==(test_mode==2?0:MNT_RDONLY));
 uint8_t *payload=(void*)a->data;uintptr_t spec;uint16_t mode;uint32_t uid,gid;
 memcpy(&spec,payload,8);memcpy(&mode,payload+16,2);memcpy(&uid,payload+0x144,4);memcpy(&gid,payload+0x148,4);
 assert(!strcmp((char*)spec,"/dev/disk1s2")&&mode==1&&uid==UINT32_MAX&&gid==UINT32_MAX);
 if(scenario==9)return EACCES;
 if(scenario==11)data_mount.device=makedev(1,4);
 if(scenario==12)data_mount.flags=0;
 if(scenario==13)strcpy(data_mount.stat.f_fstypename,"hfs");
 if(scenario==14)data_node.mp=&root_mount;
 if(scenario==15)root_mount.flags=0;
 return 0;
}

static int proof_calls,sync_calls,ufs_calls,was_write;
#define MNT_WAIT 1
static int kebab_apfs_proof_file(mount_t m,bool w){assert(m==&data_mount);proof_calls++;was_write=w;return scenario==17?EIO:0;}
static int VFS_SYNC(mount_t m,int wait,void *ctx){assert(m==&data_mount&&wait==1&&!ctx);sync_calls++;return scenario==18?ENOSPC:0;}
static int kebab_ufs_sync(void){ufs_calls++;return scenario==19?EIO:0;}
static void reset(unsigned mode,unsigned rw,int sc){
 scenario=sc;test_mode=mode;test_rw=rw;mount_calls=copy_calls=proof_calls=sync_calls=ufs_calls=was_write=0;
 root_mount=(struct fake_mount){.flags=1,.stat={"apfs"},.device=makedev(1,2)};
 data_mount=(struct fake_mount){.flags=mode==2?0:1,.stat={"apfs"},.device=makedev(1,3)};
 root_node=(struct fake_node){&root_mount,0};root_dev=(struct fake_node){&root_mount,0};
 data_node=(struct fake_node){&data_mount,0};data_dev=(struct fake_node){&data_mount,0};
 if(sc==4)root_mount.flags=0;if(sc==5)strcpy(root_mount.stat.f_fstypename,"hfs");
 if(sc==7)root_mount.device=makedev(1,7);
}
int
kebab_apfs_probe_prepare(proc_t p, user_addr_t scratch)
{
 uint32_t mode = 0, rw = 0;
 if (!PE_parse_boot_argn("kebab_apfs_probe", &mode, sizeof(mode)) || !mode) return 0;
 if (mode > 3 || !scratch) return EINVAL;
 bool write_file = (mode == 2);
 PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));
 if ((write_file && rw != 1) || (!write_file && rw)) return EINVAL;
 mount_t original_root = vnode_mount(rootvnode);
 if (!(vfs_flags(original_root) & MNT_RDONLY) || strcmp(vfs_statfs(original_root)->f_fstypename, "apfs")) return EINVAL;
 struct kebab_volume_identity system, data;
 int error = kebab_lookup_apfs_preboot(&system, &data);
 if (error) { printf("KEBAB APFS PROBE: identity lookup error=%d\n", error); return error; }
 vnode_t root_device = vfs_devvp(original_root);
 dev_t root_dev = root_device ? vnode_specrdev(root_device) : NODEV;
 if (root_device) vnode_put(root_device);
 if (root_dev != makedev(system.major_number, system.minor_number)) return EINVAL;
 char path[64];
 snprintf(path, sizeof(path), "/dev/%s", data.bsd_name);
 printf("KEBAB APFS PROBE: target uuid=EF45F98E-9F6C-43DB-B7C6-F418CA77429B path=%s dev=%u/%u backing=%llx System=%u/%u readonly=%u\n",
     path, data.major_number, data.minor_number, data.backing_id, system.major_number, system.minor_number, !write_file);
 /* Structure offsets recovered from this target's original mount_apfs.
  * The descriptor is copied to the current init task's scratch page;
  * APFS's normal mount syscall consumes user pointers from that page. */
 uint8_t payload[0x150] = {0};
 user64_addr_t spec = scratch + 0x160;
 uint16_t normal_mount = 1;
 uint32_t anonymous_owner = UINT32_MAX;
 memcpy(payload, &spec, sizeof(spec));
 memcpy(payload + 16, &normal_mount, sizeof(normal_mount));
 memcpy(payload + 0x144, &anonymous_owner, sizeof(anonymous_owner));
 memcpy(payload + 0x148, &anonymous_owner, sizeof(anonymous_owner));
 struct mount_args request = { .type = scratch + 0x100, .path = scratch + 0x120,
     .flags = write_file ? 0 : MNT_RDONLY, .data = scratch + 0x200 };
 int32_t result = 0;
 if ((error = copyout("apfs", request.type, 5)) ||
     (error = copyout("/private/preboot", request.path, 17)) ||
     (error = copyout(path, spec, strlen(path) + 1)) ||
     (error = copyout(payload, request.data, sizeof(payload)))) return error;
 error = mount(p, &request, &result);
 printf("KEBAB APFS PROBE: mount syscall error=%d\n", error);
 if (error) return error;
 vnode_t data_root = NULLVP;
 error = vnode_lookup("/private/preboot", 0, &data_root, vfs_context_current());
 if (error) return error;
 mount_t mounted = vnode_mount(data_root);
 vnode_t device = vfs_devvp(mounted);
 dev_t actual = device ? vnode_specrdev(device) : NODEV;
 if (device) vnode_put(device);
 if (actual != makedev(data.major_number, data.minor_number) || mounted == original_root ||
     ((vfs_flags(mounted) & MNT_RDONLY) != 0) == write_file || strcmp(vfs_statfs(mounted)->f_fstypename, "apfs") ||
     vnode_mount(rootvnode) != original_root || !(vfs_flags(original_root) & MNT_RDONLY)) error = EINVAL;
 printf("KEBAB APFS PROBE: mounted dev=%u/%u readonly=%u System-readonly=%u identity=%s\n",
     major(actual), minor(actual), (vfs_flags(mounted) & MNT_RDONLY) != 0,
     (vfs_flags(original_root) & MNT_RDONLY) != 0, error ? "FAIL" : "PASS");
 if (!error && mode != 1) error = kebab_apfs_proof_file(mounted, write_file);
 if (write_file) {
  /* Flush even after a failed file operation, before a diagnostic panic. */
  int filesystem_error = VFS_SYNC(mounted, MNT_WAIT, vfs_context_current());
  int device_error = kebab_ufs_sync();
  printf("KEBAB APFS PROOF: durable flush filesystem=%d UFS=%d\n", filesystem_error, device_error);
  if (!error) error = filesystem_error ? filesystem_error : device_error;
 }
 vnode_put(data_root);
 return error;
}
int main(void){
 uint8_t scratch[4096]={0};scratch_base=scratch;
 const int expected[]={0,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL,ENOENT,EINVAL,EFAULT,EACCES,ENOENT,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL};
 for(int sc=0;sc<16;sc++){
  reset(sc==1?2:1,sc==3,sc);
  assert(kebab_apfs_probe_prepare(NULL,sc==2?0:(uintptr_t)scratch)==expected[sc]);
  assert(!root_dev.held&&!data_node.held&&!data_dev.held);
  assert(!proof_calls&&!sync_calls&&!ufs_calls);
 }
 const struct {unsigned mode,rw;int sc,expected;} cases[]={
  {2,1,0,0},{2,1,17,EIO},{2,1,18,ENOSPC},{2,1,19,EIO},
  {3,0,0,0},{2,0,0,EINVAL},{2,2,0,EINVAL},{3,1,0,EINVAL},{4,0,0,EINVAL},{0,0,0,0}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  reset(cases[i].mode,cases[i].rw,cases[i].sc);
  assert(kebab_apfs_probe_prepare(NULL,(uintptr_t)scratch)==cases[i].expected);
  assert(!root_dev.held&&!data_node.held&&!data_dev.held);
  if(i<4)assert(proof_calls==1&&was_write&&sync_calls==1&&ufs_calls==1);
  else if(i==4)assert(proof_calls==1&&!was_write&&!sync_calls&&!ufs_calls);
  else assert(!mount_calls&&!proof_calls&&!sync_calls&&!ufs_calls);
 }
 puts("PASS: production probe mount/gate path, 26 identity/RO/RW/refusal/durability-error cases");
 return 0;
}
