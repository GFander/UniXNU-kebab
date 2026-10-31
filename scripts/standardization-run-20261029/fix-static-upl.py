"""Cover source-kernel constants below prelinked components in KEBAB IOMD."""
from pathlib import Path
import json,subprocess
r=Path(__file__).parent;d=r/'static-upl';files=['iokit/Kernel/IOMemoryDescriptor.cpp','iokit/Kernel/arm/NativeAES.cpp']
got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(files),text=True,capture_output=True,check=True).stdout)
for p,value in got.items():
    for folder in ['before','after']:
        f=d/folder/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(value['source'],newline='\n')
p=d/'after/iokit/Kernel/IOMemoryDescriptor.cpp';s=p.read_text()
needle='#define io_kernel_static_start vm_kext_base'
assert s.count(needle)==1
s=s.replace(needle,'''#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
/* This source kernel precedes its prelink ranges. Excluding its static
 * pages sends kernel constants through VM-object UPL lookup, which can
 * return zero-fill pages rather than the boot loader's actual mappings.
 * Keep the original static-UPL path for both source and prelinked pages;
 * io_get_kernel_static_upl still rejects an unmapped physical page. */
#define io_kernel_static_start ((vm_kernel_stext < vm_kext_base) ? vm_kernel_stext : vm_kext_base)
#else
#define io_kernel_static_start vm_kext_base
#endif''',1);p.write_text(s,newline='\n')
p=d/'after/iokit/Kernel/arm/NativeAES.cpp';s=p.read_text();needle='  IOReturn rawStatus = kIOReturnNotReady;';assert needle in s
s=s.replace(needle,'''  extern vm_offset_t vm_kernel_stext, vm_kext_base, static_memory_end;
  IOLog("KEBAB AES KAT: input=%p native-start=0x%lx prelink-start=0x%lx static-end=0x%lx\\n",
        input, vm_kernel_stext, vm_kext_base, static_memory_end);
'''+needle,1);p.write_text(s,newline='\n')
print('Prepared KEBAB static-UPL lower-bound correction')
