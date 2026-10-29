from pathlib import Path
import re

r=Path(__file__).parent
def source(path):return (r/'before'/path).read_text()
def save(path,s):(r/'after'/path).write_text(s,newline='\n')
p='bsd/dev/kebab_geni.h';s=source(p)
s=s.replace('#ifdef __cplusplus','''/* Normal transactions serialize a whole TCM command or header+payload frame,
 * rather than just each FIFO operation. Early/panic recovery does not use GENI. */
void kebab_geni_transaction_lock(void);
void kebab_geni_transaction_unlock(void);
struct kebab_geni_status {
\tstruct kebab_geni_info info;
\tuint32_t protocol, sequencer, master_irq;
};
struct kebab_touch_status {
\tuint32_t max_x, max_y, max_objects;
\tbool identified, decoding, queue_ok, device_node, unavailable;
};
int kebab_geni_read_status(struct kebab_geni_status *status);
void kebab_touch_get_status(struct kebab_touch_status *status);
#ifdef __cplusplus''',1)
s=s.replace('/* Normal transactions', '#ifdef __cplusplus\nextern "C" {\n#endif\n/* Normal transactions',1)
s=s.replace('#ifdef __cplusplus\nextern "C" {\n#endif\nvoid kebab_geni_trace','void kebab_geni_trace',1)
save(p,s)
p='bsd/dev/kebab_geni.c';s=source(p).replace('#include <kern/clock.h>','#include <kern/clock.h>\n#include <kern/locks.h>')
at=s.index('static struct {')
s=s[:at]+'''static LCK_GRP_DECLARE(kebab_geni_lck_grp, "kebab-geni");
static LCK_MTX_DECLARE(kebab_geni_lck, &kebab_geni_lck_grp);
void kebab_geni_transaction_lock(void) { lck_mtx_lock(&kebab_geni_lck); }
void kebab_geni_transaction_unlock(void) { lck_mtx_unlock(&kebab_geni_lck); }
'''+s[at:]
# Board console commands share the same normal transaction lock as TCM.
names=['kebab_touch_se','kebab_touch_ping','kebab_touch_clock_enable']
for name in names:
    s=re.sub(r'\b'+name+r'\b',name+'_locked',s)
    s=s.replace('void\n'+name+'_locked(', 'static void\n'+name+'_locked(')
wrappers=''
for name in names:
    wrappers+='void\n'+name+'(void)\n{\n\tkebab_geni_transaction_lock();\n\t'+name+'_locked();\n\tkebab_geni_transaction_unlock();\n}\n\n'
s=s.replace('#endif /* KEBAB */',wrappers+'''int
kebab_geni_read_status(struct kebab_geni_status *status)
{
\tif (status == NULL) { return -1; }
\tkebab_geni_transaction_lock();
\tbzero(status, sizeof(*status));
\tkebab_geni_get_info(&status->info);
\tif (!status->info.mapped) { kebab_geni_transaction_unlock(); return -1; }
\tif (!kg.bb) {
\t\tstatus->protocol = (kt_se_rd(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
\t\tstatus->sequencer = kt_se_rd(SE_GENI_STATUS);
\t\tstatus->master_irq = kt_se_rd(SE_GENI_M_IRQ_STATUS);
\t}
\tkebab_geni_transaction_unlock();
\treturn 0;
}
#endif /* KEBAB */''')
save(p,s)
p='bsd/dev/kebab_touch.c';s=source(p)
for name,returns in [('kebab_touch_probe','void'),('kebab_touch_init_now','void'),('kebab_touch_start','int')]:
    # Keep existing public prototypes, rename only the actual definition.
    needle=returns+'\n'+name+'(void)\n{';assert s.count(needle)==1
    s=s.replace(needle,'static '+returns+'\n'+name+'_locked(void)\n{')
wrappers=''
for name,returns in [('kebab_touch_probe','void'),('kebab_touch_init_now','void'),('kebab_touch_start','int')]:
    wrappers+=returns+'\n'+name+'(void)\n{\n\tkebab_geni_transaction_lock();\n'
    wrappers+=('\tint result = '+name+'_locked();\n' if returns=='int' else '\t'+name+'_locked();\n')
    wrappers+='\tkebab_geni_transaction_unlock();\n'+ ('\treturn result;\n' if returns=='int' else '')+'}\n\n'
# The startup core may call the probe and clock cores without recursive locking.
s=s.replace('\t\tkebab_touch_probe();','\t\tkebab_touch_probe_locked();')
s=s.replace('int n = kt_poll_once();','''kebab_geni_transaction_lock();
\t\tint n = kt_poll_once();
\t\tuint32_t errors = n < 0 ? kebab_geni_record_poll_error() : 0;
\t\tkebab_geni_transaction_unlock();''')
s=s.replace('\t\t\tuint32_t errors = kebab_geni_record_poll_error();\n','')
s=s.replace('#endif /* KEBAB */',wrappers+'''void
kebab_touch_get_status(struct kebab_touch_status *status)
{
\tkebab_geni_transaction_lock();
\t*status = (struct kebab_touch_status) {
\t\t.max_x = kt.max_x, .max_y = kt.max_y, .max_objects = kt.max_objects,
\t\t.identified = kt.tcm_seen, .decoding = kt.decode, .queue_ok = kt.queue_ok,
\t\t.device_node = kt.dev_ready, .unavailable = kt.unavailable,
\t};
\tkebab_geni_transaction_unlock();
}
#endif /* KEBAB */''')
save(p,s)
p='iokit/conf/files.arm64';save(p,source(p)+'\niokit/Kernel/arm/KebabGeni.cpp\toptional iokitcpp\n')
p='iokit/KernelConfigTables.cpp';s=source(p);at=s.index('#endif\n#if defined(ARM64_BOARD_CONFIG_NEGRONI)')
entry='''    "   {"
    "     'IOClass' = KebabGeniBus;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch' = 'kebab,sm8250-geni13';"
    "     'IOResourceMatch' = IOBSD;"
    "   },"
    "   {"
    "     'IOClass' = KebabTouch;"
    "     'IOProviderClass' = KebabI2CDevice;"
    "     'IONameMatch' = 'synaptics,s3908';"
    "   },"
'''
save(p,s[:at]+entry+s[at:])
p='iokit/Kernel/arm/KebabDriverManifest.h';s=source(p)
s=s.replace(' X("touch", NULL, "resident GENI/TCM C core; TOUCH?; no HID")',
''' X("geni", "KebabGeniBus", "SE13/bitbang transport; boot C mappings") \\
 X("touch", "KebabTouch", "typed TCM state; C polling lifetime; no HID")''')
save(p,s)
p='iokit/Kernel/arm/KebabDriverSelfTest.cpp';s=source(p)
s=s.replace('extern "C" bool kebab_spmi_services_selftest(void);','extern "C" bool kebab_spmi_services_selftest(void);\nextern "C" bool kebab_geni_services_selftest(void);')
s=s.replace('bool spmi = kebab_spmi_services_selftest();','bool spmi = kebab_spmi_services_selftest();\n\tbool geni = kebab_geni_services_selftest();')
s=s.replace('surface && display && storage && spmi ?', 'surface && display && storage && spmi && geni ?')
save(p,s)
