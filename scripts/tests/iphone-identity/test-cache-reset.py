from pathlib import Path
import hashlib,json,subprocess,tempfile
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);root=args.source;s=(root/'bsd/kern/kebab_bootdiag.c').read_text();a=s.index('/* Optional owner-measured one-file');body=s[a:s.index('\nvoid\nkebab_process_report(void)',a)]
prefix=r'''#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <openssl/sha.h>
#define EAUTH 80
#define KEBAB_MG_CACHE_LIMIT 1048576
#define NULLVP NULL
#define VNODE_LOOKUP_NOFOLLOW 1
#define MNT_WAIT 1
#define kSuccess 0
typedef void*DTEntry;typedef int vfs_context_t;struct vnode{int x;};typedef struct vnode*vnode_t;static struct vnode node;
struct proc{void*p_textvp;};static struct proc initial,other;static struct proc*initproc=&initial;
struct vnode_attr{uint64_t va_data_size,va_fileid;};struct vfsstatfs{char f_fstypename[16],f_mntonname[128];};static struct vfsstatfs stats;
static const char*const kebab_mg_cache_paths[]={"original","a","b","c","backup"};
static const unsigned char original[]="original-cache";static unsigned char plan[64];static int mode,attempts,renames,snapshots,allocated,released,gets,puts_count,syncs;
#define VATTR_INIT(v) memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,k) ((void)0)
#define VATTR_IS_SUPPORTED(v,k) (mode!=16)
static struct proc*current_proc(void){return mode==9?&other:initproc;}
static int panic_active(void){return mode==11;}static int vfs_context_current(void){return 9;}
static int SecureDTLookupEntry(void*x,const char*p,DTEntry*out){(void)x;assert(!strcmp(p,"/chosen"));*out=(void*)1;return mode==1?-1:0;}
static int SecureDTGetProperty(DTEntry e,const char*p,const void**out,unsigned*n){assert(e==(void*)1&&!strcmp(p,"hios-mg-cache-reset"));*out=plan;*n=mode==2?63:64;return 0;}
static int vnode_lookup(const char*p,int f,vnode_t*out,int c){assert(f==VNODE_LOOKUP_NOFOLLOW&&c==9);if(!strcmp(p,"original")&&mode==12)return ENOENT;if(mode==13)return EACCES;*out=&node;gets++;return 0;}
static int vnode_isreg(vnode_t v){assert(v==&node);return mode!=14;}
static int vnode_getattr(vnode_t v,struct vnode_attr*a,int c){assert(v==&node&&c==9);if(mode==15)return EIO;a->va_fileid=mode==19?43:42;a->va_data_size=mode==20?12:sizeof(original)-1;return 0;}
static int vnode_mount(vnode_t v){assert(v==&node);return 1;}
static struct vfsstatfs*vfs_statfs(int m){assert(m==1);strcpy(stats.f_fstypename,mode==17?"hfs":"apfs");strcpy(stats.f_mntonname,mode==18?"/":"/private/var");return &stats;}
static void vnode_put(vnode_t v){assert(v==&node);puts_count++;}
static int kebab_identity_cache_snapshot(unsigned i,void**out,uint32_t*n){assert(i==0||i==4);snapshots++;if(mode==21||(mode==26&&i==4))return EIO;*n=sizeof(original)-1;if(mode==23)*n-=1;*out=malloc(*n);allocated++;memcpy(*out,original,*n);if(mode==22||(mode==27&&i==4))((char*)*out)[0]='x';return 0;}
static void kebab_identity_cache_release(void*p,uint32_t n){assert(p&&n);released++;free(p);}
static int kebab_identity_cache_backup(void){attempts++;if(mode==24)return EEXIST;if(mode==25)return EACCES;renames++;return 0;}
static int VNOP_FSYNC(vnode_t v,int flag,int c){assert(v==&node&&flag==MNT_WAIT&&c==9);syncs++;return mode==28||(mode==29&&syncs==2)?EIO:0;}
static int model_printf(const char*f,...){(void)f;return 0;}
#define printf model_printf
'''
main=r'''int main(void){for(mode=0;mode<30;mode++){
 memset(plan,0,sizeof(plan));uint32_t header[]={0x4449474d,1,0,sizeof(original)-1};uint64_t id=42;memcpy(plan,header,16);memcpy(plan+16,&id,8);SHA256(original,sizeof(original)-1,plan+24);
 if(mode==3)plan[0]=0;if(mode==4)plan[4]=2;if(mode==5)plan[8]=1;if(mode==6)memset(plan+12,0,4);if(mode==7)memset(plan+16,0,8);if(mode==8)plan[63]=1;
 initial.p_textvp=mode==10?(void*)1:NULL;attempts=renames=snapshots=allocated=released=gets=puts_count=syncs=0;
 int error=kebab_identity_cache_reset_once();bool no_op=mode==1||mode==12||mode==19||mode==20;
 assert((error==0)==(mode==0||no_op));assert(allocated==released&&gets==puts_count);
 if(mode==0){assert(renames==1&&attempts==1&&snapshots==2&&syncs==2);}else if(mode>=24){assert(attempts==1);assert(renames==(mode>=26));}else assert(!renames&&!attempts);
}return 0;}
'''
vfs=(root/'bsd/vfs/vfs_syscalls.c').read_text();a=vfs.index('#if defined(KEBAB)\n/* One exact, exclusive backup');wrapper=vfs[a:vfs.index('\nint\nrename(__unused',a)]
wrap_prefix=r'''#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#define KEBAB 1
#define NULLVP NULL
#define AT_FDCWD -2
#define UIO_SYSSPACE 2
#define RENAME_EXCL 4
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
struct proc{void*p_textvp;};static struct proc initial,other;static struct proc*initproc=&initial;static int bad,called,forced;
static struct proc*current_proc(void){return bad?&other:&initial;}
static int vfs_context_current(void){return 9;}
static int renameat_internal(int ctx,int fd,uintptr_t from,int tofd,uintptr_t to,int seg,unsigned flags){assert(ctx==9&&fd==AT_FDCWD&&tofd==AT_FDCWD&&seg==UIO_SYSSPACE&&flags==RENAME_EXCL);const char*f=(void*)from,*t=(void*)to;assert(!strcmp(f,"/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist"));assert(!strcmp(t,"/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist.hios-before-iphone-x-20261102"));called++;return forced;}
'''
wrap_main=r'''int main(void){bad=1;assert(kebab_identity_cache_backup()==EPERM&&!called);bad=0;initial.p_textvp=(void*)1;assert(kebab_identity_cache_backup()==EPERM&&!called);initial.p_textvp=NULL;forced=EEXIST;assert(kebab_identity_cache_backup()==EEXIST&&called==1);forced=EACCES;assert(kebab_identity_cache_backup()==EACCES&&called==2);forced=0;assert(kebab_identity_cache_backup()==0&&called==3);return 0;}
'''
with tempfile.TemporaryDirectory()as d:
 d=Path(d)
 def execute(name,text,ok=True,crypto=False):
  p=d/(name+'.c');p.write_text(text);exe=d/name;subprocess.run(['clang','-Wno-deprecated-declarations','-fsanitize=address,undefined',str(p),'-o',str(exe),*(['-lcrypto']if crypto else[])],check=True);q=subprocess.run([str(exe)],capture_output=True);assert(q.returncode==0)==ok,(name,q.stderr[-900:])
 execute('actual-reset',prefix+body+main,crypto=True)
 mutant=body.replace('memcmp(digest, expected, sizeof(digest))','0');assert mutant!=body;execute('ignore-digest',prefix+mutant+main,False,True)
 mutant=body.replace('header[2] != 0','false');assert mutant!=body;execute('wrong-index',prefix+mutant+main,False,True)
 execute('actual-wrapper',wrap_prefix+wrapper+wrap_main)
 mutant=wrapper.replace('UIO_SYSSPACE, RENAME_EXCL','UIO_SYSSPACE, 0');assert mutant!=wrapper;execute('overwrite-backup',wrap_prefix+mutant+wrap_main,False)
(w/'cache-reset-tests.json').write_text(json.dumps(dict(passed=True,actual_reset_cases=30,actual_fixed_vfs_wrapper_cases=5,compiled_runtime_mutants_rejected=3,host_SHA256='OpenSSL only as SHA2 test implementation; target uses existing kernel SHA2',vnode_DT_process_rename_and_fsync_models=True,source_sha256={n:hashlib.sha256((root/n).read_bytes()).hexdigest()for n in('bsd/kern/kebab_bootdiag.c','bsd/vfs/vfs_syscalls.c')},physical_mutation_not_executed=True),indent=2)+'\n');print('PASS30 actual reset cases+5 fixed VFS wrapper cases;3 compiled runtime mutants rejected')
