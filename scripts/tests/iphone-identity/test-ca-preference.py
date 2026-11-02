from pathlib import Path
import hashlib,json,subprocess,tempfile
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);source=args.source/'bsd/kern/kebab_bootdiag.c';s=source.read_text();a=s.index('/* Owner-measured, one-file original CA preference transaction');body=s[a:s.index('\nvoid\nkebab_process_report(void)',a)]
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
#define MAXPATHLEN 1024
#define FREAD 1
#define FWRITE 2
#define O_CREAT 256
#define O_EXCL 512
#define O_NOFOLLOW 1024
#define FWASWRITTEN 2048
#define UIO_WRITE 1
#define UIO_SYSSPACE 2
typedef char*caddr_t;typedef void*DTEntry;typedef int vfs_context_t;
struct vnode{int index;};typedef struct vnode*vnode_t;static struct vnode nodes[10];
struct proc{void*p_textvp;};static struct proc initial,other;static struct proc*initproc=&initial;
struct vnode_attr{uint64_t va_data_size,va_fileid;unsigned va_uid,va_gid,va_mode,va_dataprotect_class;};struct vfsstatfs{char f_fstypename[16],f_mntonname[128];};static struct vfsstatfs stats;
static const char*const kebab_mg_cache_paths[]={"a","b","c","d","e","original","backup","stage"};
static const unsigned char old_data[]="old-dictionary",new_data[]="new-dictionary-with-SDR8";
static unsigned char plan[512];static int mode,gets,puts_count,opened,closed,allocated,released,writes,moved,stage,backup,old_present,syncs,snapshots;
#define VATTR_INIT(v) memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,k) ((void)0)
#define VATTR_IS_SUPPORTED(v,k) (mode!=23 && !(mode==49&&!strcmp(#k,"va_dataprotect_class")) && !(mode==52&&(v)->va_fileid==99&&!strcmp(#k,"va_dataprotect_class")))
#define VATTR_SET(a,k,v) ((a)->k=v)
static struct proc*current_proc(void){return mode==13?&other:initproc;}
static int panic_active(void){return mode==15;}static int ml_at_interrupt_context(void){return mode==16;}static int get_preemption_level(void){return mode==17;}static int ml_get_interrupts_enabled(void){return mode!=18;}
static int PE_parse_boot_argn(const char*p,void*out,unsigned n){assert(!strcmp(p,"-cf")&&n==4);*(int*)out=mode!=20;return mode!=19;}
static int vfs_context_current(void){return 9;}static int vfs_context_ucred(int c){assert(c==9);return 9;}
static int SecureDTLookupEntry(void*x,const char*p,DTEntry*out){(void)x;assert(!strcmp(p,"/chosen"));*out=(void*)1;return mode==1?-1:0;}
static int SecureDTGetProperty(DTEntry e,const char*p,const void**out,unsigned*n){assert(e==(void*)1&&!strcmp(p,"hios-ca-sdr-preference"));*out=plan;*n=112+sizeof(new_data)-1;if(mode==2)*n=111;if(mode==10)*n-=1;return 0;}
static int vnode_lookup(const char*p,int f,vnode_t*out,int c){assert(f==1&&c==9);int i=!strcmp(p,"original")?5:!strcmp(p,"backup")?6:!strcmp(p,"stage")?7:9;if(i==5&&(!old_present||mode==21))return ENOENT;if(i==6&&!backup){if(mode==34)return EACCES;return ENOENT;}if(i==7&&!stage)return ENOENT;nodes[i].index=i;*out=&nodes[i];gets++;return 0;}
static int vnode_isreg(vnode_t v){return v->index!=9&&mode!=22;}
static int vnode_isdir(vnode_t v){assert(v->index==9);return mode!=35;}
static int vnode_getattr(vnode_t v,struct vnode_attr*a,int c){assert(c==9);if(v->index==7){a->va_fileid=99;a->va_data_size=0;a->va_uid=mode==50?0:501;a->va_gid=501;a->va_mode=0100600;a->va_dataprotect_class=(mode==39||mode==51)?3:4;return 0;}assert(v->index==5);a->va_dataprotect_class=4;a->va_fileid=mode==30?43:42;a->va_data_size=mode==31?10:sizeof(old_data)-1;a->va_uid=mode==24?0:501;a->va_gid=mode==25?0:501;a->va_mode=mode==26?0100644:0100600;return 0;}
static int vnode_mount(vnode_t v){(void)v;return 1;}
static struct vfsstatfs*vfs_statfs(int m){assert(m==1);strcpy(stats.f_fstypename,mode==27?"hfs":"apfs");strcpy(stats.f_mntonname,mode==28?"/":"/private/var");return &stats;}
static int vn_getpath(vnode_t v,char*p,int*n){(void)n;strcpy(p,mode==29?"wrong":v->index==9?"/private/var/mobile/Library/Preferences":"original");return 0;}
static void vnode_put(vnode_t v){assert(v);puts_count++;}
static int kebab_identity_cache_snapshot(unsigned i,void**out,uint32_t*n){assert(i>=5&&i<=7);snapshots++;bool old=i==6||(i==5&&old_present==1);*n=old?sizeof(old_data)-1:sizeof(new_data)-1;*out=malloc(*n);allocated++;memcpy(*out,old?old_data:new_data,*n);if((mode==32&&i==5&&old)||(mode==43&&i==7)||(mode==45&&i==6)||(mode==47&&i==5&&!old))((char*)*out)[0]='X';return 0;}
static void kebab_identity_cache_release(void*p,uint32_t n){assert(p&&n);released++;free(p);}
#define Z_WAITOK 1
#define Z_NOFAIL 2
#define LOOKUP 3
#define OP_OPEN 4
#define NOFOLLOW 5
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
struct nameidata{vnode_t ni_vp;const char*path;};
#define kalloc_type(T,f) ((T*)calloc(1,sizeof(T)))
#define kfree_type(T,p) free(p)
#define NDINIT(nd,o,op,fl,space,p,c) do{assert((o)==3&&(op)==4&&(fl)==5&&(space)==2&&(c)==9);(nd)->path=(const char*)(p);}while(0)
static int vn_open_auth(struct nameidata*nd,int*flags,struct vnode_attr*a,vnode_t auth){assert(!strcmp(nd->path,"stage")&&*flags==(FREAD|FWRITE|O_CREAT|O_EXCL|O_NOFOLLOW)&&a->va_mode==0600&&a->va_uid==501&&a->va_gid==501&&a->va_dataprotect_class==4&&auth==NULLVP);if(mode==36)return EACCES;assert(!stage);stage=1;nodes[7].index=7;nd->ni_vp=&nodes[7];opened++;return 0;}
static int vnode_close(vnode_t v,int f,int c){assert(v->index==7&&f==(FREAD|FWRITE|FWASWRITTEN)&&c==9);closed++;return mode==42?EIO:0;}
static int vn_rdwr(int op,vnode_t v,void*p,unsigned n,int64_t off,int space,int f,int cred,int*resid,void*x){(void)x;assert(op==UIO_WRITE&&v->index==7&&n==sizeof(new_data)-1&&!memcmp(p,new_data,n)&&!off&&space==2&&!f&&cred==9);writes++;*resid=mode==38?1:0;return mode==37?EIO:0;}
static int VNOP_FSYNC(vnode_t v,int f,int c){assert(v&&f==1&&c==9);syncs++;return (mode==40&&v->index==7)||(mode==41&&syncs==2)||(mode==48&&syncs==3)?EIO:0;}
static int kebab_ca_preference_move(unsigned op){if(op==0){if(mode==44)return EACCES;assert(old_present==1&&!backup);old_present=0;backup=1;moved++;}else if(op==1){if(mode==46)return EACCES;assert(!old_present&&stage);old_present=2;stage=0;moved++;}else{assert(op==2&&!old_present&&backup);old_present=1;backup=0;moved++;}return 0;}
static int model_printf(const char*f,...){(void)f;return 0;}
#define printf model_printf
'''
main=r'''int main(void){for(mode=0;mode<=52;mode++){
 memset(plan,0,sizeof(plan));uint32_t h[]={0x44534143,1,5,sizeof(old_data)-1};uint64_t id=42;uint32_t n=sizeof(new_data)-1;memcpy(plan,h,16);memcpy(plan+16,&id,8);SHA256(old_data,sizeof(old_data)-1,plan+24);memcpy(plan+56,&n,4);SHA256(new_data,n,plan+60);memcpy(plan+112,new_data,n);
 if(mode==3)plan[0]=0;if(mode==4)plan[4]=2;if(mode==5)plan[8]=1;if(mode==6)memset(plan+12,0,4);if(mode==7)memset(plan+16,0,8);if(mode==8)memset(plan+56,0,4);if(mode==9){uint32_t bad=4097;memcpy(plan+56,&bad,4);}if(mode==11)plan[111]=1;if(mode==12)plan[112]^=1;
 initial.p_textvp=mode==14?(void*)1:NULL;gets=puts_count=opened=closed=allocated=released=writes=moved=stage=backup=syncs=snapshots=0;old_present=1;if(mode==33)backup=1;
 int error=kebab_ca_sdr_preference_once();bool noop=mode==1||mode==30||mode==31;
 assert((error==0)==(mode==0||noop));assert(allocated==released&&gets==puts_count&&opened==closed);
 if(mode==0){assert(old_present==2&&backup==1&&stage==0&&moved==2&&writes==1&&syncs==3&&snapshots==4);}
 else if(mode==45||mode==46){assert(old_present==1&&!backup&&stage&&moved==2);}
 else if(mode==47||mode==48){assert(old_present==2&&backup&&moved==2);}
 else{assert(old_present==1&&!moved);if(mode<=35||mode==39||mode>=49)assert(!writes);}
 }return 0;}
