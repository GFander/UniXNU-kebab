
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
#define KEBAB_APFS_PROOF_SIZE 65559u
#define KEBAB_APFS_PROOF_PATH "/private/preboot/.hios-apfs-proof-v1"
extern int kebab_ufs_sync(void);

static uint8_t
kebab_apfs_proof_byte(uint64_t i)
{
 return (uint8_t)(17 * i + (i >> 8) + 0x5a);
}

static int
kebab_apfs_proof_file(mount_t expected_mount, bool write_file)
{
 vfs_context_t ctx = vfs_context_current();
 uint8_t *buffer = kalloc_data(4096, Z_WAITOK);
 if (!buffer) return ENOMEM;
 vnode_t file = NULLVP;
 int error = 0, close_error;
 if (write_file) {
  error = vnode_open(KEBAB_APFS_PROOF_PATH, O_CREAT | O_EXCL | O_NOFOLLOW | FWRITE, 0600, 0, &file, ctx);
  if (error) goto done;
  if (!vnode_isreg(file) || vnode_mount(file) != expected_mount) { error = EINVAL; goto close_write; }
  for (uint64_t off = 0; off < KEBAB_APFS_PROOF_SIZE; ) {
   int count = KEBAB_APFS_PROOF_SIZE - off > 4096 ? 4096 : (int)(KEBAB_APFS_PROOF_SIZE - off);
   for (int i = 0; i < count; i++) buffer[i] = kebab_apfs_proof_byte(off + i);
   int residual = 0;
   error = vn_rdwr(UIO_WRITE, file, (caddr_t)buffer, count, (off_t)off, UIO_SYSSPACE, IO_SYNC,
       vfs_context_ucred(ctx), &residual, NULL);
   if (!error && residual) error = EIO;
   if (error) break;
   off += count;
  }
  { int fsync_error = VNOP_FSYNC(file, MNT_WAIT, ctx); if (!error) error = fsync_error; }
close_write:
  close_error = vnode_close(file, FWRITE, ctx);
  file = NULLVP;
  if (!error) error = close_error;
  if (error) goto done;
 }
 error = vnode_open(KEBAB_APFS_PROOF_PATH, O_NOFOLLOW | FREAD, 0, 0, &file, ctx);
 if (error) goto done;
 if (!vnode_isreg(file) || vnode_mount(file) != expected_mount) { error = EINVAL; goto close_read; }
 struct vnode_attr attributes;
 VATTR_INIT(&attributes);
 VATTR_WANTED(&attributes, va_data_size);
 error = vnode_getattr(file, &attributes, ctx);
 if (!error && attributes.va_data_size != KEBAB_APFS_PROOF_SIZE) error = EINVAL;
 if (error) goto close_read;
 SHA256_CTX hash;
 uint8_t digest[SHA256_DIGEST_LENGTH];
 SHA256_Init(&hash);
 for (uint64_t off = 0; off < KEBAB_APFS_PROOF_SIZE; ) {
  int count = KEBAB_APFS_PROOF_SIZE - off > 4096 ? 4096 : (int)(KEBAB_APFS_PROOF_SIZE - off);
  int residual = 0;
  error = vn_rdwr(UIO_READ, file, (caddr_t)buffer, count, (off_t)off, UIO_SYSSPACE, 0,
      vfs_context_ucred(ctx), &residual, NULL);
  if (!error && residual) error = EIO;
  if (error) break;
  for (int i = 0; i < count; i++) if (buffer[i] != kebab_apfs_proof_byte(off + i)) { error = EIO; break; }
  if (error) break;
  SHA256_Update(&hash, buffer, count);
  off += count;
 }
 if (!error) {
  static const uint8_t expected[SHA256_DIGEST_LENGTH] = {0x3f,0x2f,0x47,0xb4,0x94,0x55,0xf2,0xd5,0xcc,0xe7,0xd7,0x45,0x82,0x94,0x40,0xcc,0xf0,0xe8,0xf1,0xbe,0xda,0x6c,0x51,0x20,0x64,0x1c,0x25,0x21,0x3f,0x78,0xc9,0x57};
  SHA256_Final(digest, &hash);
  if (memcmp(digest, expected, sizeof(digest))) error = EIO;
  printf("KEBAB APFS PROOF: %s bytes=%u sha256=", write_file ? "write/read" : "reboot-read", KEBAB_APFS_PROOF_SIZE);
  for (unsigned i = 0; i < sizeof(digest); i++) printf("%02x", digest[i]);
  printf(" %s\n", error ? "FAIL" : "PASS");
 }
close_read:
 close_error = vnode_close(file, FREAD, ctx);
 file = NULLVP;
 if (!error) error = close_error;
done:
 kfree_data(buffer, 4096);
 return error;
}



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
