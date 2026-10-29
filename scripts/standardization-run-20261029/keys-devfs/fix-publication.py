from pathlib import Path
r=Path(__file__).parent
p=Path('bsd/dev/kebab_keys.c');s=(r/'before'/p).read_text()
s=s.replace('int kebab_keys_start(void);','int kebab_keys_start(void);\nvoid kebab_keys_publish(void);\nbool kebab_keys_node_selftest(void);')
s=s.replace('\tbool        dev_ready;','\tbool        dev_ready;\n\tbool        polling;           /* startup succeeded; publish after devfs init */')
s=s.replace('\tkk_dev_register();','\tkk.polling = true;\n\tprintf("KEBAB KEYS: node publication deferred until devfs is ready\\n");')
extra='''/* Polling is boot-owned and permanent. Publishing the character device is a
 * separate, idempotent normal phase after devfs_kernel_mount(). No sample,
 * debounce or record layout is changed by this timing correction. */
static LCK_GRP_DECLARE(kk_publish_group, "kebabkeys-publish");
static LCK_MTX_DECLARE(kk_publish_lock, &kk_publish_group);
void
kebab_keys_publish(void)
{
\tlck_mtx_lock(&kk_publish_lock);
\tif (kk.polling && !kk.dev_ready) { kk_dev_register(); }
\tlck_mtx_unlock(&kk_publish_lock);
}

bool
kebab_keys_node_selftest(void)
{
\tif (!kk.polling) {
\t\tprintf("KEBAB DRIVER TEST: keys devfs SKIP (polling not enabled)\\n");
\t\treturn true;
\t}
\tvnode_t node = NULL;
\tint error = vnode_lookup("/dev/kebab_keys", 0, &node, vfs_context_kernel());
\tbool passed = kk.dev_ready && error == 0 && node != NULL &&
\t    vnode_vtype(node) == VCHR && vnode_specrdev(node) == makedev(kk.cmajor, 0);
\tif (node != NULL) { vnode_put(node); }
\tprintf("KEBAB DRIVER TEST: keys devfs %s (lookup=%d major=%d record=%u)\\n",
\t    passed ? "PASS" : "FAIL", error, kk.cmajor, (unsigned)sizeof(struct kk_record));
\treturn passed;
}

'''
s=s.replace('#endif /* KEBAB */',extra+'#endif /* KEBAB */')
(r/'after'/p).write_text(s,newline='\n')
p=Path('bsd/kern/bsd_init.c');s=(r/'before'/p).read_text()
anchor='#if defined(KEBAB)\n\t/*\n\t * On a real device the static trust cache'
assert s.count(anchor)==1
s=s.replace(anchor,'''#if defined(KEBAB)
\t/* devfs is now mounted; early PMIC polling did not publish its node. */
\t{
\t\textern void kebab_keys_publish(void);
\t\tkebab_keys_publish();
\t}
\t/*
\t * On a real device the static trust cache''')
(r/'after'/p).write_text(s,newline='\n')
p=Path('iokit/Kernel/arm/KebabDriverSelfTest.cpp');s=(r/'before'/p).read_text()
s=s.replace('extern "C" bool kebab_geni_services_selftest(void);','extern "C" bool kebab_geni_services_selftest(void);\nextern "C" bool kebab_keys_node_selftest(void);')
s=s.replace('bool geni = kebab_geni_services_selftest();','bool geni = kebab_geni_services_selftest();\n\tbool keys = kebab_keys_node_selftest();')
s=s.replace('surface && display && storage && spmi && geni ?', 'surface && display && storage && spmi && geni && keys ?')
(r/'after'/p).write_text(s,newline='\n')
