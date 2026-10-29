"""Extract already-qualified reset policy from the DWC3 transport, preserving bodies."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'reboot-layer'
names=['bsd/dev/kebab_usb.c','bsd/conf/files']
if not (d/'before').exists():
    got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(names),text=True,capture_output=True,check=True).stdout)
    for n in names:
        p=d/'before'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(got[n]['source'],newline='\n')
usb=(d/'before'/names[0]).read_text();files=(d/'before'/names[1]).read_text();old=usb
a=usb.index('/*\n * The firmware reset interface lives');b=usb.index('/*\n * Read-only SPMI arbiter probe',a)
prototypes=usb[a:b];usb=usb[:a]+usb[b:]
a=usb.index('/*\n * The SM8250 IMEM page');b=usb.index('#define UD_REBOOT_PLAIN',a)
constants=usb[a:b];usb=usb[:a]+usb[b:]
a=usb.index('static uint32_t\nud_fastboot_cookie');b=usb.index('/*\n * Take the stream.',a)
reset=usb[a:b];usb=usb[:a]+usb[b:]
a=usb.index('\t/*\n\t * Map IMEM now');b=usb.index('\t/*\n\t * Map the SPMI arbiter',a)
init=usb[a:b];usb=usb[:a]+'\tkebab_reboot_init();\n'+usb[b:]
fields=['\tvm_offset_t imem;              /* KEBAB_IMEM_PA, mapped once at start */\n',
 '\tvolatile bool     restart_armed;    /* console asked for the restart */\n',
 '\t/* Raw PSCI answers, kept exactly as the firmware returned them. */\n\tuint32_t    psci_reset2;       /* FEATURES(SYSTEM_RESET2) */\n\tbool        psci_reset2_valid; /* the value above was actually read */\n\tbool        psci_probe_sane;   /* the negative control came back 0xffffffff */\n']
for field in fields:assert usb.count(field)==1;usb=usb.replace(field,'')
for field in ['imem','restart_armed','psci_reset2','psci_reset2_valid','psci_probe_sane']:
    reset=reset.replace('ud.'+field,'kr.'+field);init=init.replace('ud.'+field,'kr.'+field)
mapping={'ud_fastboot_cookie':'kr_fastboot_cookie','ud_platform_restart2':'kr_platform_restart2',
 'ud_platform_restart':'kebab_reboot_plain','ud_fastboot_restart':'kebab_reboot_fastboot',
 'ud_psci_report':'kebab_reboot_psci_report','ud_psci_negative_report':'kebab_reboot_psci_negative_report',
 'ud_imem_report':'kebab_reboot_imem_report'}
for before,after in mapping.items():
    reset=reset.replace(before,after);usb=usb.replace(before,after);init=init.replace(before,after)
for name in ['kebab_reboot_plain','kebab_reboot_fastboot','kebab_reboot_psci_report','kebab_reboot_psci_negative_report','kebab_reboot_imem_report']:
    import re
    reset,n=re.subn(r'static (int|void)\n'+name+r'\(',lambda m:m[1]+'\n'+name+'(',reset);assert n==1,name
usb=usb.replace('#include "kebab_vol.h"','#include "kebab_vol.h"\n#include "kebab_reboot.h"')
usb=usb.replace('return ud.restart_armed;','return kebab_reboot_armed();')
for p in ['extern bool kebab_spmi_pon_writes_enabled(void);\n','extern int kebab_spmi_pon_reason_write(void);\n','extern int kebab_spmi_pon_reason_write_normal(void);\n']:
    assert usb.count(p)==1;usb=usb.replace(p,'')
usb=usb.replace('/* PEHaltRestart() and kPERestartCPU: the platform restart path, not a\n * driver-private reset register. */\n#include <IOKit/IOPlatformExpert.h>\n\n','')
header='''#ifndef _KEBAB_REBOOT_H
#define _KEBAB_REBOOT_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resident C reset policy. Init maps IMEM in normal context; later panic
 * calls use only that mapping and the pre-mapped unlocked PON core.
 * Callers release transport gates before reset/flush. Normal callers perform
 * filesystem sync first; panic callers must skip it. No service ownership. */
void kebab_reboot_init(void);
int kebab_reboot_plain(void);
void kebab_reboot_fastboot(bool use_reset2);
bool kebab_reboot_armed(void);
void kebab_reboot_psci_report(void);
void kebab_reboot_psci_negative_report(void);
void kebab_reboot_imem_report(void);
#ifdef __cplusplus
}
#endif
#endif
'''
source='''/* KEBAB board reset policy, separate from DWC3 endpoint/transport state.
 * Existing PON gates, verification, rollback, log flush and PSCI behavior
 * are retained. IMEM and probe state are private to this resident C layer. */
#if defined(KEBAB)
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/debug.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#include <IOKit/IOPlatformExpert.h>
#include "kebab_reboot.h"
#include "kebab_spmi_clients.h"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void kebab_usb_log_flush(void);
'''+prototypes+constants+'''static struct {
 vm_offset_t imem;
 volatile bool restart_armed;
 uint32_t psci_reset2;
 bool psci_reset2_valid, psci_probe_sane;
} kr;

'''+reset+'void\nkebab_reboot_init(void)\n{\n'+init+'}\n\nbool\nkebab_reboot_armed(void)\n{\n\treturn kr.restart_armed;\n}\n#endif /* KEBAB */\n'
assert 'ud.imem' not in usb and 'ud.psci_' not in usb and 'ud.restart_armed' not in usb
assert 'KEBAB_IMEM' not in usb and 'kebab_psci_' not in usb
needle='bsd/dev/kebab_usb.c\t\t\tstandard\n';assert files.count(needle)==1
files=files.replace(needle,needle+'bsd/dev/kebab_reboot.c\t\t\tstandard\n')
changes={names[0]:usb,names[1]:files,'bsd/dev/kebab_reboot.c':source,'bsd/dev/kebab_reboot.h':header}
patch=[]
for n,after in changes.items():
    before=(d/'before'/n).read_text() if (d/'before'/n).exists() else ''
    p=d/'after'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
    patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+n if before else '/dev/null',tofile='b/'+n))
(d/'0196-kebab-reboot-policy-layer.patch').write_text(''.join(patch),newline='\n')
print('Extracted resident reset state/policy; DWC3 transport/dispatch bodies retained')
