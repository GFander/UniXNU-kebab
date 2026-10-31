#include <stdint.h>
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
#define io_kernel_static_start ((vm_kernel_stext < vm_kext_base) ? vm_kernel_stext : vm_kext_base)
static kern_return_t
io_get_kernel_static_upl(
	vm_map_t                /* map */,
	uintptr_t               offset,
	upl_size_t              *upl_size,
	unsigned int            *page_offset,
	upl_t                   *upl,
	upl_page_info_array_t   page_list,
	unsigned int            *count,
	ppnum_t                 *highest_page)
{
	unsigned int pageCount, page;
	ppnum_t phys;
	ppnum_t highestPage = 0;

	pageCount = atop_32(round_page(*upl_size + (page_mask & offset)));
	if (pageCount > *count) {
		pageCount = *count;
	}
	*upl_size = (upl_size_t) ptoa_64(pageCount);

	*upl = NULL;
	*page_offset = ((unsigned int) page_mask & offset);

	for (page = 0; page < pageCount; page++) {
		phys = pmap_find_phys(kernel_pmap, ((addr64_t)offset) + ptoa_64(page));
		if (!phys) {
			break;
		}
		page_list[page].phys_addr = phys;
		page_list[page].free_when_done = 0;
		page_list[page].absent    = 0;
		page_list[page].dirty     = 0;
		page_list[page].precious  = 0;
		page_list[page].device    = 0;
		if (phys > highestPage) {
			highestPage = phys;
		}
	}

	*highest_page = highestPage;

	return (page >= pageCount) ? kIOReturnSuccess : kIOReturnVMError;
}
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
