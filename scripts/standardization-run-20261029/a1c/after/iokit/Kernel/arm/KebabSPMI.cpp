/*
 * SM8250 normal IOKit identities and typed clients of the boot-owned C cores.
 * The arbiter validates the actual ADT resource pairs against mapped backend
 * resources before publishing scoped child nubs. RPMh is a separate RSC
 * transport; its child is board policy dependent on SPMI PMIC readback.
 * Early/panic recovery never calls this file and retains its mappings even
 * when these normal services stop. No private Apple class ABI is imitated.
 */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include "../../../bsd/dev/kebab_spmi_bus.h"
#include "../../../bsd/dev/kebab_spmi_clients.h"

extern "C" bool kebab_spmi_services_selftest(void);

static bool
kebab_spmi_property(IOService *provider, const char *key, const void *value, size_t size)
{
	OSData *data = OSDynamicCast(OSData, provider->getProperty(key));
	return data != NULL && data->getLength() == size &&
	       memcmp(data->getBytesNoCopy(), value, size) == 0;
}

class KebabSPMIDevice : public IOService {
	OSDeclareDefaultStructors(KebabSPMIDevice);
};
OSDefineMetaClassAndStructors(KebabSPMIDevice, IOService);

class KebabSPMIBus : public IOService {
	OSDeclareDefaultStructors(KebabSPMIBus);
	IOLock *fLock;
	OSArray *fNubs;
	bool fOnline, fKeysEnabled;
	void clearChildren();
	bool addChild(const char *name, const char *property, OSObject *specifier,
	    OSObject *second = NULL);
public:
	bool init(OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	bool keysEnabled() const { return fKeysEnabled; }
	IOReturn ponState(uint8_t state[2]);
	IOReturn keysSample(uint8_t raw[4]);
	IOReturn vrmStatus(struct kebab_rpmh_info *info);
};
OSDefineMetaClassAndStructors(KebabSPMIBus, IOService);

bool
KebabSPMIBus::init(OSDictionary *properties)
{
	fLock = NULL;
	fNubs = NULL;
	fOnline = false;
	fKeysEnabled = false;
	if (!IOService::init(properties)) { return false; }
	fLock = IOLockAlloc();
	fNubs = OSArray::withCapacity(3);
	return fLock != NULL && fNubs != NULL;
}

bool
KebabSPMIBus::addChild(const char *name, const char *property, OSObject *specifier,
    OSObject *second)
{
	KebabSPMIDevice *nub = new KebabSPMIDevice;
	if (nub == NULL) { return false; }
	bool initialized = nub->init();
	if (initialized) {
		nub->setName(name);
		initialized = nub->setProperty(property, specifier);
		if (initialized && second != NULL) {
			initialized = nub->setProperty("rpmh,command-db", second);
		}
	}
	bool attached = initialized && nub->attach(this);
	bool stored = attached && fNubs->setObject(nub);
	if (attached && !stored) { nub->detach(this); }
	nub->release();
	return stored;
}

bool
KebabSPMIBus::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	struct kebab_spmi_bus_info info;
	struct kebab_rpmh_info rpmh;
	kebab_spmi_bus_get_info(&info);
	kebab_rpmh_get_info(&rpmh);
	const uint32_t ee = info.ee;
	const uint32_t pon[] = { 0, 0x08 };
	const uint32_t keys[] = { 0, 0x08, 0xc5, 0xc6 };
	if (!info.mapped || !kebab_spmi_version_known(info.version) || info.version < 0x50000000u ||
	    !kebab_spmi_property(provider, "reg", info.resources, sizeof(info.resources)) ||
	    !kebab_spmi_property(provider, "qcom,ee", &ee, sizeof(ee)) ||
	    !kebab_spmi_property(provider, "kebab,pon", pon, sizeof(pon)) ||
	    !kebab_spmi_property(provider, "kebab,pmic-keys", keys, sizeof(keys)) ||
	    !kebab_spmi_property(provider, "kebab,rpmh-rsc", rpmh.resources, 2 * sizeof(uint64_t)) ||
	    !kebab_spmi_property(provider, "kebab,rpmh-cmd-db", rpmh.resources + 2, 2 * sizeof(uint64_t))) {
		IOLog("KEBAB SPMI IOKIT: ADT/backend resource contract rejected\n");
		IOService::stop(provider);
		return false;
	}
	uint32_t enabled = 0;
	fKeysEnabled = PE_parse_boot_argn("kebab_keys", &enabled, sizeof(enabled)) && enabled != 0;
	if (!addChild("pmic-pon", "spmi,specifier", provider->getProperty("kebab,pon")) ||
	    !addChild("pmic-keys", "spmi,specifier", provider->getProperty("kebab,pmic-keys")) ||
	    !addChild("rpmh-vrm-policy", "rpmh,rsc", provider->getProperty("kebab,rpmh-rsc"),
	    provider->getProperty("kebab,rpmh-cmd-db"))) {
		clearChildren();
		IOService::stop(provider);
		return false;
	}
	setName("KebabSPMIBus");
	setProperty("backend-owner", "boot C core");
	setProperty("arbiter-version", info.version, 32);
	IOLockLock(fLock);
	fOnline = true;
	IOLockUnlock(fLock);
	registerService();
	for (unsigned i = 0; i < fNubs->getCount(); i++) {
		((IOService *)fNubs->getObject(i))->registerService();
	}
	IOLog("KEBAB SPMI IOKIT: bus registered; 3 scoped nubs, C core retains panic mappings\n");
	return true;
}

