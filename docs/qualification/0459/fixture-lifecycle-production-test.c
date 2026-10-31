
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#define EAUTH 80
typedef void *vnode_t;typedef void *mount_t;typedef int vfs_context_t;
struct vnode_attr {uint32_t wanted,supported,va_uid,va_mode,va_flags,va_dataprotect_class;uint64_t va_nlink,va_data_size;};
enum{b_va_uid=1,b_va_mode=2,b_va_nlink=4,b_va_flags=8,b_va_data_size=16,b_va_dataprotect_class=32};
static struct vnode_attr model;
static unsigned kind=1;static mount_t actual=(void *)2;static int getter_error;
#define VATTR_INIT(p) ((p)->wanted=0)
#define VATTR_WANTED(p,f) ((p)->wanted|=b_##f)
#define VATTR_IS_SUPPORTED(p,f) (((p)->supported&b_##f)!=0)
static int vnode_isreg(vnode_t p){assert(p==(void *)1);return kind;}
static mount_t vnode_mount(vnode_t p){assert(p==(void *)1);return actual;}
static int VNOP_GETATTR(vnode_t p,struct vnode_attr *out,vfs_context_t c){assert(p==(void *)1&&c==7&&out->wanted==63);if(!getter_error)*out=model;return getter_error;}
static int observed_printf(const char *f,...){(void)f;return 0;}
#define printf observed_printf

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <openssl/sha.h>
typedef long long daddr64_t;
#define NULLVP NULL
#define Z_WAITOK 1
#define Z_ZERO 2
#define ENOMEM 12
#define EINVAL 22
#define EIO 5
#define FREAD 1
#define FWRITE 2
#define NOFOLLOW 8
#define LOOKUP 1
#define OP_OPEN 2
#define UIO_SYSSPACE 1
#define IO_SYNC 1
#define IO_NOCACHE 2
#define UIO_READ 1
#define UIO_WRITE 2
#define MNT_WAIT 1
#define VNODE_READ 1
#define VNODE_BLOCKMAP_NO_TRACK 2
#define KAUTH_VNODE_ACCESS 1
#define KAUTH_VNODE_WRITE_DATA 2
#define CAST_USER_ADDR_T(p) (p)
#define VATTR_SET(p,f,v) ((p)->f=(v))
#define kalloc_type(t,f) ((t *)calloc(1,sizeof(t)))
#define kfree_type(t,p) free(p)
#define kalloc_data(s,f) calloc(1,s)
#define kfree_data(p,s) free(p)
#define bzero(p,n) memset(p,0,n)
static unsigned tool=12,opens,closes,reads,raws,authorizes,sets,syncs,live;
static int inject;static bool corrupt,rawPlain,ignoreSet,corruptAfterSet;
static vfs_context_t vfs_context_current(void){return 7;}
static int vfs_context_ucred(vfs_context_t c){assert(c==7);return 9;}
static bool PE_parse_boot_argn(const char *n,void *out,size_t s){assert(!strcmp(n,"kebab_apfs_tool")&&s==sizeof(tool));memcpy(out,&tool,s);return true;}
static int vnode_open(const char *path,int flags,int a,int b,vnode_t *out,vfs_context_t c){
 assert(path&&a==0&&b==0&&c==7&&!live&&(flags&O_NOFOLLOW));opens++;if(inject==1)return EIO;live=1;*out=(void *)1;return 0;
}
static int vnode_close(vnode_t p,int flags,vfs_context_t c){assert(p==(void *)1&&live&&c==7&&(flags&FREAD));closes++;live=0;return inject==7?EIO:0;}
static int vnode_authorize(vnode_t p,vnode_t dir,int rights,vfs_context_t c){assert(p==(void *)1&&live&&!dir&&rights==3&&c==7);authorizes++;return inject==4?13:0;}
static int VNOP_SETATTR(vnode_t p,struct vnode_attr *a,vfs_context_t c){assert(p==(void *)1&&live&&c==7&&a->va_dataprotect_class==4);sets++;if(inject==5)return EIO;if(!ignoreSet)model.va_dataprotect_class=4;return 0;}
static int VNOP_FSYNC(vnode_t p,int flags,vfs_context_t c){assert(p==(void *)1&&live&&flags==MNT_WAIT&&c==7);syncs++;return inject==6?EIO:0;}
static int vn_rdwr(int direction,vnode_t p,caddr_t out,int bytes,off_t offset,int segment,int flags,int cred,int *residual,void *unused){
 assert(direction==UIO_READ&&p==(void *)1&&live&&bytes==65559&&offset==0&&segment==UIO_SYSSPACE&&(flags&IO_NOCACHE)&&cred==9&&!unused);
 reads++;if(inject==2)return EIO;*residual=0;for(unsigned i=0;i<65559;i++)out[i]=(char)(i*17+(i>>7)+4*29);
 if(corrupt||(corruptAfterSet&&sets))out[4096]^=1;return 0;
}
static int vfs_devblocksize(mount_t m){assert(m==(void *)2);return 4096;}
static int VNOP_BLOCKMAP(vnode_t p,off_t logical,size_t bytes,daddr64_t *block,size_t *run,void *unused,int flags,vfs_context_t c){
 assert(p==(void *)1&&live&&(logical==0||logical==61440)&&bytes==4096&&!unused&&flags==3&&c==7);*block=logical?8:4;*run=4096;return 0;
}
int kebab_ufs_raw_read(uint64_t offset,void *out,uint32_t bytes){
 assert((offset==16384||offset==32768)&&bytes==4096);raws++;if(inject==3)return EIO;
 memset(out,0x5a,bytes);if(rawPlain){unsigned logical=offset==32768?61440:0;for(unsigned i=0;i<bytes;i++){unsigned at=logical+i;((unsigned char *)out)[i]=(unsigned char)(at*17+(at>>7)+4*29);}}return 0;
}
struct nameidata {vnode_t ni_vp;};
#define NDINIT(...) ((void)0)
static int vn_open_auth(struct nameidata *nd,int *flags,struct vnode_attr *attr,vnode_t d){(void)nd;(void)flags;(void)attr;(void)d;return 17;}
/* Real original APFS C/D files. No private wrapped-key fixture is stored here. */
#define KDP_BYTES 65559u
extern int kebab_ufs_raw_read(uint64_t,void *,uint32_t);

