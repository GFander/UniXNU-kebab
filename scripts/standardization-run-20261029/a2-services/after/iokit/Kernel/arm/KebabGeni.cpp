/* Normal resource/provider identities for the persistent GENI/TCM C cores.
 * Typed state queries are serialized with whole C protocol transactions.
 * The C core retains boot mappings and any polling thread for kernel lifetime;
 * stopping these services drains their queries, not that resident backend.
 * No HID class, report ABI, IRQ, or user client is claimed here. */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include "../../../bsd/dev/kebab_geni.h"

extern "C" bool kebab_geni_services_selftest(void);
static bool
kebab_geni_property(IOService *provider, const char *key, const void *value, size_t size)
{
	OSData *data = OSDynamicCast(OSData, provider->getProperty(key));
	return data != NULL && data->getLength() == size && memcmp(data->getBytesNoCopy(), value, size) == 0;
}

class KebabI2CDevice : public IOService {
	OSDeclareDefaultStructors(KebabI2CDevice);
};
OSDefineMetaClassAndStructors(KebabI2CDevice, IOService);
class KebabGeniBus : public IOService {
	OSDeclareDefaultStructors(KebabGeniBus);
	IOLock *fLock;
	KebabI2CDevice *fChild;
	bool fOnline, fAttached;
	void clearChild();
public:
	bool init(OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn copyState(struct kebab_geni_status *bus, struct kebab_touch_status *touch);
};
OSDefineMetaClassAndStructors(KebabGeniBus, IOService);
bool
KebabGeniBus::init(OSDictionary *properties)
{
	fLock = NULL;
	fChild = NULL;
	fOnline = false;
	fAttached = false;
	if (!IOService::init(properties)) { return false; }
	fLock = IOLockAlloc();
	return fLock != NULL;
}
bool
KebabGeniBus::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	struct kebab_geni_info info;
	kebab_geni_transaction_lock();
	kebab_geni_get_info(&info);
	kebab_geni_transaction_unlock();
	const uint32_t address = 0x4b;
	if (!info.mapped ||
	    !kebab_geni_property(provider, "reg", info.resources, sizeof(info.resources)) ||
	    !kebab_geni_property(provider, "kebab,touch-address", &address, sizeof(address))) {
		IOLog("KEBAB GENI IOKIT: ADT/backend resource contract rejected\n");
		IOService::stop(provider);
		return false;
	}
	fChild = new KebabI2CDevice;
	if (fChild == NULL || !fChild->init()) { clearChild(); IOService::stop(provider); return false; }
	fChild->setName("synaptics,s3908");
	if (!fChild->setProperty("i2c,address", provider->getProperty("kebab,touch-address"))) {
		clearChild(); IOService::stop(provider); return false;
	}
	fAttached = fChild->attach(this);
	if (!fAttached) { clearChild(); IOService::stop(provider); return false; }
	setName("KebabGeniBus");
	setProperty("backend-owner", "persistent boot C core");
	setProperty("bitbang-selected", info.bitbang);
	IOLockLock(fLock);
	fOnline = true;
	IOLockUnlock(fLock);
	registerService();
	fChild->registerService();
	IOLog("KEBAB GENI IOKIT: bus registered; S3908@4b scoped nub; existing transport retained\n");
	return true;
}
void
KebabGeniBus::clearChild()
{
	if (fChild == NULL) { return; }
	if (fAttached) {
		(void)fChild->terminate(kIOServiceSynchronous);
		if (fChild->getProvider() == this) { fChild->detach(this); }
	}
	fAttached = false;
	OSSafeReleaseNULL(fChild);
}
void
KebabGeniBus::stop(IOService *provider)
{
	IOLockLock(fLock);
	fOnline = false;
	IOLockUnlock(fLock);
	clearChild();
	IOService::stop(provider);
}
void
KebabGeniBus::free()
{
	clearChild();
	if (fLock != NULL) { IOLockFree(fLock); fLock = NULL; }
	IOService::free();
}
IOReturn
KebabGeniBus::copyState(struct kebab_geni_status *bus, struct kebab_touch_status *touch)
{
	if (bus == NULL || touch == NULL) { return kIOReturnBadArgument; }
	IOLockLock(fLock);
	IOReturn status = kIOReturnNoDevice;
	if (fOnline) {
		status = kebab_geni_read_status(bus) == 0 ? kIOReturnSuccess : kIOReturnNotReady;
		if (status == kIOReturnSuccess) { kebab_touch_get_status(touch); }
	}
	IOLockUnlock(fLock);
	return status;
}

static KebabGeniBus *
kebab_geni_copy_bus(IOService *client)
{
	IORegistryEntry *nub = client->copyParentEntry(gIOServicePlane);
	if (nub == NULL) { return NULL; }
	IORegistryEntry *parent = nub->copyParentEntry(gIOServicePlane);
	nub->release();
	KebabGeniBus *bus = OSDynamicCast(KebabGeniBus, parent);
	if (bus == NULL) { OSSafeReleaseNULL(parent); }
	return bus;
}
class KebabTouch : public IOService {
	OSDeclareDefaultStructors(KebabTouch);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn copyState(struct kebab_geni_status *bus, struct kebab_touch_status *touch);
};
OSDefineMetaClassAndStructors(KebabTouch, IOService);
bool
KebabTouch::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	const uint32_t address = 0x4b;
	KebabGeniBus *bus = kebab_geni_copy_bus(this);
	bool valid = bus != NULL && kebab_geni_property(provider, "i2c,address", &address, sizeof(address));
	OSSafeReleaseNULL(bus);
	if (!valid) { IOService::stop(provider); return false; }
	setName("KebabTouch");
	setProperty("protocol", "Synaptics TouchComm");
	setProperty("hid-published", false);
	registerService();
	IOLog("KEBAB GENI IOKIT: KebabTouch matched S3908@4b; typed state only, no HID\n");
	return true;
}
IOReturn
KebabTouch::copyState(struct kebab_geni_status *state, struct kebab_touch_status *touch)
{
	KebabGeniBus *bus = kebab_geni_copy_bus(this);
	IOReturn status = bus == NULL ? kIOReturnNoDevice : bus->copyState(state, touch);
	OSSafeReleaseNULL(bus);
	return status;
}
extern "C" bool
kebab_geni_services_selftest(void)
{
	OSDictionary *matching = IOService::serviceMatching("KebabTouch");
	if (matching == NULL) { return false; }
	IOService *service = IOService::waitForMatchingService(matching, 5ULL * kSecondScale);
	matching->release();
	KebabTouch *touch = OSDynamicCast(KebabTouch, service);
	struct kebab_geni_status state = {};
	struct kebab_touch_status protocol = {};
	bool passed = touch != NULL && touch->copyState(&state, &protocol) == kIOReturnSuccess &&
	    state.info.mapped && (state.info.bitbang || state.protocol == 3) && protocol.queue_ok;
	IOLog("KEBAB DRIVER TEST: GENI typed transport/TCM state %s (proto=%u mapped=%u bb=%u identified=%u decode=%u node=%u)\n",
	    passed ? "PASS" : "FAIL", state.protocol, state.info.mapped, state.info.bitbang,
	    protocol.identified, protocol.decoding, protocol.device_node);
	OSSafeReleaseNULL(service);
	return passed;
}
#endif /* KEBAB */
