"""Check zero-behavior extraction, then execute the actual C bus under MMIO faults."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

r=Path(sys.argv[1])
before=(r/'before/bsd/dev/kebab_spmi.c').read_text()
client=(r/'after/bsd/dev/kebab_spmi.c').read_text()
bus=(r/'after/bsd/dev/kebab_spmi_bus.c').read_text()
header=(r/'after/bsd/dev/kebab_spmi_bus.h').read_text()
def functions(s):
    found={}
    for m in re.finditer(r'^(?:static )?(?:uint32_t|int|void|bool|const char \*)\n(kebab_\w+)\([^;{]*\)\n\{',s,re.M):
        end=s.index('\n}',m.end())+2
        found[m[1]]=s[m.start():end].removeprefix('static ')
    return found
old=functions(before);new=functions(client+'\n'+bus)
assert set(old)<=set(new)
for name,body in old.items():
    got=new[name]
    if name=='kebab_spmi_init':
        got=got.replace('\tkebab_spmi_bus_map_arbiter();',
            '\tspmi.core = ml_io_map(KEBAB_SPMI_CORE_PA, KEBAB_SPMI_CORE_SIZE);\n'
            '\tspmi.obsrvr = ml_io_map(KEBAB_SPMI_OBSRV_PA, KEBAB_SPMI_OBSRV_SIZE);\n'
            '\tspmi.cnfg = ml_io_map(KEBAB_SPMI_CNFG_PA, KEBAB_SPMI_CNFG_SIZE);')
        got=got.replace('kebab_spmi_bus_map_channel(apid)','ml_io_map(slice, KEBAB_SPMI_CHNL_SLICE)')
    assert re.sub(r'\s+','',body)==re.sub(r'\s+','',got),name
printf=r'printf\(\s*("(?:\\.|[^"\\])*")'
assert sorted(re.findall(printf,before))==sorted(re.findall(printf,client+bus))
assert not re.search(r'\b(IOLock|lck_|IOMalloc|malloc|new |IOService)',bus)
defines=lambda s:dict(re.findall(r'^#define (KEBAB_SPMI_\w+)\s+([^\n]+)',s,re.M))
assert defines(before)==defines(header)

# Host uses the production transfer code; only ARM barrier opcodes and kernel
# includes are replaced. Kernel compilation separately keeps the real barriers.
h=re.sub(r'^#include .*\n','',header,flags=re.M)
b=re.sub(r'^#include .*\n','',bus,flags=re.M)
b=b.replace('#if defined(KEBAB)','').replace('#endif /* KEBAB */','')
b=b.replace('__asm__ volatile ("dmb sy" ::: "memory");','__asm__ volatile ("" ::: "memory");')
b=b.replace('__asm__ volatile ("dsb sy" ::: "memory");','__asm__ volatile ("" ::: "memory");')
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t vm_offset_t;typedef uintptr_t vm_size_t;
static unsigned delays,maps;
static uint32_t core[0x1100/4],obs[0x100000/4],cnfg[0x26000/4],channel[0x1000/4];
static void delay(unsigned n){delays+=n;}
vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t size){
 ++maps;
 if(pa==0x0c440000 && size==0x1100)return (uintptr_t)core;
 if(pa==0x0e600000 && size==0x100000)return (uintptr_t)obs;
 if(pa==0x0c40a000 && size==0x26000)return (uintptr_t)cnfg;
 assert(pa==0x0c6d0000 && size==0x1000);return (uintptr_t)channel;
}
'''
main=r'''
int main(void){
 kebab_spmi_bus_map_arbiter();assert(maps==3);
 core[(0x900+13*4)/4]=0x01000800;cnfg[(0x700+13*4)/4]=0;
 core[(0x900+1*4)/4]=0x00000800;cnfg[(0x700+1*4)/4]=1;
 struct kebab_spmi_match found[1];
 assert(kebab_spmi_scan(0x008,found,1)==2 && found[0].apid==1 && kebab_spmi_nonzero==2);
 assert(kebab_spmi_scan(0xf00,found,1)==0);
 uint32_t apid=0;assert(kebab_spmi_reader(8,&apid)==0 && apid==13);
 uint8_t value=0x99;unsigned off=13*0x80;
 obs[(off+8)/4]=1;obs[(off+0x18)/4]=4;
 assert(kebab_spmi_obs_read_ex(apid,0,0x805,1,false,true,&value)==0 && value==4);
 assert(obs[off/4]==0x78000050 && delays==0);
 assert(kebab_spmi_obs_read_ex(512,0,0x805,1,false,true,&value)==-1);
 assert(kebab_spmi_obs_read_ex(apid,0,0xc510,1,false,true,&value)==-1);
 obs[(off+8)/4]=0;value=0x99;
 assert(kebab_spmi_obs_read_ex(apid,0,0x805,1,false,true,&value)==-1 && value==0x99 && delays==1000);
 obs[(off+8)/4]=5;
 assert(kebab_spmi_obs_read_ex(apid,0,0x805,1,false,true,&value)==-1 && value==0x99);
 kebab_spmi_reader_forget(8);assert(!spmi.readers[0].valid);
 assert(kebab_spmi_rw_write(13,0,0x88f,4)==-1);
 spmi.chnl=kebab_spmi_bus_map_channel(13);spmi.chnl_mapped=true;spmi.chnl_apid=13;
 channel[8/4]=1;assert(kebab_spmi_rw_write(13,0,0x88f,4)==0);
 assert(channel[0x10/4]==4 && channel[0]==0x8f0 && maps==4);
 cnfg[(0x700+13*4)/4]=1;channel[0]=0;
 assert(kebab_spmi_rw_write(13,0,0x88f,6)==-1 && channel[0]==0);
 assert(!kebab_spmi_version_known(0) && !kebab_spmi_version_known(UINT32_MAX));
 assert(strcmp(kebab_spmi_family(0x50010000),"v5")==0);
 puts("PASS: unchanged production functions/logs/constants; real bus mapping, APID bounds/cache, timeout, denied and owned write");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(prefix+h+b+main)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=address,undefined',
        str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=15)
