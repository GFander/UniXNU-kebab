"""A1b: keep entry points, hide arbiter state, separate PMIC and RPMh transports."""
from pathlib import Path
import re

r=Path(__file__).parent
root=r/'after/bsd/dev'
s=(r/'before/bsd/dev/kebab_spmi.c').read_text()
bus=(r/'before/bsd/dev/kebab_spmi_bus.c').read_text()
h=(r/'before/bsd/dev/kebab_spmi_bus.h').read_text()
def fn(text,name,comments=True):
    at=text.index('\n'+name+'(')+1
    start=text.rfind('\n',0,at-1)+1
    opening=text.index('{',at);end=text.index('\n}',opening)+2
    if comments:
        while text[:start].rstrip().endswith('*/'):
            cut=text[:start].rstrip();comment=cut.rfind('/*')
            if comment<0:break
            start=comment
    return text[start:end]
def join(names):return '\n\n'.join(fn(s,name) for name in names)
normal_names=['kebab_spmi_keys_sample','kebab_spmi_keys_config','kebab_spmi_pon_report',
              'kebab_spmi_power_report','kebab_spmi_vrm_boot_power','kebab_spmi_pon_reason_write_normal',
              'kebab_spmi_test_panic_lock']
def normal(text):
    text=text.replace('lck_mtx_lock(&kebab_spmi_normal_lock);','kebab_spmi_bus_lock();')
    return text.replace('lck_mtx_unlock(&kebab_spmi_normal_lock);','kebab_spmi_bus_unlock();')
def accessors(text):
    text=text.replace('++spmi.read_attempt','kebab_spmi_bus_next_attempt()')
    text=text.replace('spmi.mapped','kebab_spmi_bus_ready()')
    text=text.replace('spmi.version','kebab_spmi_bus_version()')
    text=text.replace('spmi.verbose','kebab_spmi_bus_verbose()')
    text=text.replace('kebab_spmi_rd(spmi.core, KEBAB_SPMI_VERSION)','kebab_spmi_bus_read_version()')
    return text
prefix='''#if defined(KEBAB)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <mach/boolean.h>
#include <pexpert/pexpert.h>
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
#include "kebab_pmic_regs.h"
'''
suffix='\n#endif /* KEBAB */\n'

# Keep board provenance/register notes in the bus-private header. PMIC clients
# receive only their register constants, no arbiter VAs or MMIO helpers.
private=h[h.index('/* ------------------------------------------------------------ board facts */'):h.index('uint32_t\nkebab_spmi_rd')]
private=private.replace('extern struct kebab_spmi_bus_state kebab_spmi_bus;\n','')
private=private.replace('extern uint32_t kebab_spmi_nonzero;','')
private=private.replace(h[h.index('struct kebab_spmi_match {'):h.index('/* A PPID-to-observer-APID')],'')
private=private.replace('\tbool        write_gate;\n','').replace('\tbool        write_gate_parsed;\n','')
(root/'kebab_spmi_bus_private.h').write_text('''/* Arbiter implementation only; not a PMIC client interface. */
#ifndef _KEBAB_SPMI_BUS_PRIVATE_H
#define _KEBAB_SPMI_BUS_PRIVATE_H
'''+private+'\n#endif\n',newline='\n')
pmic='\n'.join(re.findall(r'^#define KEBAB_SPMI_(?:PON|KEYS)_\w+[^\n]*',h,re.M))
(root/'kebab_pmic_regs.h').write_text('''/* PM8150 board register facts retained from kebab_spmi.c: vendor DT,
 * qpnp-power-on.c and pinctrl-spmi-gpio.c. These are PMIC addresses, not VAs. */
#ifndef _KEBAB_PMIC_REGS_H
#define _KEBAB_PMIC_REGS_H
'''+pmic+'\n#endif\n',newline='\n')

pon=join(['kebab_spmi_pon_writes_enabled','kebab_spmi_pon_apid','kebab_spmi_obs_read','kebab_spmi_pon_read',
    'kebab_spmi_pon_reason_write','kebab_spmi_pon_report_unserialized',
    'kebab_spmi_pon_report','kebab_spmi_pon_reason_write_normal','kebab_spmi_test_panic_lock'])
