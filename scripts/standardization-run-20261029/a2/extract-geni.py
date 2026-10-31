"""Mechanical hardware/transport extraction; the TCM client keeps its policy."""
from pathlib import Path
import re

r=Path(__file__).parent
p=Path('bsd/dev/kebab_touch.c');original=(r/'before'/p).read_text()
s=original
def function(name):
    match=re.search(r'^(?:(?:static|inline) )*(?:void|bool|int|uint32_t)\n'+name+r'\([^;]*?\)\n\{',original,re.M)
    assert match,name
    end=original.index('\n}\n',match.end())+3
    return original[match.start():end]
names=['kt_trace','kt_rd','kt_wr','kt_se_rd','kt_se_wr','kt_sleep_ms',
       'kt_dump_regs','kt_pins_config','kt_reset_level','kt_reset_pulse','kt_se_init',
       'kt_gcc_probe','kt_geni_probe','kt_pins_probe',
       'kt_bb_level','kt_bb_line','kt_bb_clock_high','kt_bb_start','kt_bb_stop',
       'kt_bb_send','kt_bb_receive','kt_bb_fail','kt_bb_write','kt_bb_read',
       'kt_bb_pins_config','kt_i2c_param','kt_i2c_error','kt_se_recover',
       'kt_i2c_write','kt_i2c_read','kebab_touch_se','kebab_touch_ping','kebab_touch_clock_enable']
functions={name:function(name) for name in names}
public={'kt_trace':'kebab_geni_trace','kt_sleep_ms':'kebab_geni_sleep_ms',
        'kt_i2c_write':'kebab_geni_i2c_write','kt_i2c_read':'kebab_geni_i2c_read'}
constants=original[original.index('#define KT_SE13_PA_DEFAULT'):original.index('/* ------------------------------------------------------------ TCM protocol */')]
regtable=original[original.index('struct kt_reg {'):original.index('static void\nkt_dump_regs')]
gcc=original[original.index('#define KT_GCC_PA '):original.index('static void\nkt_gcc_probe')]
bb=original[original.index('#define KT_BB_HALF_US'):original.index('static bool\nkt_bb_level')]
rcg=original[original.index('#define KT_GCC_RCG_S5'):original.index('void\nkebab_touch_clock_enable')]
state='''static struct {
\tvm_offset_t se, wrap, tlmm, gcc_en, gcc_halt;
\tbool bb, se_dead, se_initialized;
\tuint32_t se_pa, errors; /* retain the legacy combined transport/poll counter */
} kg;
static uint32_t kt_bb_stretch_bails;
static bool kt_se_recover(const char *why);
'''
mapped=original[original.index('\tif (!kt.bb) {',original.index('kt.queue_ok = kt_queue_selftest();')):original.index('\tif (probe != 0) {',original.index('kt.queue_ok = kt_queue_selftest();'))]
prepare=original[original.index('\tif (kt.bb && kt.tlmm == 0)',original.index('kt.queue_ok = kt_queue_selftest();')):original.index('\n\t/*\n\t * Identity first.',original.index('kt.queue_ok = kt_queue_selftest();'))]
default=original[original.index('\tif (kt.se == 0) {',original.index('kebab_touch_probe(void)\n{')):original.index('\tif (kt.se == 0 || kt.tlmm == 0)',original.index('kebab_touch_probe(void)\n{'))]
manual=original[original.index('\tkt_trace("KEBAB TOUCH: TOUCHINIT begin'):original.index('\tkt_identify();',original.index('kebab_touch_init_now(void)\n{'))]
extra='''
uint32_t
kebab_geni_record_poll_error(void)
{
\treturn ++kg.errors;
}

int
kebab_geni_map(uint32_t se_pa, bool bb, uint32_t pins, uint32_t reset, bool raw)
{
\tkg.bb = bb;
\tkg.se_pa = se_pa;
'''+mapped.replace('kt.raw','raw')+'''\treturn 0;
}

bool
kebab_geni_map_default(void)
{
'''+default+'''\treturn kg.se != 0 && kg.tlmm != 0;
}

void
kebab_geni_get_info(struct kebab_geni_info *info)
{
\t*info = (struct kebab_geni_info) {
\t\t.resources = { kg.se_pa != 0 ? kg.se_pa : KT_SE13_PA_DEFAULT,
\t\t    KT_SE13_SIZE, KT_WRAP_PA_DEFAULT, KT_WRAP_SIZE,
\t\t    KT_TLMM_PA + TLMM_TILE_SOUTH, KT_TLMM_SIZE },
\t\t.mapped = kg.bb ? kg.tlmm != 0 : kg.se != 0 && kg.tlmm != 0,
\t\t.bitbang = kg.bb, .initialized = kg.se_initialized, .dead = kg.se_dead,
\t};
}

void
kebab_geni_probe_hardware(void)
{
\tkt_gcc_probe();
\tkt_geni_probe();
\tkt_pins_probe();
}

bool
kebab_geni_manual_init(void)
{
'''+manual.replace('return;','return false;')+'''\treturn true;
}

int
kebab_geni_boot_prepare(uint32_t se_pa, uint32_t hz, uint32_t pins,
    uint32_t reset, bool raw)
{
'''+prepare.replace('kt.raw','raw')+'''\treturn 0;
}
'''
bus='''/* QUPv3 SE13 FIFO I2C transport and board GPIO/clock adapter.
 * Extracted from kebab_touch.c without changing MMIO order, bounds or logs.
 * The persistent C core owns mappings; no TCM decoder or input ABI lives here.
 * Bitbang remains the selected alternate byte transport. */
#if defined(KEBAB)
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <stdarg.h>
#include <sys/systm.h>
#include "kebab_geni.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern bool kebab_usb_console_ready(void);
extern void kebab_usb_log_flush(void);
'''+constants+gcc+bb+rcg+state+regtable
for name in names:
    code=functions[name]
    if name in public:code=re.sub(r'^static ','',code,count=1)
    bus+=code+'\n\n'
