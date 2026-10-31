"""Adapt existing actual-function fault tests to the extracted reset source."""
from pathlib import Path
r=Path(__file__).parent;s=(r/'test-panic-fastboot-before.py').read_text()
s=s.replace("machine = Path(sys.argv[2]).read_text()", "machine = Path(sys.argv[2]).read_text()\nreset = Path(sys.argv[3]).read_text()")
s=s.replace("functions = '\\n'.join(function(usb, name) for name in names)","functions = '\\n'.join(function(reset if name != 'ud_run_reboot_command' else usb, name) for name in names)")
s=s.replace('                                  usb, re.M))','                                  usb + reset, re.M))')
mapping={'ud_fastboot_cookie':'kr_fastboot_cookie','ud_platform_restart2':'kr_platform_restart2','ud_platform_restart':'kebab_reboot_plain','ud_fastboot_restart':'kebab_reboot_fastboot'}
for before,after in mapping.items():s=s.replace(before,after)
s=s.replace('#define KEBAB 1','#define KEBAB 1\n#define kr ud')
s=s.replace('static uint32_t kr_fastboot_cookie', 'static int kebab_spmi_pon_reason_write_normal(void) { assert(!is_panic); ++pon_calls; return pon_rc; }\nstatic uint32_t kr_fastboot_cookie')
s=s.replace("['clang', '-Wall', '-Wextra', '-Werror',", "['clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',")
(r/'test-panic-fastboot.py').write_text(s,newline='\n')
print('Prepared actual-function reset/dispatch/halt ASAN/UBSAN tests')
