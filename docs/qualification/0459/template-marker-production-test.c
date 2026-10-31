
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#define EAUTH 80
#define ENOENT 2
#define EIO 5
#define FREAD 1
#define FWRITE 2
#define UIO_READ 1
#define UIO_WRITE 2
#define UIO_SYSSPACE 1
#define IO_SYNC 1
#define IO_NOCACHE 2
#define MNT_WAIT 1
#define NULLVP NULL
#define KDI_MARKER "/private/var/.hios-data-template-v1"
#define bzero(p,n) memset(p,0,n)
typedef void *vnode_t;typedef void *mount_t;
struct vnode_attr {unsigned wanted,supported,va_uid,va_gid,va_mode,va_flags,va_dataprotect_class;uint64_t va_nlink,va_data_size;};
enum{b_va_uid=1,b_va_gid=2,b_va_mode=4,b_va_flags=8,b_va_dataprotect_class=16,b_va_nlink=32,b_va_data_size=64};
#define VATTR_INIT(p) ((p)->wanted=0)
#define VATTR_WANTED(p,f) ((p)->wanted|=b_##f)
#define VATTR_IS_SUPPORTED(p,f) (((p)->supported&b_##f)!=0)
struct kdi_work {struct{uint8_t *buffer;}scan;uint8_t marker[4096];};
static unsigned live,opens,closes,reads,writes,syncs,regular=1;static void *mounted=(void *)2;
static int fault;static bool shortRead;static uint8_t disk[4096],scratch[4096];static struct vnode_attr model;
static int vfs_context_current(void){return 7;}static int vfs_context_ucred(int c){assert(c==7);return 9;}
static int vnode_open(const char *path,int flags,int a,int b,vnode_t *out,int c){assert(!strcmp(path,KDI_MARKER)&&flags&O_NOFOLLOW&&!live&&!a&&!b&&c==7);opens++;if(fault==1)return EIO;*out=(void *)1;live=1;return 0;}
static int kdi_new(const char *path,vnode_t *out){(void)path;(void)out;assert(false);return EIO;}
static void *vnode_mount(vnode_t p){assert(p==(void *)1&&live);return mounted;}
static bool vnode_isreg(vnode_t p){assert(p==(void *)1&&live);return regular;}
static int vnode_getattr(vnode_t p,struct vnode_attr *a,int c){assert(p==(void *)1&&live&&c==7&&a->wanted==127);if(fault==2)return EIO;*a=model;return 0;}
static int vn_rdwr(int direction,vnode_t p,caddr_t buf,int n,off_t offset,int segment,int flags,int cred,int *residual,void *unused){
 assert(p==(void *)1&&live&&n==4096&&!offset&&segment==UIO_SYSSPACE&&(flags&IO_NOCACHE)&&cred==9&&!unused);
 *residual=0;if(direction==UIO_READ){reads++;if(fault==3)return EIO;memcpy(buf,disk,n);if(shortRead)*residual=1;}
 else{assert(direction==UIO_WRITE&&(flags&IO_SYNC));writes++;if(fault==4)return EIO;memcpy(disk,buf,n);}return 0;
}
static int VNOP_FSYNC(vnode_t p,int mode,int c){assert(p==(void *)1&&live&&mode==MNT_WAIT&&c==7);syncs++;return fault==5?EIO:0;}
static int vnode_close(vnode_t p,int flags,int c){assert(p==(void *)1&&live&&(flags&FREAD)&&c==7);live=0;closes++;return fault==6?EIO:0;}
static int observed_printf(const char *format,...){(void)format;return 0;}
#define printf observed_printf
static const uint8_t kdi_contract_sha[32]={0xba,0x31,0x4e,0xf6,0x92,0x81,0x85,0x6a,0xc7,0xb7,0x54,0x47,0x59,0x52,0x69,0x8c,0x1e,0x61,0x42,0xef,0x26,0xaf,0x53,0x11,0x1d,0xf5,0x1a,0x34,0x84,0x1f,0x4b,0x53};
static void
kdi_marker_bytes(uint8_t out[4096],bool complete)
{
 bzero(out,4096);memcpy(out,"HDI1",4);out[4]=1;out[5]=complete;
 memcpy(out+32,kdi_contract_sha,32);
 static const uint8_t system[16]={0xda,0xdd,0x93,0xf8,0xdb,0xbf,0x4b,0x17,0xa1,0xdf,0x0f,0x11,0xe6,0x41,0x72,0xf7};
 static const uint8_t data[16]={0x61,0x70,0x66,0x73,0x75,0x75,0x69,0x64,0,0x40,0x76,0x6f,0x6c,0x75,0x6d,1};
 memcpy(out+64,system,16);memcpy(out+80,data,16);
}

