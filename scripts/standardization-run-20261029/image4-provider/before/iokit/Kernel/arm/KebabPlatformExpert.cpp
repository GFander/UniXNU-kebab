/*
 * Minimal device-tree platform expert for the OnePlus 8T (kebab, SM8250).
 * Uses XNU's normal platform, power-domain and nub initialization; hardware
 * interrupt and power-management services are supplied by separate drivers.
 */
#include <pexpert/arm64/board_config.h>

#if defined(KEBAB)

class IOService;   /* only needed as the provider pointer type */

/* Started directly from processTopLevel: no /arm-io nub publishes the
 * display node, and the driver only needs a provider to attach to. */
class AppleCLCD;
extern "C" int kebab_display_start(IOService *provider);
#include <kern/debug.h>
#include <pexpert/pexpert.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>

/* the driver is C; these have to be declared before use */
extern "C" bool kebab_usb_console_ready(void);
extern "C" bool kebab_usb_restart_armed(void);
extern "C" void kebab_usb_panic_spin(void);

/*
 * Development-only compatibility with the unchanged donor AMFI. Its AON
 * helpers explicitly accept a NULL ApplePMGR provider: reads return -1 and
 * writes do nothing. Publish the original nub, never a fabricated PMIC or
 * a claim that Snapdragon implements Apple's persistent AON latch.
 *
 * The donor's metaclass allocates its full 0x740-byte instance. Standard
 * init is the rebound IOService::init, not the hardware init overload.
 * The private provider at +0x88 must remain NULL before publication.
 */
static IOService *kebab_development_pmgr;

static void
kebab_publish_development_pmgr(IOService *platform)
{
	uint32_t enabled = 0;
	if (!PE_parse_boot_argn("kebab_pmgr_nub", &enabled, sizeof(enabled)) ||
	    enabled != 1 || kebab_development_pmgr != NULL) {
		return;
	}
	OSObject *object = OSMetaClass::allocClassWithName("ApplePMGRNub");
	IOService *nub = OSDynamicCast(IOService, object);
	if (nub == NULL) {
		OSSafeReleaseNULL(object);
		IOLog("KEBAB PMGR: original ApplePMGRNub class unavailable\n");
		return;
	}
	if (!nub->init()) {
		nub->release();
		IOLog("KEBAB PMGR: development nub init failed\n");
		return;
	}
	/* This offset belongs to the audited donor ABI, beyond IOService.
	 * Read it only; do not alter the original object's private state. */
	if (sizeof(IOService) > 0x88 ||
	    nub->getMetaClass()->getClassSize() != 0x740 ||
	    *reinterpret_cast<void *const *>(reinterpret_cast<uintptr_t>(nub) + 0x88) != NULL) {
		nub->release();
		IOLog("KEBAB PMGR: donor ABI/provider mismatch; refusing publication\n");
		return;
	}
	nub->setProperty("kebab,development-compatibility", true);
	nub->setProperty("kebab,persistent-aon-latch", false);
	if (!nub->attach(platform)) {
		nub->release();
		IOLog("KEBAB PMGR: development nub attach failed\n");
		return;
	}
	if (*reinterpret_cast<void *const *>(reinterpret_cast<uintptr_t>(nub) + 0x88) != NULL) {
		nub->detach(platform);
		nub->release();
		IOLog("KEBAB PMGR: attach changed private provider; refusing publication\n");
		return;
	}
	/* Retain the allocation for the platform's lifetime. Do not start the
	 * hardware service or call any Apple register accessor from here. */
	kebab_development_pmgr = nub;
	nub->registerService();
	IOLog("KEBAB PMGR: provider-less development ApplePMGRNub published; "
	    "AON reads unsupported (0xffffffff), writes ignored; no PMIC MMIO\n");
}

