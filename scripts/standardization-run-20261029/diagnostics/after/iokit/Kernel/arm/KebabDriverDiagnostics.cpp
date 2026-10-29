/* Normal console only. The C dispatcher refuses DRV? before reaching IOKit
 * during panic. No blocking wait, hardware writes, or matching side effects. */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSIterator.h>
#include "KebabDriverManifest.h"

struct KebabDriverManifestEntry {
	const char *group, *service, *scope;
};
#define KEBAB_DRIVER_ENTRY(group, service, scope) { group, service, scope },
static const KebabDriverManifestEntry manifest[] = {
	KEBAB_DRIVER_MANIFEST(KEBAB_DRIVER_ENTRY)
};
#undef KEBAB_DRIVER_ENTRY

extern "C" void kebab_driver_report(void);
extern "C" void
kebab_driver_report(void)
{
	IOLog("KEBAB DRV: manifest v1; registry state is observed now; C entries have no service\n");
	for (unsigned i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
		const KebabDriverManifestEntry &entry = manifest[i];
		if (entry.service == NULL) {
			IOLog("KEBAB DRV: %s backend=C service=none scope=%s\n", entry.group, entry.scope);
			continue;
		}
		OSDictionary *matching = IOService::serviceMatching(entry.service);
		if (matching == NULL) {
			IOLog("KEBAB DRV: %s query=allocation-failed\n", entry.group);
			continue;
		}
		OSIterator *iterator = IOService::getMatchingServices(matching);
		matching->release();
		unsigned count = 0;
		if (iterator) {
			while (count < 8) {
				IOService *service = OSDynamicCast(IOService, iterator->getNextObject());
				if (service == NULL) { break; }
				IORegistryEntry *provider = service->copyParentEntry(gIOServicePlane);
				IOLog("KEBAB DRV: %s class=%s id=%llx state=0x%x inactive=%u provider=%s\n",
				    entry.group, service->getMetaClass()->getClassName(), service->getRegistryEntryID(),
				    (unsigned)service->getState(), service->isInactive(),
				    provider == NULL ? "none" : provider->getName());
				OSSafeReleaseNULL(provider);
				count++;
			}
			IOLog("KEBAB DRV: %s class=%s matched=%u%s valid=%u scope=%s\n",
			    entry.group, entry.service, count, count == 8 ? "+" : "",
			    iterator->isValid(), entry.scope);
			iterator->release();
		} else {
			IOLog("KEBAB DRV: %s class=%s query=unavailable scope=%s\n",
			    entry.group, entry.service, entry.scope);
		}
	}
	IOLog("KEBAB DRV: end; matched service does not certify user ABI or hardware capability\n");
}
#endif /* KEBAB */