static int
kdp_metadata(vnode_t file,mount_t expected,uint32_t cls,uint64_t bytes,vfs_context_t context)
{
 if(!vnode_isreg(file)||vnode_mount(file)!=expected){printf("KEBAB DATA FILE META: class=%u wrong type/mount identity\n",cls);return EAUTH;}
 struct vnode_attr attr;VATTR_INIT(&attr);
 VATTR_WANTED(&attr,va_uid);VATTR_WANTED(&attr,va_mode);VATTR_WANTED(&attr,va_nlink);
 VATTR_WANTED(&attr,va_flags);VATTR_WANTED(&attr,va_data_size);VATTR_WANTED(&attr,va_dataprotect_class);
 int error=VNOP_GETATTR(file,&attr,context);if(error)return error;
 if(!VATTR_IS_SUPPORTED(&attr,va_uid)||!VATTR_IS_SUPPORTED(&attr,va_mode)||
     !VATTR_IS_SUPPORTED(&attr,va_nlink)||!VATTR_IS_SUPPORTED(&attr,va_flags)||
     !VATTR_IS_SUPPORTED(&attr,va_data_size)||!VATTR_IS_SUPPORTED(&attr,va_dataprotect_class)||
     attr.va_uid||attr.va_nlink!=1||attr.va_flags||(attr.va_mode&07777)!=0600||
     attr.va_data_size!=bytes||attr.va_dataprotect_class!=cls){
  printf("KEBAB DATA FILE META: class=%u uid=%u mode=0%o nlink=%llu flags=0x%x bytes=%llu protection=%u supported=%u%u%u%u%u%u\n",cls,
      VATTR_IS_SUPPORTED(&attr,va_uid)?attr.va_uid:UINT32_MAX,
      VATTR_IS_SUPPORTED(&attr,va_mode)?attr.va_mode:0,
      (unsigned long long)(VATTR_IS_SUPPORTED(&attr,va_nlink)?attr.va_nlink:0),
      VATTR_IS_SUPPORTED(&attr,va_flags)?attr.va_flags:0,
      (unsigned long long)(VATTR_IS_SUPPORTED(&attr,va_data_size)?attr.va_data_size:0),
      VATTR_IS_SUPPORTED(&attr,va_dataprotect_class)?attr.va_dataprotect_class:0,
      !!VATTR_IS_SUPPORTED(&attr,va_uid),!!VATTR_IS_SUPPORTED(&attr,va_mode),
      !!VATTR_IS_SUPPORTED(&attr,va_nlink),!!VATTR_IS_SUPPORTED(&attr,va_flags),
      !!VATTR_IS_SUPPORTED(&attr,va_data_size),!!VATTR_IS_SUPPORTED(&attr,va_dataprotect_class));
  return EAUTH;
 }
 return 0;
}

/* Same class-at-create contract as original open_dprotected_np/open1_dprotected:
 * pass va_dataprotect_class into vn_open_auth, rather than inherit a different
 * root-directory class before a later setattr. */
