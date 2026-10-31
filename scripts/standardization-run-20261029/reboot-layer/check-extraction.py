"""Check actual reset bodies and init block after reversing only renamed symbols/state."""
from pathlib import Path
import json,re
r=Path(__file__).parent
old=(r/'before/bsd/dev/kebab_usb.c').read_text();new=(r/'after/bsd/dev/kebab_reboot.c').read_text()
mapping={'ud_fastboot_cookie':'kr_fastboot_cookie','ud_platform_restart2':'kr_platform_restart2',
 'ud_platform_restart':'kebab_reboot_plain','ud_fastboot_restart':'kebab_reboot_fastboot',
 'ud_psci_report':'kebab_reboot_psci_report','ud_psci_negative_report':'kebab_reboot_psci_negative_report',
 'ud_imem_report':'kebab_reboot_imem_report'}
def body(source,name):
    start=source.index('\n'+name+'(');start=source.index('\n{\n',start)+3
    return source[start:source.index('\n}\n',start)]
def reverse(s):
    for a,b in mapping.items():s=s.replace(b,a)
    return s.replace('kr.','ud.')
for a,b in mapping.items():assert body(old,a)==reverse(body(new,b)),a
a=old.index('\t/*\n\t * Map IMEM now');b=old.index('\t/*\n\t * Map the SPMI arbiter',a)
assert old[a:b].removesuffix('\n')==reverse(body(new,'kebab_reboot_init'))
usb=(r/'after/bsd/dev/kebab_usb.c').read_text()
assert usb.count('kebab_reboot_init();')==1
assert not any(x in usb for x in ['KEBAB_IMEM','kebab_psci_','ud.restart_armed','ud.psci_'])
result={'reset_function_bodies_equal_after_identifier_state_rename':list(mapping),
 'init_block_equal':True,'USB_has_no_IMEM_PSCI_state_or_constants':True,'single_normal_init_call':True}
(r/'extraction-check.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: seven reset function bodies and init block preserved; reset state private; one normal init call')
