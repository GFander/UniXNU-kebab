from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);a=p.parse_args();a.receipt_dir.mkdir(parents=True,exist_ok=True);source=a.source/'bsd/vfs/vfs_syscalls.c';s=source.read_text();start=s.index('/* Only fixed owner-measured offline experiment paths');body=s[start:s.index('#endif',start)]
prefix=r"""#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#define NULLVP 0
struct proc{void*p_textvp;};static struct proc first,other;static struct proc*initproc=&first;static int outsider,calls,lower_error;static const char*seen_from,*seen_to;static unsigned seen_flags;
#define AT_FDCWD -2
#define UIO_SYSSPACE 2
#define RENAME_SWAP 2
#define RENAME_EXCL 4
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
static struct proc*current_proc(void){return outsider?&other:initproc;}static int vfs_context_current(void){return 9;}
const char*kebab_offline_plan_path(unsigned index,unsigned kind){if(kind>2)return 0;switch(index){case 0:return kind==0?"MG":kind==1?"MG-backup":0;case 8:return kind==0?"PB":kind==1?"PB-backup":"PB-stage";case 13:return kind==0?"LD":kind==1?"LD-backup":"LD-stage";case 14:return kind==0?"MA":kind==1?"MA-backup":"MA-stage";default:return 0;}}
static int renameat_internal(int context,int fromfd,uintptr_t from,int tofd,uintptr_t to,int segment,unsigned flags){assert(context==9&&fromfd==AT_FDCWD&&tofd==AT_FDCWD&&segment==UIO_SYSSPACE);calls++;seen_from=(const char*)from;seen_to=(const char*)to;seen_flags=flags;return lower_error;}
"""
main=r"""int main(void){unsigned indices[]={0,8,13,14,15,0xffffffff};unsigned checks=0;for(unsigned j=0;j<6;j++)for(unsigned op=0;op<6;op++){unsigned index=indices[j];calls=0;int error=kebab_offline_plan_rename(index,op);bool valid=j<4&&op<5&&!(op==3&&index!=0)&&!(index==0&&(op==0||op==1));assert(error==(valid?0:EINVAL));assert(calls==valid);if(valid){assert(seen_flags==((op==0||op==2)?RENAME_SWAP:RENAME_EXCL));unsigned fk=op==0||op==1?2:op==2||op==4?1:0;unsigned tk=op==1||op==3?1:0;assert(!strcmp(seen_from,kebab_offline_plan_path(index,fk))&&!strcmp(seen_to,kebab_offline_plan_path(index,tk)));}checks++;}outsider=1;calls=0;assert(kebab_offline_plan_rename(8,0)==EPERM&&!calls);outsider=0;first.p_textvp=(void*)1;assert(kebab_offline_plan_rename(8,0)==EPERM&&!calls);first.p_textvp=0;lower_error=13;assert(kebab_offline_plan_rename(8,0)==13&&calls==1);printf("PASS %u fixed VFS dispatch/context/error cases\n",checks+3);return 0;}
"""
with tempfile.TemporaryDirectory()as d:
 d=Path(d);p=d/'actual.c';p.write_text(prefix+body+main);exe=d/'actual';subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
(a.receipt_dir/'offline-vfs-tests.json').write_text(json.dumps(dict(passed=True,actual_wrapper_cases=39,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fixed_paths_and_pre_PID1_context=True,original_rename_dispatch_and_errors_preserved=True,original_native_rename_body_not_modeled=True),indent=2)+'\n')
