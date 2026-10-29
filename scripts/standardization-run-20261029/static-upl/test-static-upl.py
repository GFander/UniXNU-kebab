"""Exercise the production static-UPL walker at 4/16 KiB and range boundaries."""
from pathlib import Path
import os,subprocess,tempfile,sys
r=Path(__file__).parent
s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'after/iokit/Kernel/IOMemoryDescriptor.cpp').read_text()
start=s.index('static kern_return_t\nio_get_kernel_static_upl(')
at=s.index('{',start);depth=1;end=at+1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end]
macro=next(x for x in s.splitlines() if x.startswith('#define io_kernel_static_start (('))
prefix='''#include <stdint.h>
#include <assert.h>
#include <stddef.h>
typedef int kern_return_t;
typedef uintptr_t vm_map_t;
typedef uint32_t upl_size_t;
typedef uint64_t ppnum_t,addr64_t;
typedef void *upl_t;
typedef struct { ppnum_t phys_addr; unsigned free_when_done,absent,dirty,precious,device; } page_t;
typedef page_t *upl_page_info_array_t;
#define PAGE_SIZE (1ULL << PAGE_SHIFT)
#define page_mask (PAGE_SIZE - 1)
#define round_page(x) (((x) + page_mask) & ~page_mask)
#define atop_32(x) ((x) >> PAGE_SHIFT)
#define ptoa_64(x) ((uint64_t)(x) << PAGE_SHIFT)
#define kIOReturnSuccess 0
#define kIOReturnVMError 1
static uintptr_t vm_kernel_stext,vm_kext_base;
static const vm_map_t kernel_pmap=1;
static uintptr_t mapped_start,mapped_end;
static ppnum_t pmap_find_phys(vm_map_t m,addr64_t va) {
 assert(m==kernel_pmap);
 return va>=mapped_start && va<mapped_end ? 0x500+(va-mapped_start)/PAGE_SIZE : 0;
}
'''
harness='''
int main(void) {
 vm_kernel_stext=0x6000000;vm_kext_base=0x8000000;
 assert(io_kernel_static_start==vm_kernel_stext);
 assert(0x6c00000>=io_kernel_static_start && 0x6c00000<vm_kext_base);
 assert(!(0x5ffffff>=io_kernel_static_start));
 vm_kernel_stext=0xa000000;assert(io_kernel_static_start==vm_kext_base);
 vm_kernel_stext=vm_kext_base;assert(io_kernel_static_start==vm_kext_base);
 mapped_start=0x10000000; mapped_end=mapped_start+PAGE_SIZE*3;
 page_t pages[4];unsigned off,count;ppnum_t high;upl_t upl;
 upl_size_t size=PAGE_SIZE;count=4;
 assert(!io_get_kernel_static_upl(0,mapped_start+17,&size,&off,&upl,pages,&count,&high));
 assert(off==17 && size==PAGE_SIZE*2 && !upl && high==0x501);
 assert(pages[0].phys_addr==0x500 && pages[1].phys_addr==0x501 && !pages[1].dirty);
 size=PAGE_SIZE*4;count=4;
 assert(io_get_kernel_static_upl(0,mapped_start,&size,&off,&upl,pages,&count,&high)==kIOReturnVMError);
 assert(high==0x502);
 size=PAGE_SIZE*4;count=2;
 assert(!io_get_kernel_static_upl(0,mapped_start,&size,&off,&upl,pages,&count,&high));
 assert(size==PAGE_SIZE*2 && high==0x501);
 size=PAGE_SIZE;count=4;
 assert(io_get_kernel_static_upl(0,mapped_end,&size,&off,&upl,pages,&count,&high)==kIOReturnVMError);
 assert(!high);
 return 0;
}
'''
if os.name=='nt':
    (r/'static-upl-host.c').write_text(prefix+macro+'\n'+body+harness,newline='\n')
    print('Generated host harness from production walker')
else:
    with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as temp:
        for shift in [12,14]:
            exe=Path(temp)/('upl-'+str(shift))
            subprocess.run(['clang++','-x','c++','-std=c++17','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-DPAGE_SHIFT='+str(shift),
                str(r/'static-upl-host.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    print('PASS: production static-UPL walker, min-range routing, offsets, cap, missing-page refusal at 4/16 KiB')