bus+=extra+'\n#endif /* KEBAB */\n'
bus=re.sub(r'\bkt\.(se|wrap|tlmm|bb|se_dead|se_initialized|gcc_en|gcc_halt|errors)\b',r'kg.\1',bus)
for old,new in public.items():bus=re.sub(r'\b'+old+r'\b',new,bus)
assert not re.search(r'\bkt\.',bus), set(re.findall(r'\bkt\.\w+',bus))
(r/'after/bsd/dev/kebab_geni.c').write_text(bus,newline='\n')
# Remove transport functions and hardware tables from the client.
for code in functions.values():s=s.replace(code,'')
for code in [constants,regtable,gcc,bb,rcg,'static uint32_t kt_bb_stretch_bails;\n','static bool kt_se_recover(const char *why);\n']:
    assert code in s,code[:50]
    s=s.replace(code,'')
s=s.replace('#include "kebab_touch_tcm.h"','#include "kebab_touch_tcm.h"\n#include "kebab_geni.h"\n#define KT_RAIL_SETTLE_MS 15')
for field in ['se','wrap','tlmm','se_dead','se_initialized','gcc_en','gcc_halt']:
    s=re.sub(r'^\t(?:vm_offset_t|bool)\s+'+field+r';[^\n]*\n','',s,flags=re.M)
s=re.sub(r'^\tuint32_t\s+errors;[^\n]*\n','',s,flags=re.M)
s=s.replace('kt.errors++;','uint32_t errors = kebab_geni_record_poll_error();').replace('kt.errors','errors')
# Existing probe and init entry points retain the TCM/power policy in this TU.
probe_at=s.index('void\nkebab_touch_probe(void)\n{');probe_end=s.index('\n}\n',probe_at)+3
probe=s[probe_at:probe_end]
probe=probe.replace(default,'\tstruct kebab_geni_info info;\n')
probe=probe.replace('if (kt.se == 0 || kt.tlmm == 0)','if (!kebab_geni_map_default())')
probe=probe.replace('\tkt_trace("KEBAB TOUCH: read-only probe begin','\tkebab_geni_get_info(&info);\n\tkt_trace("KEBAB TOUCH: read-only probe begin')
probe=probe.replace('kt.se_initialized, kt.se_dead','info.initialized, info.dead')
probe=probe.replace('\tkt_gcc_probe();\n\tkt_geni_probe();\n\tkt_pins_probe();','\tkebab_geni_probe_hardware();')
s=s[:probe_at]+probe+s[probe_end:]
init_at=s.index('void\nkebab_touch_init_now(void)\n{');init_end=s.index('\n}\n',init_at)+3
init=s[init_at:init_end].replace(default,'')
init=init.replace('if (kt.se == 0 || kt.tlmm == 0)','if (!kebab_geni_map_default())')
init=init.replace(manual,'\tif (!kebab_geni_manual_init()) { return; }\n')
s=s[:init_at]+init+s[init_end:]
assert mapped in s and prepare in s
s=s.replace(mapped,'\tif (kebab_geni_map(se_pa, kt.bb, pins, reset, kt.raw) != 0) { return -1; }\n')
s=s.replace(prepare,'\tif (kebab_geni_boot_prepare(se_pa, hz, pins, reset, kt.raw) != 0) { return -1; }\n')
for old,new in public.items():s=re.sub(r'\b'+old+r'\b',new,s)
assert not re.search(r'\bkt\.(se|wrap|tlmm|se_dead|se_initialized|gcc_en|gcc_halt)\b',s)
assert not re.search(r'\b(?:kt_rd|kt_wr|kt_se_rd|kt_se_wr|ml_io_map)\(',s.replace('extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);',''))
s=s.replace('extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);','')
s=s.replace('This file owns the\n * transport and the order of the bring-up; it owns no wire format.',
            'The byte transport lives in kebab_geni.c; this file owns\n * the TCM transaction and bring-up policy, not register access.')
# Avoid pages of empty extraction scaffolding; leave all live client bodies intact.
s=re.sub(r'\n(?:/\* -[^\n]+ \*/\n)?(?:\n){3,}','\n\n',s)
(r/'after'/p).write_text(s,newline='\n')
p=Path('bsd/conf/files');s=(r/'before'/p).read_text()
line=next(x for x in s.splitlines() if x.startswith('bsd/dev/kebab_touch.c'))
(r/'after'/p).write_text(s+'\n'+line.replace('kebab_touch.c','kebab_geni.c')+'\n',newline='\n')
print('Extracted',len(names),'hardware/transport functions; no TCM body changes')