static int
kdp_create(const char *path,uint32_t cls,vnode_t *file,vfs_context_t context)
{
 struct nameidata *nd=kalloc_type(struct nameidata,Z_WAITOK|Z_ZERO);if(!nd)return ENOMEM;
 struct vnode_attr attr;VATTR_INIT(&attr);VATTR_SET(&attr,va_mode,0600);VATTR_SET(&attr,va_dataprotect_class,cls);
 int flags=O_CREAT|O_EXCL|O_NOFOLLOW|FREAD|FWRITE;
 NDINIT(nd,LOOKUP,OP_OPEN,NOFOLLOW,UIO_SYSSPACE,CAST_USER_ADDR_T(path),context);
 int error=vn_open_auth(nd,&flags,&attr,NULLVP);*file=error?NULLVP:nd->ni_vp;
 kfree_type(struct nameidata,nd);return error;
}

/* Original F_SETPROTECTIONCLASS path: authorize then VNOP_SETATTR. Reopen
 * after each real class migration and verify the original file payload. */
static int
kdp_reclass(mount_t expected,const char *path,uint32_t cls,uint8_t *plain,uint8_t *back)
{
 vfs_context_t context=vfs_context_current();uint32_t other=cls==3?4:3;int error=0;
 for(unsigned stage=0;!error&&stage<2;++stage){
  uint32_t source=stage?other:cls,target=stage?cls:other;vnode_t file=NULLVP;
  error=vnode_open(path,O_NOFOLLOW|FREAD|FWRITE,0,0,&file,context);if(error)break;
  error=kdp_metadata(file,expected,source,KDP_BYTES,context);
  if(!error)error=vnode_authorize(file,NULLVP,KAUTH_VNODE_ACCESS|KAUTH_VNODE_WRITE_DATA,context);
  if(!error){struct vnode_attr attr;VATTR_INIT(&attr);VATTR_SET(&attr,va_dataprotect_class,target);error=VNOP_SETATTR(file,&attr,context);}
  printf("KEBAB DATA REWRAP: original class %u -> %u setattr=%d\n",source,target,error);
  int synced=VNOP_FSYNC(file,MNT_WAIT,context);if(!error)error=synced;
  int closed=vnode_close(file,FREAD|FWRITE,context);file=NULLVP;if(!error)error=closed;
  if(error)break;
  error=vnode_open(path,O_NOFOLLOW|FREAD,0,0,&file,context);if(error)break;
  error=kdp_metadata(file,expected,target,KDP_BYTES,context);
  if(!error){int residual=0;error=vn_rdwr(UIO_READ,file,(caddr_t)back,KDP_BYTES,0,UIO_SYSSPACE,IO_NOCACHE,vfs_context_ucred(context),&residual,NULL);if(!error&&residual)error=EIO;}
  if(!error&&memcmp(plain,back,KDP_BYTES))error=EAUTH;
  closed=vnode_close(file,FREAD,context);if(!error)error=closed;
  printf("KEBAB DATA REWRAP: class %u reopen/native protected read %s (%d)\n",target,error?"FAIL":"PASS",error);
 }
 return error;
}

/* Only the private known-pattern proof file may be restored after a normal
 * userspace session changed its reported class from D to C. The caller must
 * first prove its complete original payload, metadata and two encrypted raw
 * extents. Use the original authorized class migration, never recreate the
 * file, edit raw metadata, replace class keys or touch a system/application file.
 * Reopen and repeat the full proof after this call. */
static int
kdp_restore_fixture_d(mount_t expected,vfs_context_t context)
{
 const char *path="/private/var/.hios-cp-d-v1";vnode_t file=NULLVP;
 int error=vnode_open(path,O_NOFOLLOW|FREAD|FWRITE,0,0,&file,context);if(error)return error;
 error=kdp_metadata(file,expected,3,KDP_BYTES,context);
 if(!error)error=vnode_authorize(file,NULLVP,KAUTH_VNODE_ACCESS|KAUTH_VNODE_WRITE_DATA,context);
 if(!error){struct vnode_attr attr;VATTR_INIT(&attr);VATTR_SET(&attr,va_dataprotect_class,4);error=VNOP_SETATTR(file,&attr,context);}
 int synced=VNOP_FSYNC(file,MNT_WAIT,context);if(!error)error=synced;
 int closed=vnode_close(file,FREAD|FWRITE,context);if(!error)error=closed;
 printf("KEBAB DATA FIXTURE: original authorized class C -> D result=%d; fixed known-pattern file only\n",error);
 return error;
}