static int
kdi_marker_before(struct kdi_work *w,mount_t data,bool write,bool complete,bool *exists,bool *finished)
{
 vnode_t file=NULLVP;int error=vnode_open(KDI_MARKER,O_NOFOLLOW|FREAD|(write?FWRITE:0),0,0,&file,vfs_context_current());
 if(error==ENOENT&&write)error=kdi_new(KDI_MARKER,&file);
 if(error)return error;
 if(vnode_mount(file)!=data||!vnode_isreg(file)){error=EAUTH;goto done;}
 struct vnode_attr a;VATTR_INIT(&a);VATTR_WANTED(&a,va_data_size);VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);VATTR_WANTED(&a,va_nlink);VATTR_WANTED(&a,va_flags);VATTR_WANTED(&a,va_dataprotect_class);
 error=vnode_getattr(file,&a,vfs_context_current());if(error)goto done;
 if(!VATTR_IS_SUPPORTED(&a,va_data_size)||!VATTR_IS_SUPPORTED(&a,va_uid)||!VATTR_IS_SUPPORTED(&a,va_gid)||!VATTR_IS_SUPPORTED(&a,va_mode)||!VATTR_IS_SUPPORTED(&a,va_nlink)||!VATTR_IS_SUPPORTED(&a,va_flags)||!VATTR_IS_SUPPORTED(&a,va_dataprotect_class)||a.va_dataprotect_class!=4||a.va_uid||a.va_gid||(a.va_mode&07777)!=0600||a.va_nlink!=1||a.va_flags||(a.va_data_size!=0&&a.va_data_size!=4096)){error=EAUTH;goto done;}
 *exists=a.va_data_size==4096;*finished=false;
 if(*exists){
  int residual=0;error=vn_rdwr(UIO_READ,file,(caddr_t)w->marker,4096,0,UIO_SYSSPACE,IO_NOCACHE,vfs_context_ucred(vfs_context_current()),&residual,NULL);
  if(!error&&residual)error=EIO;if(error)goto done;
  bool bit=w->marker[5]==1;uint8_t *expected=w->scan.buffer;kdi_marker_bytes(expected,bit);
  if(memcmp(expected,w->marker,4096)){error=EAUTH;goto done;}*finished=bit;
 }
 if(write){
  kdi_marker_bytes(w->marker,complete);int residual=0;
  error=vn_rdwr(UIO_WRITE,file,(caddr_t)w->marker,4096,0,UIO_SYSSPACE,IO_SYNC|IO_NOCACHE,vfs_context_ucred(vfs_context_current()),&residual,NULL);
  if(!error&&residual)error=EIO;if(!error)error=VNOP_FSYNC(file,MNT_WAIT,vfs_context_current());
 }
done:
 if(file){int closed=vnode_close(file,FREAD|(write?FWRITE:0),vfs_context_current());if(!error)error=closed;}
 return error;
}

static int
kdi_marker_after(struct kdi_work *w,mount_t data,bool write,bool complete,bool *exists,bool *finished)
{
 vnode_t file=NULLVP;int error=vnode_open(KDI_MARKER,O_NOFOLLOW|FREAD|(write?FWRITE:0),0,0,&file,vfs_context_current());
 if(error==ENOENT&&write)error=kdi_new(KDI_MARKER,&file);
 if(error)return error;
 if(vnode_mount(file)!=data||!vnode_isreg(file)){error=EAUTH;goto done;}
 struct vnode_attr a;VATTR_INIT(&a);VATTR_WANTED(&a,va_data_size);VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);VATTR_WANTED(&a,va_nlink);VATTR_WANTED(&a,va_flags);VATTR_WANTED(&a,va_dataprotect_class);
 error=vnode_getattr(file,&a,vfs_context_current());if(error)goto done;
 if(!VATTR_IS_SUPPORTED(&a,va_data_size)||!VATTR_IS_SUPPORTED(&a,va_uid)||!VATTR_IS_SUPPORTED(&a,va_gid)||!VATTR_IS_SUPPORTED(&a,va_mode)||!VATTR_IS_SUPPORTED(&a,va_nlink)||!VATTR_IS_SUPPORTED(&a,va_flags)||!VATTR_IS_SUPPORTED(&a,va_dataprotect_class)||(a.va_dataprotect_class!=4&&(write||a.va_dataprotect_class!=3))||a.va_uid||a.va_gid||(a.va_mode&07777)!=0600||a.va_nlink!=1||a.va_flags||(a.va_data_size!=0&&a.va_data_size!=4096)){error=EAUTH;goto done;}
 /* Read-only installation receipts may have been strengthened from D to C
  * during original userspace initialization. Keep exact owner/mode/link/mount
  * and full4096B contract/UUID/content checks. Writes still require class D. */
 printf("KEBAB DATA MARKER: class=%u bytes=%llu write=%u; full contract read still required\n",a.va_dataprotect_class,(unsigned long long)a.va_data_size,write);
 *exists=a.va_data_size==4096;*finished=false;
 if(*exists){
  int residual=0;error=vn_rdwr(UIO_READ,file,(caddr_t)w->marker,4096,0,UIO_SYSSPACE,IO_NOCACHE,vfs_context_ucred(vfs_context_current()),&residual,NULL);
  if(!error&&residual)error=EIO;if(error)goto done;
  bool bit=w->marker[5]==1;uint8_t *expected=w->scan.buffer;kdi_marker_bytes(expected,bit);
  if(memcmp(expected,w->marker,4096)){error=EAUTH;goto done;}*finished=bit;
 }
 if(write){
  kdi_marker_bytes(w->marker,complete);int residual=0;
  error=vn_rdwr(UIO_WRITE,file,(caddr_t)w->marker,4096,0,UIO_SYSSPACE,IO_SYNC|IO_NOCACHE,vfs_context_ucred(vfs_context_current()),&residual,NULL);
  if(!error&&residual)error=EIO;if(!error)error=VNOP_FSYNC(file,MNT_WAIT,vfs_context_current());
 }
