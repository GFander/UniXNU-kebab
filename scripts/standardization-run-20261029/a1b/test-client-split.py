from pathlib import Path
import re
import subprocess
import sys
import tempfile

r=Path(sys.argv[1]);d=r/'after/bsd/dev'
old='\n'.join(p.read_text() for p in (r/'before/bsd/dev').glob('*.c'))
new='\n'.join(p.read_text() for p in d.glob('*.c'))
def fns(s):
    out={}
    for m in re.finditer(r'(?m)^(?:static )?[^\n;]+\n(kebab_\w+)\([^;{]*\)\n\{',s):
        end=s.index('\n}',m.end())+2;out[m[1]]=s[m.start():end].removeprefix('static ')
    return out
before=fns(old);after=fns(new)
def norm(s):
    s=s.replace('kebab_spmi_bus_set_verbose(pon.verbose);','')
    for a,b in [
      ('kebab_spmi_rail_enable_read_locked','kebab_spmi_rail_enable_read'),
      ('kebab_rpmh_read32','kebab_spmi_rd'),('kebab_rpmh_write32','kebab_spmi_wr'),
      ('pon.write_gate_parsed','spmi.write_gate_parsed'),('pon.write_gate','spmi.write_gate'),('pon.verbose','spmi.verbose'),
      ('kebab_spmi_bus_next_attempt()','++spmi.read_attempt'),('kebab_spmi_bus_ready()','spmi.mapped'),
      ('kebab_spmi_bus_version()','spmi.version'),('kebab_spmi_bus_verbose()','spmi.verbose'),
      ('kebab_spmi_bus_read_version()','kebab_spmi_rd(spmi.core, KEBAB_SPMI_VERSION)'),
      ('kebab_spmi_bus_mapping(apid)','kebab_spmi_rd(spmi.core, KEBAB_SPMI_APID_MAP + 4u * apid)'),
      ('kebab_spmi_bus_lock()','lck_mtx_lock(&kebab_spmi_normal_lock)'),
      ('kebab_spmi_bus_unlock()','lck_mtx_unlock(&kebab_spmi_normal_lock)'),
      ('(uint32_t)kebab_spmi_bus_channel_physical(matches[i].apid)','KEBAB_SPMI_CHNLS_PA + 0x10000u * matches[i].apid')]:
        s=s.replace(a,b)
    return re.sub(r'\s+','',s)
for name,body in before.items():
    if name=='kebab_spmi_init':continue
    target='kebab_spmi_rail_enable_read_locked' if name=='kebab_spmi_rail_enable_read' else name
    assert target in after,name
    assert norm(body)==norm(after[target]),name
printf=r'printf\(\s*("(?:\\.|[^"\\])*")'
assert sorted(re.findall(printf,old))==sorted(re.findall(printf,new)),'printf formats changed'
defines=lambda s:dict(re.findall(r'^#define (KEBAB_\w+)\s+([^\n]+)',s,re.M))
olddefs=defines(old+'\n'+(r/'before/bsd/dev/kebab_spmi_bus.h').read_text())
newdefs=defines(new+'\n'+'\n'.join(p.read_text() for p in d.glob('*.h')))
for name,value in olddefs.items():
    assert name in newdefs and value.split('/*')[0].strip()==newdefs[name].split('/*')[0].strip(),name
for name in ['kebab_spmi_pon.c','kebab_spmi_keys.c','kebab_spmi_power.c']:
    s=(d/name).read_text()
    assert 'spmi.' not in s and not re.search(r'\b(ml_io_map|kebab_spmi_rd|kebab_spmi_wr)\(',s),name