void
KebabSPMIBus::clearChildren()
{
	if (fNubs == NULL) { return; }
	for (unsigned i = 0; i < fNubs->getCount(); i++) {
		IOService *nub = (IOService *)fNubs->getObject(i);
		(void)nub->terminate(kIOServiceSynchronous);
		if (nub->getProvider() == this) { nub->detach(this); }
	}
	fNubs->flushCollection();
}

void
KebabSPMIBus::stop(IOService *provider)
{
	IOLockLock(fLock);
	fOnline = false;
	IOLockUnlock(fLock);
	clearChildren();
	IOService::stop(provider);
}

void
KebabSPMIBus::free()
{
	clearChildren();
	OSSafeReleaseNULL(fNubs);
	if (fLock != NULL) { IOLockFree(fLock); fLock = NULL; }
	IOService::free();
}

IOReturn
KebabSPMIBus::ponState(uint8_t state[2])
{
	if (state == NULL) { return kIOReturnBadArgument; }
	IOLockLock(fLock);
	IOReturn status = !fOnline ? kIOReturnNoDevice :
	    kebab_spmi_pon_state(state) == 0 ? kIOReturnSuccess : kIOReturnIOError;
	IOLockUnlock(fLock);
	return status;
}

IOReturn
KebabSPMIBus::keysSample(uint8_t raw[4])
{
	if (raw == NULL) { return kIOReturnBadArgument; }
	IOLockLock(fLock);
	IOReturn status = !fOnline ? kIOReturnNoDevice : !fKeysEnabled ? kIOReturnNotReady :
	    kebab_spmi_keys_sample(raw) == 0 ? kIOReturnSuccess : kIOReturnIOError;
	IOLockUnlock(fLock);
	return status;
}

IOReturn
KebabSPMIBus::vrmStatus(struct kebab_rpmh_info *info)
{
	if (info == NULL) { return kIOReturnBadArgument; }
	IOLockLock(fLock);
	IOReturn status = kIOReturnNoDevice;
	if (fOnline) {
		status = kebab_rpmh_read_status(info) == 0 ? kIOReturnSuccess : kIOReturnNotReady;
	}
	IOLockUnlock(fLock);
	return status;
}

/* Returned parent is retained across a typed call, including detach/stop. */
static KebabSPMIBus *
kebab_spmi_copy_bus(IOService *client)
{
	IORegistryEntry *nub = client->copyParentEntry(gIOServicePlane);
	if (nub == NULL) { return NULL; }
	IORegistryEntry *parent = nub->copyParentEntry(gIOServicePlane);
	nub->release();
	KebabSPMIBus *bus = OSDynamicCast(KebabSPMIBus, parent);
	if (bus == NULL) { OSSafeReleaseNULL(parent); }
	return bus;
}

class KebabPon : public IOService {
	OSDeclareDefaultStructors(KebabPon);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn getState(uint8_t state[2]);
};
OSDefineMetaClassAndStructors(KebabPon, IOService);
bool
KebabPon::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	const uint32_t expected[] = { 0, 0x08 };
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	bool valid = bus != NULL && kebab_spmi_property(provider, "spmi,specifier", expected, sizeof(expected));
	OSSafeReleaseNULL(bus);
	if (!valid) { IOService::stop(provider); return false; }
	setName("KebabPon");
	setProperty("normal-state-query", true);
	registerService();
	IOLog("KEBAB SPMI IOKIT: KebabPon matched usid0/peripheral08\n");
	return true;
}
IOReturn
KebabPon::getState(uint8_t state[2])
{
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	IOReturn status = bus == NULL ? kIOReturnNoDevice : bus->ponState(state);
	OSSafeReleaseNULL(bus);
	return status;
}

