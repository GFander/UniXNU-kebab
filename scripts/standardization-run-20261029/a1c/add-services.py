from pathlib import Path

r=Path(__file__).parent;d=r/'after'
p=d/'bsd/dev/kebab_spmi_clients.h';s=(r/'before'/p.relative_to(d)).read_text()
at=s.index('#ifdef __cplusplus\n}')
s=s[:at]+'''/* Native-only status contracts; never copied as a userspace wire structure. */
struct kebab_rpmh_info {
\tuint64_t resources[4]; /* RSC, command DB, each address/length pair */
\tuint32_t irq_status, command_status, phase;
\tbool mapped, writes_enabled;
};
int kebab_spmi_pon_state(uint8_t state[2]);
void kebab_rpmh_get_info(struct kebab_rpmh_info *info);
int kebab_rpmh_read_status(struct kebab_rpmh_info *info);
'''+s[at:]
p.write_text(s,newline='\n')
p=d/'bsd/dev/kebab_spmi_pon.c';s=(r/'before'/p.relative_to(d)).read_text()
s=s.replace('#endif /* KEBAB */','''/* Normal-service read-only state query. The reset core remains independent. */
int
kebab_spmi_pon_state(uint8_t state[2])
{
\tuint32_t apid;
\tint result = -1;
\tif (state == NULL) { return -1; }
\tkebab_spmi_bus_lock();
\tif (kebab_spmi_bus_ready() &&
\t    kebab_spmi_version_known(kebab_spmi_bus_version()) &&
\t    kebab_spmi_bus_version() >= 0x50000000u &&
\t    kebab_spmi_reader(KEBAB_SPMI_PON_PPID, &apid) == 0 &&
\t    kebab_spmi_pon_read(apid, KEBAB_SPMI_PON_SUBTYPE, &state[0]) == 0 &&
\t    kebab_spmi_pon_read(apid, KEBAB_SPMI_PON_SPARE, &state[1]) == 0) {
\t\tresult = 0;
\t}
\tkebab_spmi_bus_unlock();
\treturn result;
}
#endif /* KEBAB */''')
p.write_text(s,newline='\n')
p=d/'bsd/dev/kebab_rpmh.c';s=(r/'before'/p.relative_to(d)).read_text()
s=s.replace('#endif /* KEBAB */','''/* Read-only status snapshot. No control, vote, acknowledgement or clearing. */
void
kebab_rpmh_get_info(struct kebab_rpmh_info *info)
{
\tif (info == NULL) { return; }
\t*info = (struct kebab_rpmh_info) {
\t\t.resources = { KEBAB_RSC_DRV2_PA, KEBAB_RSC_SIZE,
\t\t    KEBAB_CMD_DB_PA, KEBAB_CMD_DB_SIZE },
\t\t.mapped = vrm.drv != 0 && vrm.tcs != 0,
\t\t.writes_enabled = vrm.gate,
\t\t.phase = (uint32_t)vrm.channel.phase,
\t};
}
int
kebab_rpmh_read_status(struct kebab_rpmh_info *info)
{
\tif (info == NULL) { return -1; }
\tkebab_rpmh_get_info(info);
\tif (!info->mapped) { return -1; }
\tinfo->irq_status = kebab_rpmh_read32(vrm.drv + KEBAB_RSC_TCS_OFF,
\t    KEBAB_RSC_IRQ_STATUS);
\tinfo->command_status = kebab_rpmh_read32(vrm.tcs, KEBAB_RSC_CMD_STATUS);
\treturn 0;
}
#endif /* KEBAB */''')
p.write_text(s,newline='\n')
p=d/'iokit/KernelConfigTables.cpp';s=(r/'before'/p.relative_to(d)).read_text();at=s.index('#endif\n#if defined(ARM64_BOARD_CONFIG_NEGRONI)')
rows=[('KebabSPMIBus','IOPlatformDevice','kebab,sm8250-spmi','IOBSD'),
      ('KebabPon','KebabSPMIDevice','pmic-pon',None),
      ('KebabPmicKeys','KebabSPMIDevice','pmic-keys',None),
      ('KebabVrm','KebabSPMIDevice','rpmh-vrm-policy',None)]
text=''
for name,provider,match,resource in rows:
    text+='    "   {"\n'
    for key,value in [('IOClass',name),('IOProviderClass',provider),('IONameMatch',match),('IOProbeScore','1000:32')]+([('IOResourceMatch',resource)] if resource else []):
        text+='    "     '+repr(key)+' = '+("'"+value+"'" if ',' in value or '-' in value else value)+';"\n'
    text+='    "   },"\n'
p.write_text(s[:at]+text+s[at:],newline='\n')
p=d/'iokit/conf/files.arm64';s=(r/'before/iokit/conf/files.arm64').read_text()
p.write_text(s+'\niokit/Kernel/arm/KebabSPMI.cpp\toptional iokitcpp\n',newline='\n')
p=d/'iokit/Kernel/arm/KebabDriverSelfTest.cpp';s=(r/'before'/p.relative_to(d)).read_text()
s=s.replace('extern "C" bool kebab_blockstorage_selftest(void);','extern "C" bool kebab_blockstorage_selftest(void);\nextern "C" bool kebab_spmi_services_selftest(void);')
s=s.replace('bool storage = kebab_blockstorage_selftest();','bool storage = kebab_blockstorage_selftest();\n\tbool spmi = kebab_spmi_services_selftest();')
s=s.replace('surface && display && storage ?', 'surface && display && storage && spmi ?')
p.write_text(s,newline='\n')