pon=pon.replace('spmi.write_gate_parsed','pon.write_gate_parsed').replace('spmi.write_gate','pon.write_gate')
pon=pon.replace('spmi.verbose','pon.verbose')
needle='\t\tpon.verbose = PE_parse_boot_argn("kebab_pon_verbose", &verbose,\n\t\t    sizeof(verbose)) && verbose != 0;'
assert needle in pon
pon=pon.replace(needle,needle+'\n\t\tkebab_spmi_bus_set_verbose(pon.verbose);')
pon=normal(accessors(pon))
pon+='''

/* Policy remains in PON; the bus maps only the selected owned channel. */
void
kebab_spmi_pon_prepare(void)
{
\tstruct kebab_spmi_bus_info info;
\tkebab_spmi_bus_get_info(&info);
\tif (!kebab_spmi_pon_writes_enabled()) {
\t\tprintf("KEBAB SPMI: chnls 0x%08x not mapped; the write path is off\\n",
\t\t    (uint32_t)info.resources[6]);
\t\treturn;
\t}
\tif (kebab_spmi_version_known(info.version) && info.version >= 0x50000000u) {
\t\tuint32_t apid;
\t\tif (kebab_spmi_pon_apid(&apid) == 0) {
\t\t\tkebab_spmi_bus_prepare_write(apid);
\t\t}
\t} else {
\t\tprintf("KEBAB SPMI: arbiter is not v5; the write path is not implemented for it\\n");
\t}
}'''
# The original verbose line prints the physical channel; obtain the constant
# from the narrow resource snapshot rather than exposing the bus's VA state.
pon=pon.replace('KEBAB_SPMI_CHNLS_PA + 0x10000u * matches[i].apid',
    '(uint32_t)kebab_spmi_bus_channel_physical(matches[i].apid)')
(root/'kebab_spmi_pon.c').write_text('/* PM8150 PON policy and restart reason; panic core stays lock-free. */\n'+
    prefix+'\nstatic struct { bool write_gate, write_gate_parsed, verbose; } pon;\n\n'+pon+suffix,newline='\n')

keys=normal(accessors(join(['kebab_spmi_keys_sample_unserialized','kebab_spmi_keys_config_unserialized',
    'kebab_spmi_keys_sample','kebab_spmi_keys_config'])))
(root/'kebab_spmi_keys.c').write_text('/* PM8150 key register client; no arbiter mappings or MMIO. */\n'+prefix+'\n'+keys+suffix,newline='\n')

powerdefs=s[s.index('/*\n * The S3908 node names two rails'):s.index('static int\nkebab_spmi_power_read')]
power=normal(accessors(join(['kebab_spmi_power_read','kebab_spmi_power_one','kebab_spmi_power_map_summary',
    'kebab_spmi_rail_target','kebab_spmi_rail_enable_read','kebab_spmi_power_report_unserialized','kebab_spmi_power_report'])))
power=power.replace('static int\nkebab_spmi_rail_enable_read(', 'int\nkebab_spmi_rail_enable_read_locked(')
power=power.replace('kebab_spmi_rd(spmi.core,\n\t\t    KEBAB_SPMI_APID_MAP + 4u * apid)',
    'kebab_spmi_bus_mapping(apid)')
assert 'spmi.' not in power
(root/'kebab_spmi_power.c').write_text('/* Read-only PMIC rail identity/state client of the SPMI arbiter. */\n'+prefix+'\n'+powerdefs+power+suffix,newline='\n')

rpmhdefs=s[s.index('/* ------------------------------------------- RPMh discovery'):s.index('static void\nkebab_rpmh_cmd_db')]
rpmhdefs+='\n'+'\n'.join(re.findall(r'^#define KEBAB_VRM_\w+[^\n]*',s,re.M))+'\n'
vrm=re.search(r'static struct \{[\s\S]*?\} vrm;',s)[0]
rpmh=join(['kebab_rpmh_cmd_db','kebab_rpmh_rsc','kebab_vrm_status_name','kebab_vrm_phase_name',
    'kebab_vrm_rd','kebab_vrm_wr','kebab_vrm_now_us','kebab_spmi_vrm_writes_enabled',
    'kebab_vrm_map','kebab_spmi_vrm_init','kebab_tcs_report','kebab_spmi_vrm_enable',
    'kebab_spmi_vrm_boot_power_unserialized','kebab_spmi_vrm_boot_power','kebab_rpmh_report'])
rpmh=normal(rpmh).replace('kebab_spmi_rail_enable_read(', 'kebab_spmi_rail_enable_read_locked(')
raw='\n\n'.join(fn(bus,n) for n in ['kebab_spmi_rd','kebab_spmi_wr'])
raw=re.sub(r'(?m)^(uint32_t|void)\n',r'static \1\n',raw)
rpmh=(raw+'\n\n'+rpmh).replace('kebab_spmi_rd','kebab_rpmh_read32').replace('kebab_spmi_wr','kebab_rpmh_write32')
(root/'kebab_rpmh.c').write_text('''/* RPMh command DB/RSC controller adapter and board rail policy.
 * TCS is an RSC transport, not an SPMI transaction. The tested encoder/state
 * machine stays in kebab_rpmh_vrm.c; PMIC readback goes through its SPMI client.
 */
'''+prefix.replace('#if defined(KEBAB)\n','#if defined(KEBAB)\n#define KEBAB_RPMH_KERNEL 1\n')+'''#include "kebab_rpmh_vrm.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void kebab_usb_log_flush(void);
'''+rpmhdefs+vrm+'\n\n'+rpmh+suffix,newline='\n')

