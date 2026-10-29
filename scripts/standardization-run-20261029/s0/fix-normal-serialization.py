from pathlib import Path
import re

r=Path(__file__).parent
p=r/'after/bsd/dev/kebab_spmi.c';s=(r/'before/bsd/dev/kebab_spmi.c').read_text()
s=s.replace('#include <kern/clock.h>','#include <kern/clock.h>\n#include <kern/locks.h>')
needle='/* ------------------------------------------------------------ board facts */'
s=s.replace(needle,'''/* Normal callers share arbiter command/data registers. Panic recovery uses
 * kebab_spmi_pon_reason_write() directly and must never acquire this mutex. */
static LCK_GRP_DECLARE(kebab_spmi_normal_group, "kebab_spmi_normal");
static LCK_MTX_DECLARE(kebab_spmi_normal_lock, &kebab_spmi_normal_group);
int kebab_spmi_pon_reason_write_normal(void);
void kebab_spmi_test_panic_lock(void);

'''+needle)
items=[('int','kebab_spmi_keys_sample','uint8_t raw[4]','raw'),
       ('int','kebab_spmi_keys_config','uint8_t gpio6[3], uint8_t gpio7[3]','gpio6, gpio7'),
       ('void','kebab_spmi_report','void',''),
       ('void','kebab_spmi_pon_report','void',''),
       ('void','kebab_spmi_power_report','void',''),
       ('int','kebab_spmi_vrm_boot_power','void','')]
wrappers=[]
for result,name,args,callargs in items:
    needle=result+'\n'+name+'('
    assert s.count(needle)==1,name
    s=s.replace(needle,'static '+result+'\n'+name+'_unserialized(',1)
    wrappers.append(result+'\n'+name+'('+args+')\n{\n'
        '\tlck_mtx_lock(&kebab_spmi_normal_lock);\n'+
        ('\tint result = ' if result=='int' else '\t')+name+'_unserialized('+callargs+');\n'
        '\tlck_mtx_unlock(&kebab_spmi_normal_lock);\n'+
        ('\treturn result;\n' if result=='int' else '')+'}')
wrappers.append('''int
kebab_spmi_pon_reason_write_normal(void)
{
\tlck_mtx_lock(&kebab_spmi_normal_lock);
\tint result = kebab_spmi_pon_reason_write();
\tlck_mtx_unlock(&kebab_spmi_normal_lock);
\treturn result;
}

/* Default-off failure injection: PANIC! invokes this on the normal console
 * thread. Holding the ordinary bus mutex across the panic proves that the
 * independent panic FASTBOOT path cannot wait on the interrupted owner. */
void
kebab_spmi_test_panic_lock(void)
{
\tuint32_t enabled = 0;
\tif (PE_parse_boot_argn("kebab_spmi_lockpanic", &enabled, sizeof(enabled)) && enabled) {
\t\tlck_mtx_lock(&kebab_spmi_normal_lock);
\t\tprintf("KEBAB SPMI TEST: normal bus mutex held for intentional panic\\n");
\t\tpanic("kebab usb console test (normal SPMI lock held)");
\t}
}''')
s=s.replace('#endif /* KEBAB */','\n\n'.join(wrappers)+'\n\n#endif /* KEBAB */')
p.write_text(s,newline='\n')
p=r/'after/bsd/dev/kebab_usb.c';s=(r/'before/bsd/dev/kebab_usb.c').read_text()
s=s.replace('extern int kebab_spmi_pon_reason_write(void);',
    'extern int kebab_spmi_pon_reason_write(void);\nextern int kebab_spmi_pon_reason_write_normal(void);\nextern void kebab_spmi_test_panic_lock(void);')
needle='if (kebab_spmi_pon_reason_write() != 0)'
assert s.count(needle)==1
s=s.replace(needle,'if ((panic_active() ? kebab_spmi_pon_reason_write() :\n\t    kebab_spmi_pon_reason_write_normal()) != 0)')
needle='\t\tif (trigger) panic("kebab usb console test");'
assert s.count(needle)==1
s=s.replace(needle,'\t\tif (trigger) {\n\t\t\tkebab_spmi_test_panic_lock();\n\t\t\tpanic("kebab usb console test");\n\t\t}')
p.write_text(s,newline='\n')
print('normal bus serialization added; original unlocked PON core retained')
