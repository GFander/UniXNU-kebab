"""Fault-inject the production Data mount path without performing any disk I/O."""
from pathlib import Path
r=Path(__file__).parent;s=(r/'after/bsd/kern/kebab_data.c').read_text()
start=s.index('int\nkebab_data_prepare(');end=s.rindex('\n#endif')
header=(r/'after/bsd/kern/kebab_data.h').read_text();a=header.index('struct kebab_volume_identity');z=header.index('\n};',a)+3
prefix=r'''
#include <assert.h>
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
static uint32_t test_mode;
static unsigned char *scratch_base;
#define makedev(a,b) (((uint32_t)(a)<<24)|(b))
#define major(d) ((d)>>24)
#define minor(d) ((d)&0xffffff)
'''
mocks=r'''
static int PE_parse_boot_argn(const char *s,void *p,size_t n){
 assert(n==4);*(uint32_t*)p=!strcmp(s,"kebab_data_mount")?test_mode:scenario==3;return 1;
}
static mount_t vnode_mount(vnode_t v){return v->mp;}
static unsigned vfs_flags(mount_t m){return m->flags;}
static struct fake_statfs *vfs_statfs(mount_t m){return &m->stat;}
static vnode_t vfs_devvp(mount_t m){vnode_t v=m==&root_mount?&root_dev:&data_dev;v->held++;return v;}
static dev_t vnode_specrdev(vnode_t v){return v->mp->device;}
static void vnode_put(vnode_t v){assert(v->held>0);v->held--;}
static void *vfs_context_current(void){return NULL;}
static int vnode_lookup(const char *p,int flags,vnode_t *v,void *ctx){
 (void)flags;(void)ctx;assert(!strcmp(p,"/private/var"));if(scenario==10)return ENOENT;
 *v=&data_node;data_node.held++;return 0;
}
static int kebab_lookup_apfs_data(struct kebab_volume_identity *s,struct kebab_volume_identity *d){
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
 assert(!strcmp((char*)a->type,"apfs")&&!strcmp((char*)a->path,"/private/var")&&a->flags==MNT_RDONLY);
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
'''
main=r'''
int main(void){
 const int expected[]={0,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL,ENOENT,EINVAL,EFAULT,EACCES,ENOENT,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL};
 for(scenario=0;scenario<16;scenario++){
  uint8_t scratch[4096]={0};scratch_base=scratch;test_mode=scenario==1?2:1;mount_calls=copy_calls=0;
  root_mount=(struct fake_mount){.flags=1,.stat={"apfs"},.device=makedev(1,2)};
  data_mount=(struct fake_mount){.flags=1,.stat={"apfs"},.device=makedev(1,3)};
  root_node=(struct fake_node){&root_mount,0};root_dev=(struct fake_node){&root_mount,0};
  data_node=(struct fake_node){&data_mount,0};data_dev=(struct fake_node){&data_mount,0};
  if(scenario==4)root_mount.flags=0;
  if(scenario==5)strcpy(root_mount.stat.f_fstypename,"hfs");
  if(scenario==7)root_mount.device=makedev(1,7);
  assert(kebab_data_prepare(NULL,scenario==2?0:(uintptr_t)scratch)==expected[scenario]);
  assert(!root_dev.held&&!data_node.held&&!data_dev.held);
  if(scenario<=8&&scenario)assert(!mount_calls);
  if(!scenario)assert(mount_calls==1&&copy_calls==4);
 }
 test_mode=0;mount_calls=copy_calls=0;
 assert(!kebab_data_prepare(NULL,0)&&!mount_calls&&!copy_calls);
 puts("PASS: production Data mount path; target arguments, gate and 15 fault/refusal cases, vnode references balanced");
 return 0;
}
'''
(r/'mount-path-host.c').write_text(prefix+header[a:z]+'\n'+mocks+s[start:end]+main,newline='\n')
print('Generated production Data mount fault-injection harness')
