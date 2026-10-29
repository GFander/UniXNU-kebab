from pathlib import Path
import re

r=Path(__file__).parent
p=r/'after/bsd/dev/kebab_spmi.c'
s=(r/'before/bsd/dev/kebab_spmi.c').read_text()
start=s.index('/* ------------------------------------------------------------ board facts */')
end=s.index('/* ---------------------------------------------------------------- helpers */')
defs=s[start:end]
defs=defs.replace('static struct {','struct kebab_spmi_bus_state {',1)
defs=defs.replace('} spmi;','};\n\nextern struct kebab_spmi_bus_state kebab_spmi_bus;\n#define spmi kebab_spmi_bus',1)
defs=defs.replace('static uint32_t kebab_spmi_nonzero;','extern uint32_t kebab_spmi_nonzero;')
s=s[:start]+'#include "kebab_spmi_bus.h"\n\n'+s[end:]
names=['kebab_spmi_rd','kebab_spmi_wr','kebab_spmi_family','kebab_spmi_version_known',
       'kebab_spmi_scan','kebab_spmi_reader','kebab_spmi_reader_forget',
       'kebab_spmi_obs_read_ex','kebab_spmi_rw_write']
protos=[];functions=[]
for name in names:
    a=s.index('\n'+name+'(')+1
    a=s.rfind('\n',0,a-1)+1
    opening=s.index('{',a);b=opening+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    func=s[a:b]
    assert func.startswith('static '),name
    func=func[len('static '):]
    protos.append(func[:func.index('{')].strip()+';')
    functions.append(func)
    s=s[:a]+s[b:]
mapping='''\tspmi.core = ml_io_map(KEBAB_SPMI_CORE_PA, KEBAB_SPMI_CORE_SIZE);
\tspmi.obsrvr = ml_io_map(KEBAB_SPMI_OBSRV_PA, KEBAB_SPMI_OBSRV_SIZE);
\tspmi.cnfg = ml_io_map(KEBAB_SPMI_CNFG_PA, KEBAB_SPMI_CNFG_SIZE);'''
assert s.count(mapping)==1
s=s.replace(mapping,'\tkebab_spmi_bus_map_arbiter();')
assert s.count('spmi.chnl = ml_io_map(slice, KEBAB_SPMI_CHNL_SLICE);')==1
s=s.replace('spmi.chnl = ml_io_map(slice, KEBAB_SPMI_CHNL_SLICE);',
            'spmi.chnl = kebab_spmi_bus_map_channel(apid);')
# Mapping belongs to the bus. RPMh/RSC remains a different transport and will
# be separated from the PMIC clients in the next single-purpose batch.
s=re.sub(r'\n{4,}','\n\n\n',s)
p.write_text(s,newline='\n')
header='''/* SM8250 SPMI arbiter core. Transitional private header for A1a.
 * A1b narrows client access; no IOKit, allocation or locks on the transfer path.
 * Board values and register semantics are retained from kebab_spmi.c.
 */
#ifndef _KEBAB_SPMI_BUS_H
#define _KEBAB_SPMI_BUS_H
#include <sys/types.h>
#include <mach/vm_types.h>
'''+defs+'\n'+'\n'.join(protos)+'''
void kebab_spmi_bus_map_arbiter(void);
vm_offset_t kebab_spmi_bus_map_channel(uint32_t apid);
#endif
'''
(p.parent/'kebab_spmi_bus.h').write_text(header,newline='\n')
core='''/*
 * SM8250 PMIC arbiter: resource mapping, APID/PPID lookup and bounded transfers.
 * Extracted unchanged from kebab_spmi.c in A1a. Boot preparation maps resources;
 * panic FASTBOOT uses only already mapped state, barriers and bounded polling.
 */
#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include "kebab_spmi_bus.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

struct kebab_spmi_bus_state kebab_spmi_bus;
uint32_t kebab_spmi_nonzero;

'''+ '\n\n'.join(functions)+'''

void
kebab_spmi_bus_map_arbiter(void)
{
'''+mapping+'''
}

vm_offset_t
kebab_spmi_bus_map_channel(uint32_t apid)
{
\tvm_offset_t slice = KEBAB_SPMI_CHNLS_PA + 0x10000u * apid;
\treturn ml_io_map(slice, KEBAB_SPMI_CHNL_SLICE);
}
#endif /* KEBAB */
'''
(p.parent/'kebab_spmi_bus.c').write_text(core,newline='\n')
p=r/'after/bsd/conf/files';s=p.read_text()
needle='bsd/dev/kebab_spmi.c'
i=s.index('\n',s.index(needle))+1
s=s[:i]+'bsd/dev/kebab_spmi_bus.c\t\t\tstandard\n'+s[i:]
p.write_text(s,newline='\n')
print('A1a: 9 unchanged functions plus mapping moved to the bus; old entrypoints retained')
