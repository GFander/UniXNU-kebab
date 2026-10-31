from pathlib import Path
import sys
r=Path(__file__).parent
old=(r.parent/'apfs-probe/mount-path-host.c').read_text()
prefix=old[:old.index('int\nkebab_apfs_probe_prepare(')]
prefix=prefix.replace('#include <assert.h>','#include <assert.h>\n#include <stdbool.h>',1)
prefix=prefix.replace('static uint32_t test_mode;','static uint32_t test_mode,test_rw;')
prefix=prefix.replace(':scenario==3;',':test_rw;')
prefix=prefix.replace('a->flags==MNT_RDONLY','a->flags==(test_mode==2?0:MNT_RDONLY)')
source=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'after/bsd/kern/kebab_apfs_probe.c').read_text();a=source.index('int\nkebab_apfs_probe_prepare(');b=source.rindex('\n#endif')
mocks=r'''
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
'''
main=r'''
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
'''
(r/'mount-gates-host.c').write_text(prefix+mocks+source[a:b]+main,newline='\n')
print('Generated production mount/gate harness')
