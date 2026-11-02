from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);a=p.parse_args();a.receipt_dir.mkdir(parents=True,exist_ok=True)
source=a.source/'osfmk/kern/kebab_bootdiag.c';text=source.read_text();start=text.index('/* Original20H392 DeviceType singleton decision audit.');body=text[start:text.index('void kebab_offline_task_report',start)]
prefix=r"""#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
typedef int task_t;typedef int vm_map_t;typedef int mach_msg_type_number_t;typedef void*task_info_t;
typedef struct {uint64_t all_image_info_addr,all_image_info_size;unsigned all_image_info_format;}task_dyld_info_data_t;
#define TASK_DYLD_INFO_COUNT 3
#define TASK_DYLD_INFO 17
#define KERN_SUCCESS 0
#define TASK_DYLD_ALL_IMAGE_INFO_64 2
#define VM_MAP_NULL 0
static int mode,releases,flag_reads,head_reads;static char logbuf[4096];
static int model_printf(const char*f,...){va_list args;va_start(args,f);int n=vsnprintf(logbuf+strlen(logbuf),sizeof(logbuf)-strlen(logbuf),f,args);va_end(args);return n;}
#define printf model_printf
static int task_info(task_t t,int f,task_info_t out,mach_msg_type_number_t*n){(void)t;(void)f;(void)n;task_dyld_info_data_t*o=out;o->all_image_info_addr=0x2000;o->all_image_info_size=16;o->all_image_info_format=2;return mode==1?5:0;}
static vm_map_t get_task_map_reference(task_t t){assert(t==1);return mode==2?0:1;}
static void vm_map_deallocate(vm_map_t m){assert(m==1);releases++;}
static bool kebab_shell_isa(uint64_t isa,uint64_t expected){return (isa&0xffffffff8ULL)==expected;}
static bool kebab_plugin_image_uuid(vm_map_t m,uint64_t b,uint8_t*u){(void)m;assert(b==0x100000000ULL);static const uint8_t actual[16]={0x00,0x0b,0x12,0x1a,0xe2,0xfe,0x3d,0xa2,0xa4,0x42,0x11,0x64,0xc1,0x81,0xd4,0xec};memcpy(u,actual,16);if(mode==4)u[0]=1;return true;}
static bool kebab_ca_u64(vm_map_t m,uint64_t addr,uint64_t*out){(void)m;if(addr==0x1002304c0ULL)*out=mode==6?0:UINT64_MAX;else if(addr==0x1002304b8ULL)*out=mode==7?0:0x4000;else assert(0);return true;}
static bool kebab_plugin_user_copy(vm_map_t m,uint64_t addr,void*out,size_t n){(void)m;if(mode==3)return false;if(addr==0x2000){assert(n==16);uint64_t h[2]={1ULL|(1ULL<<32),0x3000};head_reads++;if(mode==11&&head_reads==2)h[1]+=8;memcpy(out,h,16);}else if(addr==0x3000){assert(n==24);uint64_t r[3]={0x100000000ULL,0x5000,0};memcpy(out,r,24);}else if(addr==0x1000170d4ULL){assert(n==8);uint64_t code=mode==5?0:0xd65f03c039404800ULL;memcpy(out,&code,8);}else if(addr==0x4000){assert(n==24);uint8_t f[24]={0};uint64_t isa=mode==8?0:0x10022f260ULL;memcpy(f,&isa,8);f[10]=1;f[15]=1;f[18]=mode==9?2:1;flag_reads++;if(mode==10&&flag_reads==2)f[18]=0;memcpy(out,f,24);}else assert(0);return true;}
"""
main=r"""int main(void){for(mode=0;mode<=11;mode++){releases=flag_reads=head_reads=0;logbuf[0]=0;kebab_offline_device_type_report(1);if(mode==0){assert(strstr(logbuf,"should-hactivate=1")&&strstr(logbuf,"iphone=1")&&strstr(logbuf,"NOT activation-state"));}else assert(!strstr(logbuf,"should-hactivate="));assert(releases==(mode!=1&&mode!=2));}return 0;}
"""
with tempfile.TemporaryDirectory()as d:
 d=Path(d)
 for name,b,ok in [('actual',body,True),('skip-instruction-pin',body.replace('code != 0xd65f03c039404800ULL','false'),False),('skip-initialized',body.replace('once != UINT64_MAX','false'),False),('skip-changing-state',body.replace('memcmp(flags,flags2,sizeof(flags))','false'),False)]:
  assert name=='actual' or b!=body
  p=d/(name+'.c');p.write_text(prefix+b+main);exe=d/name;subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],capture_output=True);assert(q.returncode==0)==ok,(name,q.stderr[-700:])
(a.receipt_dir/'offline-decision-tests.json').write_text(json.dumps(dict(passed=True,cases=12,compiled_runtime_mutants_rejected=3,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),original_device_type_memory_model=True,not_original_user_API_invocation=True,not_activation_state=True),indent=2)+'\n')
print('PASS12 actual readonly decision observer cases and3 compiled runtime mutants')
