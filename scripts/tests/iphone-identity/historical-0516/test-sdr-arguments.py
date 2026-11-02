from pathlib import Path
import hashlib,json,subprocess
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);src=(args.source/'bsd/kern/kern_exec.c').read_text();a=src.index('static int\nkebab_fb_exec_arguments(');b=src.index('\n#endif',a);actual=src[a:b]
prefix=r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define NCARGS 262144
#define IMGPF_IS_64BIT_ADDR 1
#define UIO_SYSSPACE 1
#define TRUE 1
#define CAST_USER_ADDR_T(x) ((uintptr_t)(x))
struct cn {int cn_namelen;char *cn_nameptr;};struct nd {struct cn ni_cnd;};
struct image_params {int ip_argc,ip_envc,ip_strspace,ip_argspace,ip_flags;char *ip_startargv,*ip_endargv,*ip_endenvv,*ip_strendp;struct nd *ip_ndp;};
typedef struct {uint8_t uuid[16];}load_result_t;
static int enabled=1;
static int PE_parse_boot_argn(const char*k,unsigned*v,size_t n){assert(!strcmp(k,"-cf")&&n==4);*v=enabled;return enabled;}
static int exec_add_user_string(struct image_params*p,uintptr_t a,int space,int arg){assert(space==UIO_SYSSPACE&&arg);size_t n=strlen((char*)a)+1;if(n>(size_t)p->ip_strspace||n>(size_t)p->ip_argspace)return E2BIG;memcpy(p->ip_strendp,(char*)a,n);p->ip_strendp+=n;p->ip_strspace-=n;p->ip_argspace-=n;return 0;}
'''
tests=r'''
static uint8_t storage[8192];static struct nd nd={{10,"backboardd"}};
static const load_result_t original={{0x70,0xb7,0x23,0x40,0x73,0x7a,0x39,0x73,0x8b,0x47,0x4e,0x99,0x70,0xf4,0xec,0xb9}};
static struct image_params setup(unsigned shift,int envs,int explicit_setting){
 memset(storage,0xa5,sizeof(storage));char*p=(char*)storage+32+shift;struct image_params v={0};v.ip_startargv=p;
 const char*args[]={"/usr/libexec/backboardd","-CAEnableDeepFramebuffer","YES"};
 v.ip_argc=explicit_setting?3:1;for(int i=0;i<v.ip_argc;i++){strcpy(p,args[i]);p+=strlen(p)+1;}
 v.ip_endargv=p;v.ip_envc=envs;
 for(int i=0;i<envs;i++){const char*e=i?"UNICODE=UTF8":"KEEP=original";strcpy(p,e);p+=strlen(p)+1;}
 v.ip_strspace=(int)((char*)storage+sizeof(storage)-32-p);v.ip_argspace=v.ip_strspace-128;
 while(v.ip_strspace%8){*p++=0;v.ip_strspace--;v.ip_argspace--;}
 v.ip_endenvv=v.ip_strendp=p;v.ip_flags=1;v.ip_ndp=&nd;return v;
}
static void check(struct image_params*p,int explicit_setting,int envs){
 assert(p->ip_argc==3&&p->ip_envc==envs+1);char*c=p->ip_startargv;
 assert(!strcmp(c,"/usr/libexec/backboardd"));c+=strlen(c)+1;
 assert(!strcmp(c,"-CAEnableDeepFramebuffer"));c+=strlen(c)+1;
 assert(!strcmp(c,explicit_setting?"YES":"NO"));c+=strlen(c)+1;assert(c==p->ip_endargv);
 for(int i=0;i<envs;i++){assert(!strcmp(c,i?"UNICODE=UTF8":"KEEP=original"));c+=strlen(c)+1;}
 assert(!strcmp(c,"CA_DISABLE_FRAMEBUFFER_COMPRESSION=1"));c+=strlen(c)+1;
 assert(p->ip_endenvv==p->ip_strendp&&p->ip_endenvv>=c&&p->ip_endenvv-c<8&&p->ip_strspace%8==0);
 for(;c<p->ip_endenvv;c++)assert(*c==0);
 for(unsigned i=0;i<32;i++)assert(storage[i]==0xa5&&storage[sizeof(storage)-1-i]==0xa5);
}
int main(void){unsigned count=0;
 for(unsigned shift=0;shift<8;shift++)for(int e=0;e<=2;e+=2)for(int explicit_setting=0;explicit_setting<=1;explicit_setting++){
  struct image_params p=setup(shift,e,explicit_setting);int oldstr=p.ip_strspace,oldarg=p.ip_argspace;
  char oldenv[128];size_t envbytes=p.ip_endenvv-p.ip_endargv;memcpy(oldenv,p.ip_endargv,envbytes);
  assert(kebab_fb_exec_environment(&p,&original)==0);check(&p,explicit_setting,e);
  assert(oldarg-p.ip_argspace==oldstr-p.ip_strspace+(explicit_setting?8:24));count++;
 }
 for(unsigned gate=0;gate<5;gate++){
  struct image_params p=setup(0,2,0),before=p;load_result_t id=original;uint8_t copy[8192];memcpy(copy,storage,sizeof(copy));
  if(gate==0)id.uuid[0]^=1;if(gate==1)p.ip_flags=0;if(gate==2)p.ip_ndp=NULL;if(gate==3){nd.ni_cnd.cn_nameptr="other_name";}if(gate==4)enabled=0;before=p;
  assert(kebab_fb_exec_environment(&p,&id)==0&&!memcmp(&p,&before,sizeof(p))&&!memcmp(copy,storage,sizeof(copy)));enabled=1;nd.ni_cnd.cn_nameptr="backboardd";count++;
 }
 for(unsigned bad=0;bad<8;bad++){
  struct image_params p=setup(0,2,0);if(bad==0)p.ip_argc=-1;if(bad==1)p.ip_envc=-1;if(bad==2)p.ip_endargv=p.ip_startargv;if(bad==3)p.ip_strendp--;
  if(bad==4)p.ip_strspace=0;if(bad==5)p.ip_argspace=0;if(bad==6)p.ip_argc=NCARGS;if(bad==7)p.ip_endenvv=p.ip_endargv;
  uint8_t copy[8192];memcpy(copy,storage,sizeof(copy));assert(kebab_fb_exec_arguments(&p)!=0&&!memcmp(copy,storage,sizeof(copy)));count++;
 }
 /* Repeated exec policy application preserves explicit preference; existing
  * compression env remains single and the original copyout vectors stay valid. */
 struct image_params p=setup(3,2,0);assert(!kebab_fb_exec_environment(&p,&original));struct image_params before=p;uint8_t copy[8192];memcpy(copy,storage,sizeof(copy));assert(!kebab_fb_exec_environment(&p,&original));assert(!memcmp(&p,&before,sizeof(p))&&!memcmp(copy,storage,sizeof(copy)));count++;
 printf("qualified integration cases=%u\n",count);return 0;}