# Narrow bus operations. Only the bus translation unit can see its state/VAs.
bus=bus.replace('#include "kebab_spmi_bus.h"','#include "kebab_spmi_bus.h"\n#include "kebab_spmi_clients.h"\n#include "kebab_spmi_bus_private.h"\n#include <kern/locks.h>')
bus=bus.replace('struct kebab_spmi_bus_state kebab_spmi_bus;','static struct kebab_spmi_bus_state kebab_spmi_bus;')
bus=bus.replace('uint32_t kebab_spmi_nonzero;','static uint32_t kebab_spmi_nonzero;')
bus=bus.replace('uint32_t\nkebab_spmi_rd(', 'static uint32_t\nkebab_spmi_rd(')
bus=bus.replace('void\nkebab_spmi_wr(', 'static void\nkebab_spmi_wr(')
bus=bus.replace('void\nkebab_spmi_bus_map_arbiter(', 'static void\nkebab_spmi_bus_map_arbiter(')
bus=bus.replace('vm_offset_t\nkebab_spmi_bus_map_channel(', 'static vm_offset_t\nkebab_spmi_bus_map_channel(')
lockdecl='''static LCK_GRP_DECLARE(kebab_spmi_normal_group, "kebab_spmi_normal");
static LCK_MTX_DECLARE(kebab_spmi_normal_lock, &kebab_spmi_normal_group);
'''
bus=bus.replace('static struct kebab_spmi_bus_state kebab_spmi_bus;',lockdecl+'\nstatic struct kebab_spmi_bus_state kebab_spmi_bus;')
bus=bus.replace('#endif /* KEBAB */', '\n\n'+join(['kebab_spmi_match_line','kebab_spmi_report_unserialized','kebab_spmi_report']).replace('static void\nkebab_spmi_match_line','void\nkebab_spmi_match_line')+'\n\n'+'''
void kebab_spmi_bus_lock(void) { lck_mtx_lock(&kebab_spmi_normal_lock); }
void kebab_spmi_bus_unlock(void) { lck_mtx_unlock(&kebab_spmi_normal_lock); }
bool kebab_spmi_bus_ready(void) { return spmi.mapped; }
uint32_t kebab_spmi_bus_version(void) { return spmi.version; }
uint32_t kebab_spmi_bus_read_version(void) { return kebab_spmi_rd(spmi.core, KEBAB_SPMI_VERSION); }
uint32_t kebab_spmi_bus_next_attempt(void) { return ++spmi.read_attempt; }
bool kebab_spmi_bus_verbose(void) { return spmi.verbose; }
void kebab_spmi_bus_set_verbose(bool verbose) { spmi.verbose = verbose; }
uint32_t kebab_spmi_bus_mapping(uint32_t apid)
{
\treturn apid < KEBAB_SPMI_APID_MAX ?
\t       kebab_spmi_rd(spmi.core, KEBAB_SPMI_APID_MAP + 4u * apid) : 0;
}
uint64_t kebab_spmi_bus_channel_physical(uint32_t apid)
{
\treturn KEBAB_SPMI_CHNLS_PA + 0x10000u * apid;
}
void kebab_spmi_bus_get_info(struct kebab_spmi_bus_info *info)
{
\tif (info == NULL) { return; }
\t*info = (struct kebab_spmi_bus_info) {
\t\t.resources = { KEBAB_SPMI_CORE_PA, KEBAB_SPMI_CORE_SIZE,
\t\t    KEBAB_SPMI_OBSRV_PA, KEBAB_SPMI_OBSRV_SIZE,
\t\t    KEBAB_SPMI_CNFG_PA, KEBAB_SPMI_CNFG_SIZE,
\t\t    KEBAB_SPMI_CHNLS_PA, KEBAB_SPMI_CHNLS_SIZE },
\t\t.version = spmi.version, .ee = KEBAB_SPMI_EE,
\t\t.mapped = spmi.mapped, .write_channel_mapped = spmi.chnl_mapped,
\t\t.write_apid = spmi.chnl_apid,
\t};
}
'''+suffix)
init=fn(s,'kebab_spmi_init',False)
part=init[init.index('\tkebab_spmi_bus_map_arbiter();'):init.index('\t/*\n\t * The RPMh VRM path')]
part=part.replace('return;','return false;')
businit='''bool
kebab_spmi_bus_init(void)
{
\tif (spmi.attempted) { return spmi.mapped; }
\tspmi.attempted = true;
'''+part+'\treturn true;\n}\n'
part=init[init.index('\t\t\tvm_offset_t slice ='):init.index('\n\t\t}\n\t} else {')]
part=part.replace('\t\t\t','\t')
prepare='void\nkebab_spmi_bus_prepare_write(uint32_t apid)\n{\n'+part+'\n}\n'
bus=bus.replace('#endif /* KEBAB */',businit+'\n'+prepare+'\n#endif /* KEBAB */')
(root/'kebab_spmi_bus.c').write_text(bus,newline='\n')

