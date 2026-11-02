from pathlib import Path
import hashlib,json,subprocess,tempfile
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);source=args.source/'bsd/kern/kebab_bootdiag.c';s=source.read_text();a=s.index('/* Fixed MobileGestalt cache audit');body=s[a:s.index('/* Optional owner-measured one-file',a)]
prefix='''#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#define MAXPATHLEN 1024
#define NULLVP NULL
#define VNODE_LOOKUP_NOFOLLOW 1
#define KAUTH_VNODE_READ_DATA 2
#define KAUTH_VNODE_READ_SECURITY 2048
#define KAUTH_VNODE_READ_ATTRIBUTES 128
#define KAUTH_ACL_COPYSIZE(p) (8+24*(p)->acl_entrycount)
#define CONFIG_MACF 1
#define ATTR_BIT_MAP_COUNT 5
#define ATTR_CMN_EXTENDED_SECURITY 0x400000
struct attrlist{unsigned bitmapcount,commonattr;};
#define Z_WAITOK 0
#define UIO_READ 1
#define UIO_SYSSPACE 2
typedef int boolean_t;typedef int vfs_context_t;struct vnode{int x;};typedef struct vnode*vnode_t;static struct vnode node;
struct acl{unsigned acl_entrycount,acl_flags;unsigned char ace[24];};static struct acl test_acl={1};static int acl_frees,xattr_calls;static char selected_path[1024];
struct vnode_attr{uint64_t va_data_size,va_fileid;struct timespec va_modify_time,va_change_time;unsigned va_uid,va_gid,va_mode,va_flags,va_dataprotect_class;uint64_t va_supported;struct timespec va_create_time,va_access_time;struct acl*va_acl;};
struct vfsstatfs{char f_fstypename[16],f_mntonname[1024];};static struct vfsstatfs stats;
static int mode,lookups,puts,allocs,frees,reads,attributes;
#define VATTR_INIT(v) memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,k) ((void)0)
#define VATTR_IS_SUPPORTED(v,k) (mode!=7)
static int panic_active(void){return mode==1;}static int ml_at_interrupt_context(void){return 0;}static int get_preemption_level(void){return 0;}static int ml_get_interrupts_enabled(void){return 1;}
static vfs_context_t vfs_context_current(void){return 1;}
static int vnode_lookup(const char*p,int flags,vnode_t*out,vfs_context_t c){(void)c;assert(flags==VNODE_LOOKUP_NOFOLLOW&&(strstr(p,"MobileGestalt")||strstr(p,"mobilegestalt")||strstr(p,"purplebuddy")||strstr(p,"data_ark.plist")));lookups++;strcpy(selected_path,p);if(mode==2)return ENOENT;*out=&node;return 0;}
static int vnode_isreg(vnode_t v){assert(v==&node);return mode!=3;}
static int vnode_authorize(vnode_t v,void*x,int right,vfs_context_t c){(void)v;(void)x;(void)c;assert(right==KAUTH_VNODE_READ_DATA||right==(KAUTH_VNODE_READ_SECURITY|KAUTH_VNODE_READ_ATTRIBUTES));return mode==4||mode==21?EACCES:0;}
static int vnode_getattr(vnode_t v,struct vnode_attr*a,vfs_context_t c){(void)v;(void)c;attributes++;if(mode==5)return EIO;a->va_data_size=mode==6?0:mode==8?1048577:8;a->va_fileid=42;a->va_uid=501;a->va_gid=501;a->va_mode=0600;test_acl.acl_flags=mode==22&&attributes==2?1:0;test_acl.acl_entrycount=mode==23?129:1;a->va_acl=(mode==17||mode==22||mode==23||mode==24)?&test_acl:NULL;if(mode==24&&attributes==2)a->va_data_size=9;a->va_modify_time.tv_sec=mode==16&&attributes>=2?2:1;a->va_change_time.tv_sec=1;return 0;}
static int vnode_mount(vnode_t v){assert(v==&node);return 1;}
static struct vfsstatfs*vfs_statfs(int m){assert(m==1);strcpy(stats.f_fstypename,mode==9?"hfs":"apfs");strcpy(stats.f_mntonname,"/private/var");return &stats;}
static int vn_getpath(vnode_t v,char*p,int*n){(void)v;assert(*n>=64);if(mode==10)return EIO;strcpy(p,mode==18?"/private/var/wrong-target":selected_path);return 0;}
static size_t strlcpy_model(char*d,const char*s,size_t n){snprintf(d,n,"%s",s);return strlen(s);}
#define strlcpy strlcpy_model
static void*kalloc_data(unsigned n,int f){(void)f;if(mode==11)return NULL;allocs++;return malloc(n);}
static void kfree_data(void*p,unsigned n){assert(n==8||n==32||n==64);frees++;free(p);}
static int mac_vnode_check_getattrlist(vfs_context_t c,vnode_t v,struct attrlist*a,int options){(void)c;(void)v;assert(a->bitmapcount==5&&a->commonattr==ATTR_CMN_EXTENDED_SECURITY&&options==0);return mode==26?EACCES:0;}
static int vn_listxattr(vnode_t v,void*buffer,size_t*n,int options,vfs_context_t c){(void)v;(void)c;assert(!buffer&&options==0);xattr_calls++;*n=mode==19?7:0;return mode==20?EACCES:0;}
static void kauth_acl_free(struct acl*a){assert(a==&test_acl);acl_frees++;}
static int vfs_context_ucred(int c){return c;}
static int vn_rdwr(int op,vnode_t v,void*p,unsigned n,int64_t off,int space,int flags,int cred,int*resid,void*x){(void)v;(void)x;(void)cred;assert(op==UIO_READ&&off==0&&space==UIO_SYSSPACE&&flags==0&&n==8);reads++;if(mode==12)return EIO;*resid=mode==13?1:0;memcpy(p,"bplist00",8);if(mode==14&&reads==2)((char*)p)[0]='x';if(mode==15&&reads==2)*resid=1;return 0;}
static void vnode_put(vnode_t v){assert(v==&node);puts++;}
static int model_printf(const char*f,...){(void)f;return 0;}
#define printf model_printf
'''
main='''int main(void){for(mode=0;mode<=16;mode++){lookups=puts=allocs=frees=reads=attributes=0;void*out=(void*)1;uint32_t n=99;int error=kebab_identity_cache_snapshot(0,&out,&n);if(mode==0){assert(!error&&n==8&&!memcmp(out,"bplist00",8));kebab_identity_cache_release(out,n);}else assert(error&&out==NULL&&n==0);assert(puts==(lookups&&mode!=2));assert(allocs==frees);}mode=0;lookups=0;void*out=NULL;uint32_t n=0;assert(kebab_identity_cache_snapshot(24,&out,&n)==EINVAL&&lookups==0);assert(kebab_identity_cache_snapshot(0,NULL,&n)==EINVAL&&lookups==0);assert(kebab_identity_cache_snapshot(0,&out,NULL)==EINVAL&&lookups==0);for(mode=17;mode<=20;mode++){lookups=vnode_put_count=allocs=frees=reads=attributes=acl_frees=xattr_calls=0;out=NULL;n=0;int error=kebab_identity_cache_snapshot(8,&out,&n);if(mode==18){assert(error==EINVAL&&reads==0&&xattr_calls==0);}else{assert(error==0&&reads==2&&xattr_calls==1);kebab_identity_cache_release(out,n);}assert(allocs==frees&&vnode_put_count==1);assert(acl_frees==(mode==17));}mode=0;lookups=0;assert(kebab_identity_cache_snapshot(14,&out,&n)==EAGAIN&&lookups==0);const char*bad[]={"", "/var/containers/Data/System/12345678-1234-1234-1234-123456789ABC/Library/internal/data_ark.plist", "/private/var/containers/Data/System/../../root/Library/internal/data_ark.plist", "/private/var/containers/Data/System/123456781234-1234-1234-123456789ABC/Library/internal/data_ark.plist", "/private/var/containers/Data/System/12345678-1234-1234-1234-123456789ABZ/Library/internal/data_ark.plist", "/private/var/containers/Data/System/12345678-1234-1234-1234-123456789ABC/Library/internal/activation_record.plist"};for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){kebab_offline_remember_data_ark(bad[i]);assert(kebab_offline_data_ark_ready==0);}const char*good="/private/var/containers/Data/System/12345678-1234-1234-1234-123456789ABC/Library/internal/data_ark.plist";kebab_offline_remember_data_ark(good);assert(kebab_offline_data_ark_ready==2&&!strcmp(kebab_offline_data_ark_path,good));kebab_offline_remember_data_ark("/private/var/containers/Data/System/87654321-1234-1234-1234-123456789ABC/Library/internal/data_ark.plist");assert(!strcmp(kebab_offline_data_ark_path,good));const int acl_modes[]={17,21,22,23,24,25,26};for(unsigned j=0;j<7;j++){mode=acl_modes[j];lookups=vnode_put_count=allocs=frees=reads=attributes=acl_frees=0;out=NULL;n=0;int error=kebab_identity_cache_snapshot(16,&out,&n);if(mode==17||mode==25){assert(!error&&n==(mode==17?64:32));uint32_t*header=out;assert(header[0]==0x314c4341&&header[1]==1&&header[2]==14&&header[3]==n-32);kebab_identity_cache_release(out,n);}else assert(error&&out==NULL&&n==0);assert(vnode_put_count==1&&allocs==frees);assert(acl_frees==(mode==17||mode==22||mode==24?2:mode==23?1:0));}return 0;}
'''
with tempfile.TemporaryDirectory()as d:
 d=Path(d)
 prefix=prefix.replace('puts','vnode_put_count');main=main.replace('puts','vnode_put_count')
 for name,actual,ok in [('actual',body,True),('missing-auth',body.replace('vnode_authorize(vnode, NULL, KAUTH_VNODE_READ_DATA, context)','0'),False),('accept-changing-bytes',body.replace('residual || memcmp(data, check, length)','residual'),False)]:
  p=d/(name+'.c');p.write_text(prefix+actual+main);exe=d/name;subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);result=subprocess.run([str(exe)],capture_output=True);assert(result.returncode==0)==ok,(name,result.stderr[-800:])
(w/'cache-snapshot-tests.json').write_text(json.dumps(dict(passed=True,actual_reader_cases=40,actual_ACL_cases=7,compiled_runtime_mutants_rejected=2,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),vnode_allocator_credentials_and_filesystem_models=True,physical_files_not_read_yet=True),indent=2)+'\n');print('PASS40 actual readonly-cache reader cases and2 compiled runtime mutants')