'''
out=w/'deep-argument-tests';out.mkdir(exist_ok=True)
variants={'actual':actual,'missing-argc':actual.replace('imgp->ip_argc += 2;','imgp->ip_argc += 1;'),'lost-environment':actual.replace('memmove(imgp->ip_endargv + sizeof(arguments), imgp->ip_endargv, environment_bytes);','(void)environment_bytes;'),'lost-identity-gate':actual.replace('memcmp(load_result->uuid, uuid, sizeof(uuid)) ||','0 ||'),'wrong-format-pref':actual.replace('Framebuffer\0NO','Framebuffer\0YES')}
# Last mutation uses source C escape characters, not a Python embedded NUL.
variants['wrong-format-pref']=actual.replace('Framebuffer\\0NO','Framebuffer\\0YES')
receipts=[]
for name,body in variants.items():
 assert name=='actual'or body!=actual,name
 p=out/(name+'.c');p.write_text(prefix+body+tests)
for name in variants:
 p=subprocess.run(['clang','-std=c11','-D_GNU_SOURCE','-fsanitize=address,undefined','-fno-omit-frame-pointer','-O1',str(out/(name+'.c')),'-o',str(out/name)],capture_output=True,text=True);assert p.returncode==0,p.stderr
 p=subprocess.run([str(out/name)],capture_output=True,text=True);(out/(name+'.txt')).write_text(p.stdout+p.stderr)
 if name=='actual':assert p.returncode==0,p.stderr+p.stdout
 else:assert p.returncode!=0,(name,p.stdout)
 receipts.append(dict(variant=name,exit=p.returncode,passed=(p.returncode==0 if name=='actual'else p.returncode!=0)))
q=dict(passed=True,version='0516',actual_cases=46,compiled_runtime_mutants=4,source_sha256=hashlib.sha256(src.encode()).hexdigest(),results=receipts,limitations='Host argv/env/copyout layout, original native Mac CF Boolean argument-domain reference; no original iOS CA or device proof')
(w/'host-0516-tests.json').write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
