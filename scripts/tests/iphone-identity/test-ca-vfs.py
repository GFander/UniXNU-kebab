from pathlib import Path
import hashlib,json,subprocess,tempfile
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);args=p.parse_args();w=args.receipt_dir;w.mkdir(parents=True,exist_ok=True);source=args.source/'bsd/vfs/vfs_syscalls.c';s=source.read_text();functions=[]
for start in('int kebab_ca_preference_move(unsigned);',):
 a=s.index(start);functions.append(s[a:s.index('\n#endif',a)])
prefix=r'''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#define NULLVP NULL
#define AT_FDCWD -2
#define UIO_SYSSPACE 2
#define RENAME_EXCL 4
#define VNODE_LOOKUP_NOFOLLOW 1
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
typedef int vfs_context_t;struct vnode{int x;};typedef struct vnode*vnode_t;static struct vnode node;
struct vnode_attr{unsigned va_uid,va_gid,va_mode;};
#define VATTR_INIT(a) memset(a,0,sizeof(*(a)))
#define VATTR_SET(a,k,v) ((a)->k=v)
struct proc{void*p_textvp;};static struct proc initial,other;static struct proc*initproc=&initial;static int bad,called,forced,lookups,puts_count,owner_calls,not_regular;
static struct proc*current_proc(void){return bad?&other:&initial;}
static int vfs_context_current(void){return 9;}
static int renameat_internal(int ctx,int fd,uintptr_t from,int tofd,uintptr_t to,int seg,unsigned flags){assert(ctx==9&&fd==-2&&tofd==-2&&seg==2&&flags==4);const char*f=(void*)from,*t=(void*)to;const char*base="/private/var/mobile/Library/Preferences/com.apple.backboardd.plist";assert(!strncmp(f,base,strlen(base))&&!strncmp(t,base,strlen(base)));assert(!strcmp(t,base)||strstr(t,".hios-before-sdr8-20261102"));called++;return forced;}
static int vnode_lookup(const char*p,int f,vnode_t*out,int c){assert(strstr(p,".hios-sdr8-stage-20261102")&&f==1&&c==9);lookups++;if(forced==ENOENT)return forced;*out=&node;return 0;}
static int vnode_isreg(vnode_t v){assert(v==&node);return !not_regular;}
static int chmod_vnode(int c,vnode_t v,struct vnode_attr*a){assert(c==9&&v==&node&&a->va_uid==501&&a->va_gid==501&&a->va_mode==0600);owner_calls++;return forced;}
static void vnode_put(vnode_t v){assert(v==&node);puts_count++;}
'''
main=r'''int main(){bad=1;assert(kebab_ca_preference_move(0)==EPERM&&!called);bad=0;initial.p_textvp=(void*)1;assert(kebab_ca_preference_move(1)==EPERM&&!called);initial.p_textvp=NULL;assert(kebab_ca_preference_move(3)==EINVAL&&!called);for(unsigned op=0;op<3;op++){forced=EACCES;assert(kebab_ca_preference_move(op)==EACCES);forced=EEXIST;assert(kebab_ca_preference_move(op)==EEXIST);forced=0;assert(!kebab_ca_preference_move(op));}assert(called==9);return 0;}'''

with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'actual.c';p.write_text(prefix+'\n'.join(functions)+main);exe=p.with_suffix('');subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
(w/'host-0522-vfs-tests.json').write_text(json.dumps(dict(passed=True,original_exclusive_rename_dispatch_and_errors_preserved=True,pre_PID1_context_guard=True,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n');print('PASS actual fixed VFS wrapper guards + original dispatch/error propagation')
