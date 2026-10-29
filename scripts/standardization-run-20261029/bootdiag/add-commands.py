from pathlib import Path
r=Path(__file__).parent
p=Path('bsd/dev/kebab_usb.c');s=(r/'before'/p).read_text()
s=s.replace('#define UD_REBOOT_DRV_REPORT    30u','#define UD_REBOOT_DRV_REPORT    30u\n#define UD_REBOOT_PROC_REPORT   31u\n#define UD_REBOOT_PID1_REPORT   32u\nextern void kebab_process_report(void);\nextern void kebab_pid1_report(void);')
anchor='\t\tif (ud_command_seen(buf, received, "DRV?\\n",'
assert s.count(anchor)==1
normal=''
for command,value in [('PROC?','PROC_REPORT'),('PID1?','PID1_REPORT')]:
    normal+='\t\tif (ud_command_seen(buf, received, "'+command+'\\n", sizeof("'+command+'\\n") - 1)) {\n\t\t\treturn UD_REBOOT_'+value+';\n\t\t}\n'
s=s.replace(anchor,normal+anchor)
anchor='\tif (received >= sizeof("DRV?\\n") - 1 &&'
assert s.count(anchor)==1
kdp=''
for command,value in [('PROC?','PROC_REPORT'),('PID1?','PID1_REPORT')]:
    kdp+='\tif (received >= sizeof("'+command+'\\n") - 1 && !memcmp(buf, "'+command+'\\n", sizeof("'+command+'\\n") - 1)) {\n\t\treturn UD_REBOOT_'+value+';\n\t}\n'
s=s.replace(anchor,kdp+anchor)
anchor='\t} else if (command == UD_REBOOT_DRV_REPORT) {'
s=s.replace(anchor,'\t} else if (command == UD_REBOOT_PROC_REPORT) {\n\t\tkebab_process_report();\n\t} else if (command == UD_REBOOT_PID1_REPORT) {\n\t\tkebab_pid1_report();\n'+anchor)
(r/'after'/p).write_text(s,newline='\n')
p=Path('bsd/conf/files');s=(r/'before'/p).read_text()
line=next(x for x in s.splitlines() if x.startswith('bsd/dev/kebab_keys.c'))
(r/'after'/p).write_text(s+'\n'+line.replace('bsd/dev/kebab_keys.c','bsd/kern/kebab_bootdiag.c')+'\n',newline='\n')