'''
with tempfile.TemporaryDirectory()as d:
 d=Path(d)
 for name,actual,ok in [('actual',body,True),('skip-old-digest',body.replace('error = kebab_ca_preference_digest(5, header[3], old_hash);','error = 0;'),False),('nonexclusive-create',body.replace('FREAD | FWRITE | O_CREAT | O_EXCL | O_NOFOLLOW','FREAD | FWRITE | O_CREAT | O_NOFOLLOW'),False),('ignore-owner',body.replace('attr.va_uid != 501','false').replace('created.va_uid != attr.va_uid','false'),False),('ignore-class-match',body.replace('created.va_dataprotect_class != attr.va_dataprotect_class','false'),False),('omit-class-at-create',body.replace('VATTR_SET(&attr, va_dataprotect_class, protection);',''),False)]:
  p=d/(name+'.c');p.write_text(prefix+actual+main);exe=d/name;subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-lcrypto','-Wno-deprecated-declarations','-o',str(exe)],check=True);result=subprocess.run([str(exe)],capture_output=True);assert(result.returncode==0)==ok,(name,result.stderr[-1400:])
(w/'host-0522-ca-preference-tests.json').write_text(json.dumps(dict(passed=True,actual_transaction_cases=53,compiled_runtime_mutants_rejected=5,original_functions_modeled_not_physical=True,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n');print('PASS53 actual configuration/protection paths +5 runtime mutants')