assert 'vm_offset_t' not in (d/'kebab_spmi_bus.h').read_text()
assert 'struct kebab_spmi_bus_state' not in (d/'kebab_spmi_bus.h').read_text()
build=(r/'after/bsd/conf/files').read_text()
entries=re.findall(r'^bsd/dev/(kebab_spmi(?:_bus|_pon|_keys|_power)?|kebab_rpmh)\.c\b',build,re.M)
assert len(entries)==6 and len(set(entries))==6,('duplicate or missing build inputs',entries)
def clean(s):
    s=re.sub(r'^#include .*\n','',s,flags=re.M)
    s=s.replace('#if defined(KEBAB)','').replace('#endif /* KEBAB */','')
    return re.sub(r'__asm__ volatile \("d[ms]b sy" ::: "memory"\);','__asm__ volatile ("" ::: "memory");',s)
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
typedef uintptr_t vm_offset_t;typedef uintptr_t vm_size_t;
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
static void lck_mtx_lock(pthread_mutex_t*p){assert(!pthread_mutex_lock(p));}
static void lck_mtx_unlock(pthread_mutex_t*p){assert(!pthread_mutex_unlock(p));}
static unsigned maps,vrm_calls,caseid,parsed;
static uint32_t core[0x1100/4],obs[0x100000/4],cnfg[0x26000/4],channel[0x1000/4];
static void delay(unsigned n){(void)n;}
vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t size){
 ++maps;
 if(pa==0x0c440000 && size==0x1100)return caseid==1?0:(uintptr_t)core;
 if(pa==0x0e600000 && size==0x100000)return (uintptr_t)obs;
 if(pa==0x0c40a000 && size==0x26000)return (uintptr_t)cnfg;
 assert(pa==0x0c6d0000 && size==0x1000);return caseid==5?0:(uintptr_t)channel;
}
static bool PE_parse_boot_argn(const char*name,uint32_t*out,unsigned size){
 assert(size==4);++parsed;*out=strcmp(name,"kebab_pon_write")==0 && caseid!=2;return true;
}
static struct { bool write_gate,write_gate_parsed,verbose; } pon;
'''
headers='\n'.join(clean((d/n).read_text()) for n in [
    'kebab_spmi_bus.h','kebab_spmi_clients.h','kebab_pmic_regs.h','kebab_spmi_bus_private.h'])
selected='\n'.join(after[n] for n in ['kebab_spmi_pon_writes_enabled','kebab_spmi_pon_apid','kebab_spmi_pon_prepare','kebab_spmi_init'])
stubs='void kebab_spmi_vrm_init(void){assert(spmi.mapped);++vrm_calls;}\n'
main=r'''
int main(int argc,char**argv){
 assert(argc==2);caseid=(unsigned)atoi(argv[1]);
 core[0]=caseid==3?0x30000000:caseid==6?0xffffffff:0x50010000;
 if(caseid!=7){core[(0x900+13*4)/4]=0x01000800;}
 cnfg[(0x700+13*4)/4]=caseid==4?1:0;
 kebab_spmi_init();kebab_spmi_init();
 bool channelExpected=caseid==0;
 assert(maps==(caseid==0||caseid==5?4:3));
 assert(vrm_calls==(caseid==1?0:1));assert(parsed==(caseid==1?0:2));
 struct kebab_spmi_bus_info info;kebab_spmi_bus_get_info(&info);
 assert(info.mapped==(caseid!=1));assert(info.write_channel_mapped==channelExpected);
 assert(info.version==(caseid==1?0:core[0]));assert(info.ee==0);
 assert(info.resources[0]==0x0c440000 && info.resources[1]==0x1100);
 assert(info.resources[2]==0x0e600000 && info.resources[3]==0x100000);
 assert(info.resources[4]==0x0c40a000 && info.resources[5]==0x26000);
 assert(info.resources[6]==0x0c600000 && info.resources[7]==0x2000000);
 assert(kebab_spmi_bus_channel_physical(13)==0x0c6d0000);
 puts("PASS: actual split init, resource ownership, once-only gates and failed mappings");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(prefix+headers+clean((d/'kebab_spmi_bus.c').read_text())+stubs+selected+main)
    subprocess.run(['cc','-std=c11','-pthread','-Wall','-Wextra','-fsanitize=address,undefined',
        str(p/'test.c'),'-o',str(p/'test')],check=True)
    for case in range(8):
        subprocess.run([str(p/'test'),str(case)],check=True,timeout=15,capture_output=True)
print('PASS: all retained function semantics and printf formats; private state/API separation; 8 init/gate/failure cases')
