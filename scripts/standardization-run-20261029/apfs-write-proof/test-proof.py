"""Compile production fixed-file proof against fault-injected VFS and real SHA256."""
from pathlib import Path
import sys
r=Path(__file__).parent;s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'after/bsd/kern/kebab_apfs_probe.c').read_text()
body=s[s.index('#define KEBAB_APFS_PROOF_SIZE'):s.index('\nint kebab_apfs_probe_prepare')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <openssl/sha.h>
#define SHA256_DIGEST_LENGTH 32
#define uint64_t unsigned long long
#define NULLVP NULL
#define FREAD 1
#define FWRITE 2
#define Z_WAITOK 1
#define MNT_WAIT 1
#define UIO_WRITE 1
#define UIO_READ 0
#define UIO_SYSSPACE 1
#define IO_SYNC 1
typedef char *caddr_t;
typedef void *vfs_context_t;
typedef int *mount_t;
struct node { int held; };
typedef struct node *vnode_t;
struct vnode_attr { uint64_t va_data_size; };
#define VATTR_INIT(a) memset(a,0,sizeof(*(a)))
#define VATTR_WANTED(a,b) ((void)(a))
static int expected_mount,wrong_mount,scenario,exists,write_calls,read_calls,close_calls,fsync_calls,allocs,frees;
static size_t file_size;
static unsigned char file_data[65559];
static struct node object;
static void *kalloc_data(size_t n,int f){assert(n==4096&&f==Z_WAITOK);if(scenario==1)return NULL;allocs++;return malloc(n);}
static void kfree_data(void *p,size_t n){assert(p&&n==4096);frees++;free(p);}
static vfs_context_t vfs_context_current(void){return NULL;}
static void *vfs_context_ucred(vfs_context_t c){return c;}
static int vnode_open(const char *p,int flags,int mode,int unused,vnode_t *v,vfs_context_t ctx){
 (void)ctx;assert(!strcmp(p,"/private/preboot/.hios-apfs-proof-v1")&&!unused&&(flags&O_NOFOLLOW));
 bool w=flags&FWRITE;
 if((scenario==3&&w)||(scenario==10&&!w))return EACCES;
 if(w){assert((flags&O_EXCL)&&(flags&O_CREAT)&&mode==0600);if(exists)return EEXIST;exists=1;file_size=0;}
 else if(!exists)return ENOENT;
 assert(!object.held);object.held=1;*v=&object;return 0;
}
static bool vnode_isreg(vnode_t v){assert(v==&object&&object.held);return scenario!=5;}
static mount_t vnode_mount(vnode_t v){assert(v==&object&&object.held);return scenario==4?&wrong_mount:&expected_mount;}
static int vnode_getattr(vnode_t v,struct vnode_attr *a,vfs_context_t c){
 (void)c;assert(v==&object&&object.held);a->va_data_size=scenario==12?17:file_size;return scenario==11?EIO:0;
}
static int vnode_close(vnode_t v,int flags,vfs_context_t c){
 (void)c;assert(v==&object&&object.held);object.held=0;close_calls++;
 return (scenario==9&&(flags&FWRITE))||(scenario==16&&(flags&FREAD))?EIO:0;
}
static int VNOP_FSYNC(vnode_t v,int wait,vfs_context_t c){(void)c;assert(v==&object&&object.held&&wait==MNT_WAIT);fsync_calls++;return scenario==8?EIO:0;}
static int vn_rdwr(int direction,vnode_t v,caddr_t b,int n,off_t off,int seg,int flags,void *cred,int *resid,void *p){
 (void)cred;(void)p;assert(v==&object&&object.held&&n>0&&n<=4096&&off>=0&&off+n<=65559&&seg==UIO_SYSSPACE);
 *resid=0;
 if(direction==UIO_WRITE){
  assert(flags==IO_SYNC);write_calls++;
  if(scenario==6&&write_calls==2)return EIO;
  if(scenario==7&&write_calls==2){*resid=1;return 0;}
  memcpy(file_data+off,b,n);file_size=off+n;
 }else{
  assert(direction==UIO_READ&&!flags);read_calls++;
  if(scenario==13&&read_calls==2)return EIO;
  if(scenario==14&&read_calls==2){*resid=1;return 0;}
  memcpy(b,file_data+off,n);if(scenario==15&&read_calls==2)b[0]^=1;
 }
 return 0;
}
'''
main=r'''
int main(void){
 const int expected[]={0,ENOMEM,EEXIST,EACCES,EINVAL,EINVAL,EIO,EIO,EIO,EIO,EACCES,EIO,EINVAL,EIO,EIO,EIO,EIO,0,ENOENT};
 for(scenario=0;scenario<19;scenario++){
  exists=scenario==2||scenario==17;file_size=exists?65559:0;
  for(unsigned i=0;i<65559;i++)file_data[i]=kebab_apfs_proof_byte(i);
  write_calls=read_calls=close_calls=fsync_calls=allocs=frees=object.held=0;
  assert(kebab_apfs_proof_file(&expected_mount,scenario<17)==expected[scenario]);
  assert(!object.held&&allocs==frees);
  if(scenario==0)assert(write_calls==17&&read_calls==17&&fsync_calls==1&&close_calls==2&&file_size==65559);
  if(scenario==2||scenario==3||scenario==4||scenario==5)assert(!write_calls);
  if(scenario>=17)assert(!write_calls&&!fsync_calls);
  if(scenario==17)assert(read_calls==17&&close_calls==1);
 }
 puts("PASS: production fixed-file proof, real SHA256, 19 write/read/failure/ownership cases");
 return 0;
}
'''
(r/'proof-host.c').write_text(prefix+body+main,newline='\n')
print('Generated production fixed-file proof harness')