static int
kdp_file(mount_t expected,const char *path,uint32_t cls,bool create,bool rewrap)
{
 struct Work{uint8_t plain[KDP_BYTES],back[KDP_BYTES],cipher[4096];};
 struct Work *w=kalloc_data(sizeof(*w),Z_WAITOK|Z_ZERO);if(!w)return ENOMEM;
 vfs_context_t context=vfs_context_current();vnode_t file=NULLVP;int error=0;const char *step="create";
 unsigned tool=0;PE_parse_boot_argn("kebab_apfs_tool",&tool,sizeof(tool));
 uint32_t observed_class=cls;bool restore_d=false,restored_d=false;
 for(unsigned i=0;i<KDP_BYTES;++i)w->plain[i]=(uint8_t)(i*17+(i>>7)+cls*29);
 if(create){
  error=kdp_create(path,cls,&file,context);
  if(error)goto done;
  if(!vnode_isreg(file)||vnode_mount(file)!=expected)error=EAUTH;
  if(!error)error=kdp_metadata(file,expected,cls,0,context);
  if(!error){
   int residual=0;error=vn_rdwr(UIO_WRITE,file,(caddr_t)w->plain,KDP_BYTES,0,UIO_SYSSPACE,
       IO_SYNC|IO_NOCACHE,vfs_context_ucred(context),&residual,NULL);
   if(!error&&residual)error=EIO;
  }
  int sync_error=VNOP_FSYNC(file,MNT_WAIT,context);if(!error)error=sync_error;
  int closed=vnode_close(file,FREAD|FWRITE,context);file=NULLVP;if(!error)error=closed;
  if(error)goto done;
 }
 if(rewrap){error=kdp_reclass(expected,path,cls,w->plain,w->back);if(error)goto done;}
read_file:
 step="open";error=vnode_open(path,O_NOFOLLOW|FREAD,0,0,&file,context);if(error)goto done;
 step="metadata";error=kdp_metadata(file,expected,cls,KDP_BYTES,context);
 if(error==EAUTH&&tool==12&&!create&&!rewrap&&!restored_d&&cls==4&&
     !strcmp(path,"/private/var/.hios-cp-d-v1")&&
     !kdp_metadata(file,expected,3,KDP_BYTES,context)){
  error=0;observed_class=3;restore_d=true;
  printf("KEBAB DATA FIXTURE: observed original class C; full existing D-pattern/raw proof required before restoration\n");
 }
 if(!error){
  step="protected-read";int residual=0;error=vn_rdwr(UIO_READ,file,(caddr_t)w->back,KDP_BYTES,0,UIO_SYSSPACE,
      IO_NOCACHE,vfs_context_ucred(context),&residual,NULL);
  if(!error&&residual)error=EIO;
 }
 if(!error&&memcmp(w->plain,w->back,KDP_BYTES)){
  step="plaintext-pattern";unsigned first=0;while(first<KDP_BYTES&&w->plain[first]==w->back[first])first++;
  printf("KEBAB DATA FILE READ: class=%u first mismatch offset=%u; no file/key bytes disclosed\n",cls,first);error=EAUTH;
 }
 int blocksize=vfs_devblocksize(expected);
 if(!error&&blocksize!=512&&blocksize!=4096)error=EINVAL;
 for(unsigned i=0;!error&&i<2;++i){
  off_t logical=i?61440:0;daddr64_t block=-1;size_t run=0;step=i?"raw-extent-tail":"raw-extent-head";
  error=VNOP_BLOCKMAP(file,logical,4096,&block,&run,NULL,VNODE_READ|VNODE_BLOCKMAP_NO_TRACK,context);
  if(!error&&(block<0||run<4096||(uint64_t)block>UINT64_MAX/(unsigned)blocksize))error=EINVAL;
  if(!error)error=kebab_ufs_raw_read((uint64_t)block*(unsigned)blocksize,w->cipher,4096);
  if(!error&&!memcmp(w->cipher,w->plain+logical,4096))error=EAUTH;
  if(!error){
   uint8_t digest[32];SHA256_CTX hash;SHA256_Init(&hash);SHA256_Update(&hash,w->cipher,4096);SHA256_Final(digest,&hash);
   printf("KEBAB DATA FILE: class=%u logical=%llu ciphertext-sha256=",observed_class,(unsigned long long)logical);
   for(unsigned j=0;j<32;++j)printf("%02x",digest[j]);printf(" differs from plaintext\n");
   bzero(digest,sizeof(digest));
  }
 }
 if(!error&&restore_d){
  step="restore-private-fixture-D";
  error=vnode_close(file,FREAD,context);file=NULLVP;
  if(!error)error=kdp_restore_fixture_d(expected,context);
  if(!error){restore_d=false;restored_d=true;observed_class=cls;goto read_file;}
 }
 if(!error){
  uint8_t digest[32];SHA256_CTX hash;SHA256_Init(&hash);SHA256_Update(&hash,w->back,KDP_BYTES);SHA256_Final(digest,&hash);
  printf("KEBAB DATA FILE: %s class=%u bytes=%u original APFS protected IO sha256=",create?"write/read":(rewrap?"rewrap-read":(restored_d?"post-restoration-read":"cold-read")),cls,KDP_BYTES);
  for(unsigned j=0;j<32;++j)printf("%02x",digest[j]);printf(" PASS\n");bzero(digest,sizeof(digest));
 }
done:
 if(error)printf("KEBAB DATA FILE FAILURE: class=%u step=%s error=%d; checks retained\n",cls,step,error);
 if(file){int closed=vnode_close(file,FREAD,context);if(!error)error=closed;}
 printf("KEBAB DATA FILE: fixed %s class=%u %s (%d)\n",create?"exclusive create":(rewrap?"class rewrap":(restored_d?"post-restoration proof":"cold restore")),cls,error?"FAIL":"PASS",error);
 bzero(w,sizeof(*w));kfree_data(w,sizeof(*w));return error;
}