class KebabPmicKeys : public IOService {
	OSDeclareDefaultStructors(KebabPmicKeys);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn getSample(uint8_t raw[4]);
};
OSDefineMetaClassAndStructors(KebabPmicKeys, IOService);
bool
KebabPmicKeys::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	const uint32_t expected[] = { 0, 0x08, 0xc5, 0xc6 };
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	bool valid = bus != NULL && kebab_spmi_property(provider, "spmi,specifier", expected, sizeof(expected));
	if (!valid) { OSSafeReleaseNULL(bus); IOService::stop(provider); return false; }
	setName("KebabPmicKeys");
	setProperty("polling-enabled", bus->keysEnabled());
	bus->release();
	registerService();
	IOLog("KEBAB SPMI IOKIT: KebabPmicKeys matched usid0/pages08,c5,c6; polling policy retained\n");
	return true;
}
IOReturn
KebabPmicKeys::getSample(uint8_t raw[4])
{
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	IOReturn status = bus == NULL ? kIOReturnNoDevice : bus->keysSample(raw);
	OSSafeReleaseNULL(bus);
	return status;
}

class KebabVrm : public IOService {
	OSDeclareDefaultStructors(KebabVrm);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn getStatus(struct kebab_rpmh_info *info);
};
OSDefineMetaClassAndStructors(KebabVrm, IOService);
bool
KebabVrm::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	struct kebab_rpmh_info info;
	kebab_rpmh_get_info(&info);
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	bool valid = bus != NULL &&
	    kebab_spmi_property(provider, "rpmh,rsc", info.resources, 2 * sizeof(uint64_t)) &&
	    kebab_spmi_property(provider, "rpmh,command-db", info.resources + 2, 2 * sizeof(uint64_t));
	OSSafeReleaseNULL(bus);
	if (!valid) { IOService::stop(provider); return false; }
	setName("KebabVrm");
	setProperty("transport", "RPMh RSC; SPMI is PMIC readback dependency");
	setProperty("backend-mapped", info.mapped);
	setProperty("writes-enabled", info.writes_enabled);
	registerService();
	IOLog("KEBAB SPMI IOKIT: KebabVrm matched RSC policy/readback resources; no IOPM claim\n");
	return true;
}
IOReturn
KebabVrm::getStatus(struct kebab_rpmh_info *info)
{
	KebabSPMIBus *bus = kebab_spmi_copy_bus(this);
	IOReturn status = bus == NULL ? kIOReturnNoDevice : bus->vrmStatus(info);
	OSSafeReleaseNULL(bus);
	return status;
}

static IOService *
kebab_spmi_copy_service(const char *name)
{
	OSDictionary *matching = IOService::serviceMatching(name);
	if (matching == NULL) { return NULL; }
	IOService *service = IOService::waitForMatchingService(matching, 5ULL * kSecondScale);
	matching->release();
	return service;
}

extern "C" bool
kebab_spmi_services_selftest(void)
{
	IOService *p = kebab_spmi_copy_service("KebabPon");
	IOService *k = kebab_spmi_copy_service("KebabPmicKeys");
	IOService *v = kebab_spmi_copy_service("KebabVrm");
	KebabPon *pon = OSDynamicCast(KebabPon, p);
	KebabPmicKeys *keys = OSDynamicCast(KebabPmicKeys, k);
	KebabVrm *vrm = OSDynamicCast(KebabVrm, v);
	uint8_t state[2] = {}, raw[4] = {};
	struct kebab_rpmh_info info = {};
	bool passed = pon != NULL && keys != NULL && vrm != NULL &&
	    pon->getState(state) == kIOReturnSuccess && state[0] == 0x04 &&
	    keys->getSample(raw) == kIOReturnSuccess && raw[3] == 0x10 &&
	    vrm->getStatus(&info) == kIOReturnSuccess && info.mapped;
	IOLog("KEBAB DRIVER TEST: SPMI typed PON/keys/RSC %s (PON %02x/%02x keys %02x/%02x/%02x/%02x TCS1 %08x)\n",
	    passed ? "PASS" : "FAIL", state[0], state[1], raw[0], raw[1], raw[2], raw[3], info.command_status);
	OSSafeReleaseNULL(p);
	OSSafeReleaseNULL(k);
	OSSafeReleaseNULL(v);
	return passed;
}
#endif /* KEBAB */
