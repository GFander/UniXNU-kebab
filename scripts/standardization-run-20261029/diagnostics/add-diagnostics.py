from pathlib import Path

r = Path(__file__).parent
p = Path('bsd/dev/kebab_usb.c')
s = (r/'before'/p).read_text()
s = s.replace('#define UD_REBOOT_SYNC          29u',
              '#define UD_REBOOT_SYNC          29u\n#define UD_REBOOT_DRV_REPORT    30u\n\nextern void kebab_driver_report(void);')
commands = [('TOUCH?', 'TOUCH_PROBE'), ('TOUCHINIT', 'TOUCH_INIT'),
            ('TOUCHCLK', 'TOUCH_CLOCK'), ('TOUCHSE', 'TOUCH_SE'),
            ('TOUCHPING', 'TOUCH_PING'), ('PWR?', 'PWR_REPORT'),
            ('RPMH?', 'RPMH_REPORT'), ('DRV?', 'DRV_REPORT')]
anchor = '\t\t/* "FWLOG? all\\n" must be tried before the bare form. */'
assert s.count(anchor) == 1
normal = ''.join('\t\tif (ud_command_seen(buf, received, "'+cmd+'\\n",\n'
                 '\t\t    sizeof("'+cmd+'\\n") - 1)) {\n'
                 '\t\t\treturn UD_REBOOT_'+value+';\n\t\t}\n'
                 for cmd, value in commands)
s = s.replace(anchor, normal + anchor)
anchor = '\tif (received >= sizeof("FWLOG? all\\n") - 1 &&'
assert s.count(anchor) == 1
s = s.replace(anchor, '\tif (received >= sizeof("DRV?\\n") - 1 &&\n'
              '\t    !memcmp(buf, "DRV?\\n", sizeof("DRV?\\n") - 1)) {\n'
              '\t\treturn UD_REBOOT_DRV_REPORT;\n\t}\n' + anchor)
anchor = '\t} else if (command == UD_REBOOT_SPMI_REPORT) {'
assert s.count(anchor) == 1
s = s.replace(anchor, '\t} else if (command == UD_REBOOT_DRV_REPORT) {\n'
              '\t\tkebab_driver_report();\n' + anchor)
(r/'after'/p).write_text(s, newline='\n')
p = Path('iokit/conf/files.arm64')
s = (r/'before'/p).read_text()
(r/'after'/p).write_text(s+'\niokit/Kernel/arm/KebabDriverDiagnostics.cpp\toptional iokitcpp\n', newline='\n')
