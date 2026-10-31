"""Exercise the production command matcher, including binary KDP rejection."""
from pathlib import Path
import re
import subprocess
import tempfile

r=Path(__file__).parent
s=(r/'after/bsd/dev/kebab_usb.c').read_text()
def function(name):
    at=s.index('\n'+name+'('); start=s.rfind('\nstatic ',0,at)+1
    end=s.index('\n}\n',at)+3
    return s[start:end]
constants='\n'.join(re.findall(r'^#define UD_REBOOT_\w+\s+[^\n]+',s,re.M))
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static bool kdp;
static bool kebab_usb_kdp_enabled(void) { return kdp; }
static uint32_t ud_inst_command(const char *p, uint32_t n) { (void)p; (void)n; return 0; }
static uint32_t ud_vol_command(const char *p, uint32_t n) { (void)p; (void)n; return 0; }
'''
commands=[('TOUCH?',9),('TOUCHINIT',10),('TOUCHCLK',11),('TOUCHSE',12),
          ('TOUCHPING',13),('PWR?',14),('RPMH?',15),('DRV?',30),
          ('FASTBOOT',2),('FASTBOOT2',3),('TCS?',21),('FWLOG? all',17),('FWLOG?',16)]
main='int main(void) {\n'
for cmd, value in commands:
    main+=f'''{{
    const char command[] = "{cmd}\\n";
    const char binary[] = "\\xfa" "{cmd}\\n";
    kdp=false;
    assert(ud_reboot_command(command,sizeof(command)-1)=={value});
    assert(ud_reboot_command(binary,sizeof(binary)-1)=={value});
    kdp=true;
    assert(ud_reboot_command(command,sizeof(command)-1)=={value});
    assert(ud_reboot_command(binary,sizeof(binary)-1)==0);
    for(unsigned n=0;n<sizeof(command)-1;n++)
        assert(ud_reboot_command(command,n)==0);
}}\n'''
main+='puts("production matcher: 13 commands, truncation, prefix ordering, normal scan and KDP binary rejection PASS");\n}\n'
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.c';exe=p.with_suffix('')
    p.write_text(prefix+constants+'\n'+function('ud_command_seen')+'\n'+function('ud_reboot_command')+'\n'+main)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