done:
 if(file){int closed=vnode_close(file,FREAD|(write?FWRITE:0),vfs_context_current());if(!error)error=closed;}
 return error;
}

static void reset(unsigned cls){model=(struct vnode_attr){0,127,0,0,0600,0,cls,1,4096};live=opens=closes=reads=writes=syncs=0;regular=1;mounted=(void *)2;fault=0;shortRead=false;kdi_marker_bytes(disk,true);}
static int run(bool old,bool write,bool *finished){struct kdi_work w={0};w.scan.buffer=scratch;bool exists=false;*finished=false;int e=old?kdi_marker_before(&w,(void *)2,write,true,&exists,finished):kdi_marker_after(&w,(void *)2,write,true,&exists,finished);assert(!live&&closes==(fault==1?0:1));if(!e)assert(exists);return e;}
int main(void){unsigned cases=0;bool finished;
 for(unsigned cls=0;cls<8;cls++)for(unsigned write=0;write<2;write++){
  reset(cls);int e=run(false,write,&finished);bool allowed=cls==4||(cls==3&&!write);assert(e==(allowed?0:EAUTH));if(!e)assert(finished);if(!allowed)assert(!reads&&!writes);cases++;
 }
 for(unsigned supported=0;supported<128;supported++)for(unsigned cls=3;cls<=4;cls++){
  reset(cls);model.supported=supported;int e=run(false,false,&finished);assert(e==(supported==127?0:EAUTH));if(e)assert(!reads&&!writes);cases++;
 }
 for(unsigned bad=0;bad<7;bad++)for(unsigned cls=3;cls<=4;cls++){
  reset(cls);switch(bad){case 0:model.va_uid=501;break;case 1:model.va_gid=501;break;case 2:model.va_mode=0644;break;case 3:model.va_flags=1;break;case 4:model.va_nlink=2;break;case 5:model.va_data_size=4095;break;default:regular=0;}
  assert(run(false,false,&finished)==EAUTH&&!reads&&!writes);cases++;
 }
 for(unsigned cls=3;cls<=4;cls++)for(unsigned at=0;at<4096;at++){
  reset(cls);disk[at]^=0x80;assert(run(false,false,&finished)==EAUTH&&reads==1&&!writes);cases++;
 }
 // The complete class D behavior remains identical, including error codes.
 for(unsigned write=0;write<2;write++)for(int failure=0;failure<=6;failure++){
  reset(4);fault=failure;bool oldFinished;int old=run(true,write,&oldFinished);unsigned oldReads=reads,oldWrites=writes,oldSyncs=syncs;
  reset(4);fault=failure;int now=run(false,write,&finished);assert(now==old&&finished==oldFinished&&reads==oldReads&&writes==oldWrites&&syncs==oldSyncs);cases++;
 }
 reset(3);shortRead=true;assert(run(false,false,&finished)==EIO&&!writes);cases++;
 reset(3);mounted=(void *)3;assert(run(false,false,&finished)==EAUTH&&!reads&&!writes);cases++;
 reset(3);kdi_marker_bytes(disk,false);{struct kdi_work w={0};w.scan.buffer=scratch;bool exists;assert(!kdi_marker_after(&w,(void *)2,false,false,&exists,&finished)&&exists&&!finished&&!writes);}cases++;
 fprintf(stdout,"PASS %u complete marker lifecycle cases: class C/D exact protected reads, full4096B contract/UUID corruption refusals, strict class D writes, unchanged prior class D results; VFS/APFS modeled\n",cases);
}
