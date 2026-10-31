/*
 * Optional read-only native factory tests. This exercises this kernel's
 * IOService/IOUserClient path, not the private userland framework ABI.
 * Off by default; never run from early/panic USB or an interrupt context.
 */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <kern/task.h>
#include <pexpert/pexpert.h>

extern "C" void kebab_driver_selftest(void);
extern "C" bool kebab_blockstorage_selftest(void);

static bool
kebab_client_selftest(const char *name, bool surface)
{
	OSDictionary *matching = IOService::serviceMatching(name);
	if (matching == NULL) {
		return false;
	}
	IOService *service = IOService::waitForMatchingService(matching,
	    5ULL * kSecondScale);
	matching->release();
	if (!service) {
		IOLog("KEBAB DRIVER TEST: %s not published\n", name);
		return false;
	}
	IOUserClient *client = NULL;
	IOReturn status = service->newUserClient(kernel_task, NULL, 0, NULL, &client);
	if (status != kIOReturnSuccess || client == NULL) {
		IOLog("KEBAB DRIVER TEST: %s open failed 0x%x\n", name, status);
		service->release();
		return false;
	}
	IOExternalMethodArguments args = {};
	uint8_t limits[0x18] = {};
	uint64_t dimensions[2] = {};
	uint32_t selector = surface ? 13 : 8;
	if (surface) {
		args.structureOutput = limits;
		args.structureOutputSize = sizeof(limits);
	} else {
		args.scalarOutput = dimensions;
		args.scalarOutputCount = 2;
	}
	status = client->externalMethod(selector, &args, NULL, NULL, NULL);
	bool passed = status == kIOReturnSuccess && (surface ?
	    args.structureOutputSize == sizeof(limits) && limits[0x16] == 1 :
	    args.scalarOutputCount == 2 && dimensions[0] != 0 && dimensions[1] != 0);
	/* A missing output must be refused without touching a NULL pointer. */
	bzero(&args, sizeof(args));
	IOReturn invalid = client->externalMethod(selector, &args, NULL, NULL, NULL);
	passed &= invalid == (surface ? kIOReturnNoSpace : kIOReturnBadArgument);
	IOReturn closed = client->clientClose();
	IOReturn afterClose = client->externalMethod(selector, &args, NULL, NULL, NULL);
	passed &= closed == kIOReturnSuccess && afterClose == kIOReturnNoDevice;
	client->release();
	service->release();
	IOLog("KEBAB DRIVER TEST: %s factory/query/bad-output/close %s (0x%x/0x%x/0x%x)\n",
	    name, passed ? "PASS" : "FAIL", status, invalid, afterClose);
	return passed;
}

extern "C" void
kebab_driver_selftest(void)
{
	uint32_t enabled = 0;
	if (!PE_parse_boot_argn("kebab_driver_test", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	bool surface = kebab_client_selftest("IOCoreSurfaceRoot", true);
	bool display = kebab_client_selftest("AppleCLCD", false);
	bool storage = kebab_blockstorage_selftest();
	IOLog("KEBAB DRIVER TEST: read-only native contracts %s\n",
	    surface && display && storage ? "PASS" : "FAIL");
}
#endif /* KEBAB */