static void reset(void){
 model=(struct vnode_attr){0,63,0,0600,0,3,1,65559};kind=1;actual=(void *)2;getter_error=0;
 tool=12;opens=closes=reads=raws=authorizes=sets=syncs=live=0;inject=0;corrupt=rawPlain=ignoreSet=corruptAfterSet=false;
}
static int run(const char *path,uint32_t cls,bool create,bool rewrap){int e=kdp_file((void *)2,path,cls,create,rewrap);assert(!live&&opens>=closes);return e;}
int main(void){
 const char *path="/private/var/.hios-cp-d-v1";unsigned cases=0;
 reset();assert(!run(path,4,false,false)&&opens==3&&closes==3&&reads==2&&raws==4&&authorizes==1&&sets==1&&syncs==1&&model.va_dataprotect_class==4);cases++;
 // Already-correct files are read, not rewritten.
 reset();model.va_dataprotect_class=4;assert(!run(path,4,false,false)&&opens==1&&closes==1&&reads==1&&raws==2&&!sets&&!authorizes);cases++;
 // Every strict metadata field/type/mount and every fixed profile/path gate.
 for(unsigned bad=0;bad<10;bad++){
  reset();switch(bad){case 0:model.va_uid=501;break;case 1:model.va_mode=0644;break;case 2:model.va_nlink=2;break;case 3:model.va_flags=1;break;case 4:model.va_data_size--;break;case 5:model.va_dataprotect_class=2;break;case 6:model.supported=31;break;case 7:kind=0;break;case 8:actual=(void *)3;break;default:getter_error=5;}
  assert(run(path,4,false,false)!=0&&!reads&&!sets&&!authorizes);cases++;
 }
 for(unsigned mode=0;mode<14;mode++)if(mode!=12){reset();tool=mode;assert(run(path,4,false,false)==EAUTH&&!sets&&!authorizes);cases++;}
 reset();assert(run("/private/var/another-file",4,false,false)==EAUTH&&!sets);cases++;
 reset();corrupt=true;assert(run(path,4,false,false)==EAUTH&&reads==1&&!raws&&!sets&&!authorizes);cases++;
 reset();rawPlain=true;assert(run(path,4,false,false)==EAUTH&&reads==1&&raws==1&&!sets&&!authorizes);cases++;
 reset();assert(run(path,4,true,false)==17&&!sets);cases++;
 reset();assert(run(path,4,false,true)==EAUTH&&!sets);cases++;
 // Original failure returns survive; neither failed auth nor failed pre-proof
 // can be turned into a restoration or a success marker.
 for(int failed=1;failed<=7;failed++){
  reset();inject=failed;assert(run(path,4,false,false)==(failed==4?13:EIO));if(failed<=4||failed==7)assert(!sets);cases++;
 }
 reset();ignoreSet=true;assert(run(path,4,false,false)==EAUTH&&sets==1&&reads==1&&model.va_dataprotect_class==3);cases++;
 reset();corruptAfterSet=true;assert(run(path,4,false,false)==EAUTH&&sets==1&&reads==2);cases++;
 fprintf(stdout,"PASS %u complete private-file production lifecycle cases; strict profile/path/metadata/full-pattern/raw-proof before authorized migration, full reread afterward, errors preserved; APFS/VFS/raw extent APIs explicitly modeled\n",cases);
}