class KebabPlatformExpert : public IODTPlatformExpert {
	OSDeclareDefaultStructors(KebabPlatformExpert);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void processTopLevel(IORegistryEntry *root) APPLE_KEXT_OVERRIDE;
	const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	bool getTargetName(char *name, int maxLength) APPLE_KEXT_OVERRIDE;
	bool getProductName(char *name, int maxLength) APPLE_KEXT_OVERRIDE;
	int haltRestart(unsigned int type) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(KebabPlatformExpert, IODTPlatformExpert);

bool
KebabPlatformExpert::start(IOService *provider)
{
	IOLog("KEBAB: starting device-tree platform expert\n");
	if (!IODTPlatformExpert::start(provider)) {
		return false;
	}
	kebab_publish_development_pmgr(this);
	registerService();
	IOLog("KEBAB: platform expert registered\n");
	return true;
}

const char *
KebabPlatformExpert::deleteList(void)
{
	return "()";
}

const char *
KebabPlatformExpert::excludeList(void)
{
	return "('chosen', 'defaults', 'cpus', 'options')";
}

void
KebabPlatformExpert::processTopLevel(IORegistryEntry *root)
{
	/*
	 * The stock implementation requires an Apple NVRAM /options node.
	 * KEBAB has no NVRAM controller; publish real CPU/device nodes without
	 * inventing one. createNubs adds the topology's logical CPU/cluster IDs.
	 */
	IORegistryEntry *cpus = root->childFromPath("cpus", gIODTPlane);
	if (cpus != NULL) {
		createNubs(this, IODTFindMatchingEntries(cpus, kIODTExclusive, NULL));
		cpus->release();
	}
	createNubs(this, IODTFindMatchingEntries(root, kIODTExclusive, excludeList()));

	/* Bring the panel up so IOMobileFramebuffer.framework finds it. */
	kebab_display_start(this);
}

bool
KebabPlatformExpert::getTargetName(char *name, int maxLength)
{
	if (name == NULL || maxLength <= 0) {
		return false;
	}
	strlcpy(name, "kebab", (size_t)maxLength);
	return true;
}

bool
KebabPlatformExpert::getProductName(char *name, int maxLength)
{
	if (name == NULL || maxLength <= 0) {
		return false;
	}
	strlcpy(name, "OnePlus 8T", (size_t)maxLength);
	return true;
}
#endif /* KEBAB */


/*
 * PEHaltRestart() eventually reaches IOPlatformExpert::haltRestart(), which
 * calls PE_halt_restart if one is installed.  Nothing on this port installs
 * it, so the stock path returns -1 without touching the hardware.  PSCI is
 * already the firmware interface used for CPU_ON (KebabCPU.cpp), so use its
 * SYSTEM_RESET function here.  On success the call does not return; a return
 * value is a PSCI error for the caller to report.
 */
static int
kebab_psci_system_reset(void)
{
	register uint64_t x0 __asm__("x0") = 0x84000009ULL; /* SYSTEM_RESET */

	__asm__ volatile("smc #0" : "+r"(x0) : :
	    "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
	    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
	return (int32_t)x0;
}

/*
 * The remaining PSCI calls used by the fastboot diagnosis.  They live here so
 * the firmware interface has one owner; the USB driver only decides when to
 * ask.  SYSTEM_RESET2 is SMC64-only (0xC4000012) and takes reset_type in x1
 * and an implementation-defined cookie in x2; reset_type 0 is the
 * architectural warm reset.  A return value means the call failed.
 */
extern "C" uint64_t
kebab_psci_call(uint64_t function, uint64_t arg1, uint64_t arg2)
{
	register uint64_t x0 __asm__("x0") = function;
	register uint64_t x1 __asm__("x1") = arg1;
	register uint64_t x2 __asm__("x2") = arg2;

	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2) : :
	    "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
	    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
	return x0;
}

/* PSCI_VERSION: major in bits 31:16, minor in bits 15:0. */
extern "C" uint32_t
kebab_psci_version(void)
{
	return (uint32_t)kebab_psci_call(0x84000000ULL, 0, 0);
}

/*
 * PSCI_FEATURES answers with PSCI_RET_NOT_SUPPORTED (0xFFFFFFFF) for a function
 * the firmware does not implement.  Every other value means supported: zero is
 * the normal "supported, no feature flags" answer, not a refusal.  Linux's
 * drivers/firmware/psci.c tests only != PSCI_RET_NOT_SUPPORTED for this reason,
 * and SYSTEM_RESET - which demonstrably resets this board - answers zero.
 *
 * Keep the raw 32-bit value so the console can print exactly what the firmware
 * said instead of a re-interpretation of it.
 */
#define KEBAB_PSCI_RET_NOT_SUPPORTED 0xffffffffu

extern "C" uint32_t
kebab_psci_features(uint32_t function)
{
	return (uint32_t)kebab_psci_call(0x8400000aULL, function, 0);
}

/*
 * SYSTEM_RESET2 is SMC64 and may return a 64-bit error.  On success it does not
 * return at all, so the caller reports the whole x0 when it does.
 */
extern "C" uint64_t
kebab_psci_system_reset2(uint32_t reset_type, uint32_t cookie)
{
	return kebab_psci_call(0xc4000012ULL, reset_type, cookie);
}

/*
 * A panic ends in a halt or a restart request. Halting leaves the USB controller
 * orphaned and the host loses the port, so when the panic report has been produced
 * we stay in a loop that keeps servicing it: the host can reconnect and later
 * console output still arrives. Any other halt/restart goes to the stock path.
 */
int
KebabPlatformExpert::haltRestart(unsigned int type)
{


	/*
	 * haltRestart also carries panic lifecycle notifications, and kPEPanicBegin
	 * arrives before the report has been written - spinning on that would swallow
	 * the panic output. Only take over the final stop.
	 *
	 * A restart requested by the console itself must not re-enter that loop:
	 * kebab_usb_panic_spin() refuses a second spinner and would wait forever.
	 */
	if (panic_active() && kebab_usb_console_ready() &&
	    !kebab_usb_restart_armed() &&
	    (type == kPEHaltCPU || type == kPERestartCPU ||
	    type == kPEPanicRestartCPU || type == kPEPanicRestartCPUNoCallouts)) {
		kebab_usb_panic_spin();          /* does not return */
	}
	/*
	 * A panic restart that did not take the console loop above has no
	 * console to keep.  The stock path would return -1 without touching the
	 * hardware and halt_all_cpus() would then park the board until a power
	 * cycle, so reset here instead.  This is the "cannot be serviced" arm of
	 * the panic policy: the console pump is the only reason to stay alive
	 * after a panic, and it is unavailable.
	 */
	if (type == kPERestartCPU ||
	    type == kPEPanicRestartCPU ||
	    type == kPEPanicRestartCPUNoCallouts) {
		if (panic_active() && !kebab_usb_console_ready()) {
			printf("KEBAB PANIC: no console to service; rebooting via PSCI SYSTEM_RESET\n");
		}
		return kebab_psci_system_reset();
	}
	/*
	 * A halt requested after a panic has nothing left to halt: the other CPUs
	 * are parked and the console pump is either running or unavailable.
	 * IODTPlatformExpert::haltRestart(kPEHaltCPU) is the shutdown-notification
	 * arm -- thread_call_allocate(), IORegistryEntry::fromPath() and
	 * pmRootDomain->handlePlatformHaltRestart() -- which allocates and takes
	 * mutexes.  PEHaltRestartInternal() skips it only because a kPEPanicBegin
	 * was delivered earlier, and a panic restart must not depend on that.
	 */
	if (panic_active() && type == kPEHaltCPU) {
		printf("KEBAB PANIC: halt requested after a panic; rebooting via PSCI SYSTEM_RESET\n");
		return kebab_psci_system_reset();
	}
	return IODTPlatformExpert::haltRestart(type);
}