protos=[]
allcode='\n'.join(p.read_text() for p in root.glob('*.c') if p.name!='kebab_spmi.c')
for text in (bus,pon,keys,power,rpmh):
    for m in re.finditer(r'(?m)^(?!static)(?:uint32_t|uint64_t|int|void|bool|const char \*)\s*\n?(kebab_\w+)\([^;{}]*\)\s*\{',text):
        signature=text[m.start():m.end()-1].strip()
        protos.append(signature+';')
busnames=set(re.findall(r'\n(kebab_\w+)\(',bus))
busprotos=[p for p in protos if re.search(r'\b(kebab_spmi_bus_\w+|kebab_spmi_scan|kebab_spmi_reader|kebab_spmi_reader_forget|kebab_spmi_obs_read_ex|kebab_spmi_rw_write|kebab_spmi_family|kebab_spmi_version_known|kebab_spmi_match_line)\(',p)]
clientprotos=[p for p in protos if p not in busprotos]
match=h[h.index('struct kebab_spmi_match {'):h.index('/* A PPID-to-observer-APID')]
public='''#ifndef _KEBAB_SPMI_BUS_H
#define _KEBAB_SPMI_BUS_H
#include <sys/types.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resource order: core, observer, configuration, channel window (u64 pairs).
 * No mapped addresses are exposed. Normal clients must serialize complete
 * transactions; early/panic PON deliberately uses the bounded unlocked core. */
struct kebab_spmi_bus_info {
\tuint64_t resources[8];
\tuint32_t version, ee, write_apid;
\tbool mapped, write_channel_mapped;
};
'''+match+'#define KEBAB_SPMI_APID_MAX 512u\n#define KEBAB_SPMI_EE 0u /* board qcom,ee; never a mapped address */\n'+ '\n'.join(dict.fromkeys(busprotos))+'''
#ifdef __cplusplus
}
#endif
#endif
'''
(root/'kebab_spmi_bus.h').write_text(public,newline='\n')
(root/'kebab_spmi_clients.h').write_text('''#ifndef _KEBAB_SPMI_CLIENTS_H
#define _KEBAB_SPMI_CLIENTS_H
#include <sys/types.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Keep legacy entry points. _locked rail read is internal to the serialized
 * board power sequence. Panic may call only the original unlocked PON core. */
void kebab_spmi_init(void);
'''+ '\n'.join(dict.fromkeys(clientprotos))+'''
#ifdef __cplusplus
}
#endif
#endif
''',newline='\n')
(root/'kebab_spmi.c').write_text('''/* Boot orchestration only. The arbiter owns mappings and transactions;
 * PON/PMIC keys/rail-read clients own device policy. RPMh has its own RSC
 * transport and the original tested TCS core. Legacy boot arguments remain.
 */
#if defined(KEBAB)
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
void
kebab_spmi_init(void)
{
\tstatic bool attempted;
\tif (attempted) { return; }
\tattempted = true;
\tif (!kebab_spmi_bus_init()) { return; }
\tkebab_spmi_vrm_init();
\tkebab_spmi_pon_prepare();
}
#endif /* KEBAB */
''',newline='\n')
p=r/'after/bsd/conf/files';text=(r/'before/bsd/conf/files').read_text();needle='bsd/dev/kebab_spmi_bus.c\t\t\tstandard\n';assert needle in text
text=text.replace(needle,needle+''.join('bsd/dev/'+f+'\t\t\tstandard\n' for f in [
    'kebab_spmi_pon.c','kebab_spmi_keys.c','kebab_spmi_power.c','kebab_rpmh.c']))
p.write_text(text,newline='\n')
for name in ['kebab_spmi_pon.c','kebab_spmi_keys.c','kebab_spmi_power.c']:
    text=(root/name).read_text();assert 'spmi.' not in text,name
    assert not re.search(r'\b(ml_io_map|kebab_spmi_rd|kebab_spmi_wr)\(',text),name
for p in root.iterdir():
    if p.is_file():p.write_text(p.read_text().rstrip()+'\n',newline='\n')
print('A1b prepared: private arbiter state, PMIC clients and separate RPMh adapter; original entry points retained')
