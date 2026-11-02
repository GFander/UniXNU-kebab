from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);a=p.parse_args();w=a.receipt_dir;w.mkdir(parents=True,exist_ok=True);source=a.source;text=(source/'osfmk/kern/kebab_bootdiag.c').read_text();at=text.index('/* Exact20H392 initialized storage');observer=text[at:text.index('void kebab_identity_task_report(task_t task);',at)]
prefix='''#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
typedef int vm_map_t;static int mode;static unsigned mg_reads;static char logbuf[4096];
static int model_printf(const char*format,...){va_list a;va_start(a,format);int n=vsnprintf(logbuf+strlen(logbuf),sizeof(logbuf)-strlen(logbuf),format,a);va_end(a);return n;}
#define printf model_printf
static bool kebab_plugin_user_copy(vm_map_t m,uint64_t a,void*out,size_t n){(void)m;if(mode==1)return false;if(a==0x180000058ULL){uint8_t u[16]={0x8f,0x97,0x04,0x52,0xc9,0xe3,0x3e,0x88,0xb1,0x14,0xfe,0xb1,0x86,0x4b,0xc9,0xe0};assert(n==16);if(mode==2)u[0]=0;memcpy(out,u,n);}else{assert(a==0x1e1d7df55ULL&&n==1);*(uint8_t*)out=mode==7?0:1;}return true;}
static bool kebab_ca_u64(vm_map_t m,uint64_t a,uint64_t*out){(void)m;if(mode==3)return false;switch(a){case 0x1e1d7c7d0ULL:*out=mode==4?0:UINT64_MAX;break;case 0x1e1d80040ULL:*out=mode==5?0:UINT64_MAX;break;case 0x1e1d80050ULL:*out=mode==6?0:UINT64_MAX;break;case 0x1e1d80048ULL:*out=2;break;case 0x1e1d7c910ULL:*out=UINT64_MAX;break;case 0x1e1d7cf50ULL:*out=1;break;default:assert(0);}return true;}
static bool kebab_ca_u32(vm_map_t m,uint64_t a,uint32_t*out){(void)m;if(a==0x1e1d7c740ULL){mg_reads++;*out=mode==9?2:1;if(mode==8&&mg_reads==2)*out=2;}else{assert(a==0x1e1d7ffccULL);*out=0;}return true;}
'''
main='''int main(void){for(mode=0;mode<10;mode++){mg_reads=0;logbuf[0]=0;kebab_identity_user_report(1);
if(mode==0){assert(strstr(logbuf,"DeviceClassNumber=1 DeviceClass=iPhone"));assert(strstr(logbuf,"home-button-type=2;"));assert(strstr(logbuf,"cached DeviceClassNumber=1;"));}
if(mode==1||mode==2)assert(strstr(logbuf,"cache UUID refused")&&!strstr(logbuf,"DeviceClassNumber="));
if(mode==3||mode==4||mode==8)assert(!strstr(logbuf,"MG initialized DeviceClassNumber="));
if(mode==5)assert(strstr(logbuf,"DeviceClass=iPhone")&&strstr(logbuf,"device-class uninitialized")&&strstr(logbuf,"home-button-type=2;"));
if(mode==6)assert(strstr(logbuf,"DeviceClass=iPhone")&&strstr(logbuf,"home-button-type uninitialized"));
if(mode==7)assert(!strstr(logbuf,"valid cached DeviceClassNumber="));
if(mode==9)assert(strstr(logbuf,"DeviceClassNumber=2 DeviceClass=iPod"));
}return 0;}
'''
with tempfile.TemporaryDirectory()as tmp:
 tmp=Path(tmp)
 def execute(name,body,ok=True):
  p=tmp/(name+'.c');p.write_text(prefix+body+main);exe=tmp/name;subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],capture_output=True);assert(q.returncode==0)==ok,(name,q.stderr[-500:])
 execute('real-observer',observer)
 mutant=observer.replace('once == UINT64_MAX &&','true &&',1);assert mutant!=observer;execute('uninitialized-mutant',mutant,False)
 mutant=observer.replace('again == *value','true');assert mutant!=observer;execute('changed-return-mutant',mutant,False)
 # Run the actual current copyout wrapper with its per-selector8 bound.
 kernel=(source/'bsd/kern/kern_mib.c').read_text();at=kernel.index('static int\nkebab_identity_out');body=kernel[at:kernel.index('\n#endif',at)]
 pre='#include <assert.h>\n#include <string.h>\n#include <stdio.h>\nstruct sysctl_req{int p,oldptr;char out[65];};\nstatic int fail,logs;\n#define PROC_NULL 0\n#define relaxed 0\n#define os_atomic_inc_orig(p,o) ((*(p))++)\n#define printf(...) (++logs)\nstatic int proc_pid(int p){return p;}\nstatic int model_out(struct sysctl_req*r,const char*v,size_t n){if(fail)return fail;if(r->oldptr)memcpy(r->out,v,n);return 0;}\n#define SYSCTL_OUT(r,v,n) model_out(r,v,n)\n\n#define HW_MACHINE 1\n#define HW_PRODUCT 27\n'
 tail='int main(void){struct sysctl_req r={.p=57,.oldptr=1};fail=14;assert(kebab_identity_out(&r,1,"iPhone10,3")==14&&logs==0);fail=0;r.oldptr=0;assert(kebab_identity_out(&r,1,"D22")==0&&logs==0);r.oldptr=1;r.p=0;assert(kebab_identity_out(&r,1,"D22")==0&&logs==0);r.p=57;for(int i=0;i<70;i++)assert(kebab_identity_out(&r,1,"iPhone10,3")==0&&!strcmp(r.out,"iPhone10,3"));assert(logs==8);return 0;}\n'
 p=tmp/'trace.c';p.write_text(pre+body+tail);exe=tmp/'trace';subprocess.run(['clang','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
(w/'host-0513-tests.json').write_text(json.dumps(dict(passed=True,actual_getter_observer_cases=10,compiled_runtime_mutants_rejected=2,actual_copyout_wrapper_error_and8_per_selector_bound=True,source_sha256={n:hashlib.sha256((source/n).read_bytes()).hexdigest()for n in('osfmk/kern/kebab_bootdiag.c','bsd/kern/kern_mib.c')},user_map_and_atomic_models=True,user_code_invocation=False,physical_verified=False),indent=2)+'\n');print('PASS0513 ten actual observer cases,2 compiled mutants,actual copyout wrapper bound/errors')
