#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
/*
 * KEBAB block storage: publish the userdata partition as an IOKit block device.
 *
 * bsd/dev/kebab_ufs.c owns the Qualcomm UFS host and exposes the partition as
 * a BSD disk plus a byte-offset raw read/write interface. APFS is an IOKit
 * driver: it matches IOMedia, which IOStorageFamily creates for a driver that
 * subclasses IOBlockStorageDevice. This file is that subclass. It does not
 * touch the UFS hardware itself and does not replace the BSD interface; every
 * transfer goes through kebab_ufs_raw_read()/kebab_ufs_raw_write().
 *
 * Layout of the stack once registered:
 *
 *   IOPlatformExpertDevice          (existing provider)
 *     KebabBlockStorageDevice       (this file: the nub APFS is not aware of)
 *       IOBlockStorageDriver        (Apple, from the prelinked IOStorageFamily)
 *         IOMedia                   (whole device, <product string> Media)
 *
 * Registration is gated by boot-arg ufsmedia=1 (default off) and is called
 * from kebab_ufs_rootdev() after the UFS partition has come up. Every failure
 * is printed and ignored: a boot must not depend on this service.
 *
 * ABI note: this class is compiled against a vendored IOBlockStorageDevice.h
 * shaped to the iOS 16.7.16 IOStorageFamily dispatch table. See the comment at
 * the top of that header and docs/BRING-UP-STATE.md section 87; the header
 * mismatch is the main risk of this driver.
 */
/*
 * The prelinked IOStorageFamily is an iOS kext: its IOStorage/IOMedia classes
 * were built with TARGET_OS_OSX == 0. The vendored storage headers use that
 * switch to select the class shape, so make the same choice here. It is 0 (not
 * merely undefined) to keep -Wundef-prefix quiet.
 */
#ifndef TARGET_OS_OSX
#define TARGET_OS_OSX 0
#endif

/*
 * Apple's headers use 0 for null defaults and do not spell out every override;
 * the kernel builds with -Weverything, so keep those diagnostics inside the
 * include block. They are style warnings in vendored text, not in this driver.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wsuggest-override"
#pragma clang diagnostic ignored "-Wsuggest-destructor-override"
#pragma clang diagnostic ignored "-Wshadow-field"

#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <sys/types.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <kern/locks.h>

extern "C" {
#include "../../../bsd/dev/kebab_ufs.h"
}

#pragma clang diagnostic pop

/* Defined at the end of this file; the prototype keeps -Wmissing-prototypes quiet. */
extern "C" int kebab_blockstorage_start(void);
extern "C" int kebab_blockstorage_rootdev(dev_t *, char *, size_t, IOService **);
extern "C" bool kebab_blockstorage_selftest(void);

/* Staging buffer for one transfer chunk. A multiple of the 4 KiB UFS block. */
#define KEBAB_BLOCK_CHUNK	(64 * 1024)

class KebabBlockStorageDevice : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(KebabBlockStorageDevice);

	/*
	 * The iOS 16.7.16 IOBlockStorageDevice places doSynchronizeCache() at vtable
	 * slot 171; the macOS header names the same slot doLockUnlockMedia().  The
	 * base implementation returns kIOReturnUnsupported and so does this one.
	 */
	virtual IOReturn	doSynchronizeCache();

	UInt64	fBlockSize;
	UInt64	fBlockCount;
	bool	fWritable;
	bool	fMediaStateReported;
	bool	fOnline;
	void *	fBounce;
	IOLock *fLock;

	static IOReturn statusForError(int error);

public:
	bool init(OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;

	IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE;
	IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
	UInt32 doGetFormatCapacities(UInt64 *capacities,
	    UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
	char * getVendorString(void) APPLE_KEXT_OVERRIDE;
	char * getProductString(void) APPLE_KEXT_OVERRIDE;
	char * getRevisionString(void) APPLE_KEXT_OVERRIDE;
	char * getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE;
	IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
	IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
	IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
	IOReturn reportMediaState(bool *mediaPresent,
	    bool *changedState = nullptr) APPLE_KEXT_OVERRIDE;
	IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
	IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
	IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block,
	    UInt64 nblks, IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
	IOReturn doSynchronize(UInt64 block, UInt64 nblks,
	    IOStorageSynchronizeOptions options) APPLE_KEXT_OVERRIDE;
	IOReturn doUnmap(IOBlockStorageDeviceExtent *extents,
	    UInt32 extentsCount, IOStorageUnmapOptions options) APPLE_KEXT_OVERRIDE;
	IOReturn requestIdle(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(KebabBlockStorageDevice, IOBlockStorageDevice);

/* C backend owns the controller; this boot-lifetime bridge owns only its
 * staging buffer. Publication is serialized independently of the backend lock:
 * matching can call geometry/read methods, so never hold kebab_ufs_lock here. */
static KebabBlockStorageDevice *kebab_block_device;
static LCK_GRP_DECLARE(kebab_block_group, "kebab_block");
static LCK_MTX_DECLARE(kebab_block_publish_lock, &kebab_block_group);

static char kebabVendorString[] = "KEBAB";
static char kebabProductString[] = "UFS userdata";
static char kebabRevisionString[] = "1";
static char kebabAdditionalString[] = "OnePlus 8T UFS LUN 0";

IOReturn
KebabBlockStorageDevice::statusForError(int error)
{
	switch (error) {
	case 0:
		return kIOReturnSuccess;
	case EROFS:
		return kIOReturnNotWritable;
	case ENXIO:
		return kIOReturnNoDevice;
	case EINVAL:
		return kIOReturnBadArgument;
	case EAGAIN:
		return kIOReturnBusy;
	case ETIMEDOUT:
		return kIOReturnTimeout;
	case EIO:
		return kIOReturnIOError;
	default:
		return kIOReturnError;
	}
}

bool
KebabBlockStorageDevice::init(OSDictionary *properties)
{
	uint64_t bytes;

	if (!IOBlockStorageDevice::init(properties)) {
		return false;
	}
	fBounce = NULL;
	fLock = NULL;
	fOnline = false;
	fMediaStateReported = false;
	fBlockSize = kebab_ufs_block_size();
	bytes = kebab_ufs_partition_bytes();
	fWritable = kebab_ufs_writable();
	if (fBlockSize == 0 || fBlockSize > KEBAB_BLOCK_CHUNK ||
	    KEBAB_BLOCK_CHUNK % fBlockSize != 0 || bytes == 0 ||
	    (bytes % fBlockSize) != 0) {
		IOLog("KEBAB BLOCK: no usable UFS geometry (%llu bytes, %llu-byte blocks)\n",
		    (unsigned long long)bytes, (unsigned long long)fBlockSize);
		return false;
	}
	fBlockCount = bytes / fBlockSize;
	fBounce = IOMallocAligned(KEBAB_BLOCK_CHUNK, PAGE_SIZE);
	fLock = IOLockAlloc();
	if (fBounce == NULL || fLock == NULL) {
		IOLog("KEBAB BLOCK: no memory for the transfer buffer\n");
		return false;
	}
	return true;
}

bool
KebabBlockStorageDevice::start(IOService *provider)
{
	if (!IOBlockStorageDevice::start(provider)) {
		return false;
	}
	IOLockLock(fLock);
	fOnline = true;
	IOLockUnlock(fLock);
	IOLog("KEBAB BLOCK: block storage up: %llu MiB, %llu-byte blocks, %s\n",
	    (unsigned long long)(fBlockCount * fBlockSize >> 20),
	    (unsigned long long)fBlockSize, fWritable ? "read-write" : "read-only");
	return true;
}

void
KebabBlockStorageDevice::stop(IOService *provider)
{
	/* Wait for the current chunk loop; reject later callers before I/O. */
	IOLockLock(fLock);
	fOnline = false;
	fMediaStateReported = false;
	IOLockUnlock(fLock);
	IOBlockStorageDevice::stop(provider);
}

void
KebabBlockStorageDevice::free(void)
{
	if (fBounce != NULL) {
		IOFreeAligned(fBounce, KEBAB_BLOCK_CHUNK);
		fBounce = NULL;
	}
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOBlockStorageDevice::free();
}

IOReturn
KebabBlockStorageDevice::doEjectMedia(void)
{
	return kIOReturnUnsupported;
}

IOReturn
KebabBlockStorageDevice::doFormatMedia(UInt64 byteCapacity)
{
	(void)byteCapacity;
	return kIOReturnUnsupported;
}

UInt32
KebabBlockStorageDevice::doGetFormatCapacities(UInt64 *capacities,
    UInt32 capacitiesMaxCount) const
{
	(void)capacities;
	(void)capacitiesMaxCount;
	return 0;
}

char *
KebabBlockStorageDevice::getVendorString(void)
{
	return kebabVendorString;
}

char *
KebabBlockStorageDevice::getProductString(void)
{
	return kebabProductString;
}

char *
KebabBlockStorageDevice::getRevisionString(void)
{
	return kebabRevisionString;
}

char *
KebabBlockStorageDevice::getAdditionalDeviceInfoString(void)
{
	return kebabAdditionalString;
}

IOReturn
KebabBlockStorageDevice::reportBlockSize(UInt64 *blockSize)
{
	if (blockSize == NULL) {
		return kIOReturnBadArgument;
	}
	*blockSize = fBlockSize;
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::reportEjectability(bool *isEjectable)
{
	if (isEjectable == NULL) {
		return kIOReturnBadArgument;
	}
	*isEjectable = false;
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::reportMaxValidBlock(UInt64 *maxBlock)
{
	if (maxBlock == NULL) {
		return kIOReturnBadArgument;
	}
	*maxBlock = fBlockCount - 1;
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::reportMediaState(bool *mediaPresent,
    bool *changedState)
{
	if (mediaPresent == NULL) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	*mediaPresent = fOnline;
	if (changedState != NULL) {
		*changedState = !fMediaStateReported;
		fMediaStateReported = true;
	}
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::reportRemovability(bool *isRemovable)
{
	if (isRemovable == NULL) {
		return kIOReturnBadArgument;
	}
	*isRemovable = false;
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::reportWriteProtection(bool *isWriteProtected)
{
	if (isWriteProtected == NULL) {
		return kIOReturnBadArgument;
	}
	*isWriteProtected = !fWritable;
	return kIOReturnSuccess;
}

IOReturn
KebabBlockStorageDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer,
    UInt64 block, UInt64 nblks, IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	IOReturn status = kIOReturnSuccess;
	UInt64 offset, length, done;
	bool write;

	(void)attributes;
	if (buffer == NULL || nblks == 0 || block >= fBlockCount ||
	    nblks > fBlockCount - block) {
		return kIOReturnBadArgument;
	}
	if (buffer->getDirection() == kIODirectionOut) {
		if (!fWritable) {
			return kIOReturnNotWritable;
		}
		write = true;
	} else if (buffer->getDirection() == kIODirectionIn) {
		write = false;
	} else {
		return kIOReturnBadArgument;
	}

	offset = block * fBlockSize;
	length = nblks * fBlockSize;
	if (buffer->getLength() < length) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (!fOnline) {
		IOLockUnlock(fLock);
		return kIOReturnNoDevice;
	}
	for (done = 0; done < length; ) {
		UInt64 left = length - done;
		IOByteCount chunk = (IOByteCount)(left < (UInt64)KEBAB_BLOCK_CHUNK ?
		    left : (UInt64)KEBAB_BLOCK_CHUNK);
		int error;

		if (write) {
			if (buffer->readBytes(done, fBounce, chunk) != chunk) {
				status = kIOReturnUnderrun;
				break;
			}
			error = kebab_ufs_raw_write(offset + done, fBounce,
			    (uint32_t)chunk);
		} else {
			error = kebab_ufs_raw_read(offset + done, fBounce,
			    (uint32_t)chunk);
			if (error == 0 &&
			    buffer->writeBytes(done, fBounce, chunk) != chunk) {
				status = kIOReturnOverrun;
				break;
			}
		}
		if (error != 0) {
			status = statusForError(error);
			break;
		}
		done += chunk;
	}
	IOLockUnlock(fLock);

	/* Accepted requests complete exactly once, outside both bridge/backend
	 * locks. Immediate validation/offline rejection never calls completion.
	 * Execution remains synchronous polling; NULL completion keeps the old
	 * synchronous return convention used by diagnostics. */
	if (completion != NULL && completion->action != NULL) {
		completion->action(completion->target, completion->parameter,
		    status, done);
		return kIOReturnSuccess;
	}
	return status;
}

IOReturn
KebabBlockStorageDevice::doSynchronize(UInt64 block, UInt64 nblks,
    IOStorageSynchronizeOptions options)
{
	(void)block;
	(void)nblks;
	(void)options;
	IOLockLock(fLock);
	IOReturn status = !fOnline ? kIOReturnNoDevice :
	    !fWritable ? kIOReturnSuccess : statusForError(kebab_ufs_sync());
	IOLockUnlock(fLock);
	return status;
}

IOReturn
KebabBlockStorageDevice::doUnmap(IOBlockStorageDeviceExtent *extents,
    UInt32 extentsCount, IOStorageUnmapOptions options)
{
	(void)extents;
	(void)extentsCount;
	(void)options;
	return kIOReturnUnsupported;
}

IOReturn
KebabBlockStorageDevice::requestIdle(void)
{
	/* Idle is a hint; this backend does not implement a hardware power state. */
	IOLockLock(fLock);
	IOReturn status = fOnline ? kIOReturnSuccess : kIOReturnNoDevice;
	IOLockUnlock(fLock);
	return status;
}

/*
 * Called from kebab_ufs_rootdev(), which owns no locks at this point. Returns
 * zero when the service is disabled or published, an errno otherwise; callers
 * ignore the result.
 */
static int
kebab_blockstorage_publish(void)
{
	uint64_t bytes;
	uint64_t block_size;
	IOService *provider;
	KebabBlockStorageDevice *device;

	if (kebab_block_device != NULL) {
		return 0;
	}
	bytes = kebab_ufs_partition_bytes();
	block_size = kebab_ufs_block_size();
	if (bytes == 0 || block_size == 0) {
		IOLog("KEBAB BLOCK: UFS geometry unavailable; no IOMedia\n");
		return ENXIO;
	}
	provider = IOService::getServiceRoot();
	if (provider == NULL) {
		IOLog("KEBAB BLOCK: no IOKit service root; no IOMedia\n");
		return ENXIO;
	}

	device = new KebabBlockStorageDevice;
	if (device == NULL) {
		IOLog("KEBAB BLOCK: cannot allocate the block device\n");
		return ENOMEM;
	}
	if (!device->init(NULL)) {
		device->release();
		return ENXIO;
	}
	device->setName("kebab-ufs0");
	/* The unchanged IOBlockStorageDriver personality requires this exact
	 * property in addition to the IOBlockStorageDevice superclass. */
	device->setProperty(kIOBlockStorageDeviceTypeKey, kIOBlockStorageDeviceTypeGeneric);
	if (!device->attach(provider)) {
		IOLog("KEBAB BLOCK: cannot attach to the platform expert\n");
		device->release();
		return ENXIO;
	}
	if (!device->start(provider)) {
		device->detach(provider);
		device->release();
		IOLog("KEBAB BLOCK: block device start failed\n");
		return ENXIO;
	}
	/*
	 * Deliberately not released: this is a singleton owned by the registry
	 * for the life of the boot. IOStorageFamily's generic IOBlockStorageDriver
	 * matches the service and publishes the IOMedia from our geometry and
	 * product string ("UFS userdata" -> "UFS userdata Media").
	 */
	kebab_block_device = device;
	device->registerService();
	IOLog("KEBAB BLOCK: registered Generic device for %llu MiB, %llu-byte blocks, %s; "
	    "IOMedia child not yet verified\n",
	    (unsigned long long)(bytes >> 20), (unsigned long long)block_size,
	    kebab_ufs_writable() ? "read-write" : "read-only");
	return 0;
}


extern "C" int
kebab_blockstorage_start(void)
{
	uint32_t enabled = 0;
	if (!PE_parse_boot_argn("ufsmedia", &enabled, sizeof(enabled)) || !enabled) {
		return 0;
	}
	lck_mtx_lock(&kebab_block_publish_lock);
	int error = kebab_blockstorage_publish();
	lck_mtx_unlock(&kebab_block_publish_lock);
	return error;
}

/* Called only for ufsmedia_root=1, after the UFS geometry is available.
 * Select the actual whole-partition IOMedia below our own bridge, never an
 * unrelated disk, and preserve its native BSD identity and root service. */
extern "C" int
kebab_blockstorage_rootdev(dev_t *dev, char *name, size_t name_size, IOService **service)
{
	if (kebab_block_device == NULL) {
		return ENXIO;
	}
	IOReturn quiet = kebab_block_device->waitQuiet(5ULL * kSecondScale);
	if (quiet != kIOReturnSuccess) {
		IOLog("KEBAB BLOCK: matching did not quiesce: 0x%x\n", quiet);
		return ETIMEDOUT;
	}
	IORegistryIterator *iterator = IORegistryIterator::iterateOver(kebab_block_device,
	    gIOServicePlane, kIORegistryIterateRecursively);
	if (iterator == NULL) {
		return ENOMEM;
	}
	int result = ENXIO;
	IORegistryEntry *entry;
	unsigned seen = 0;
	while ((entry = iterator->getNextObject()) != NULL) {
		if (++seen <= 12) {
			IOLog("KEBAB BLOCK: child %s (%s)\n", entry->getName(), entry->getMetaClass()->getClassName());
		}
		if (entry->metaCast("IOMedia") == NULL) {
			continue;
		}
		OSBoolean *whole = OSDynamicCast(OSBoolean, entry->getProperty("Whole"));
		OSNumber *size = OSDynamicCast(OSNumber, entry->getProperty("Size"));
		OSString *bsd_name = OSDynamicCast(OSString, entry->getProperty(kIOBSDNameKey));
		OSNumber *bsd_major = OSDynamicCast(OSNumber, entry->getProperty(kIOBSDMajorKey));
		OSNumber *bsd_minor = OSDynamicCast(OSNumber, entry->getProperty(kIOBSDMinorKey));
		IOService *media = OSDynamicCast(IOService, entry);
		if (whole == NULL || !whole->isTrue() || size == NULL ||
		    size->unsigned64BitValue() != kebab_ufs_partition_bytes() ||
		    bsd_name == NULL || bsd_major == NULL || bsd_minor == NULL || media == NULL) {
			IOLog("KEBAB BLOCK: IOMedia lacks whole-partition BSD metadata\n");
			continue;
		}
		uint32_t maj = bsd_major->unsigned32BitValue();
		uint32_t min = bsd_minor->unsigned32BitValue();
		dev_t media_dev = makedev(maj, min);
		if (major(media_dev) != maj || minor(media_dev) != min ||
		    bsd_name->getLength() == 0 || bsd_name->getLength() >= name_size) {
			result = EINVAL;
			continue;
		}
		strlcpy(name, bsd_name->getCStringNoCopy(), name_size);
		*dev = media_dev;
		media->retain();
		*service = media;
		IOLog("KEBAB BLOCK: verified IOMedia root %s, major %u minor %u, %llu bytes\n",
		    name, maj, min, (unsigned long long)size->unsigned64BitValue());
		result = 0;
		break;
	}
	iterator->release();
	return result;
}

/*
 * The iOS 16.7.16 IOBlockStorageDevice has doSynchronizeCache() at vtable slot
 * 171.  The base implementation there returns kIOReturnUnsupported, so keep the
 * same answer: the BSD driver below owns cache flushing through its own path.
 */
IOReturn
KebabBlockStorageDevice::doSynchronizeCache()
{
	return kIOReturnUnsupported;
}

struct KebabBlockTestCompletion {
	unsigned calls;
	IOReturn status;
	UInt64 bytes;
	bool mediaPresent;
};

static void
kebab_block_test_completed(void *target, void *, IOReturn status, UInt64 bytes)
{
	KebabBlockTestCompletion *result = (KebabBlockTestCompletion *)target;
	result->calls++;
	result->status = status;
	result->bytes = bytes;
	/* Reentry also checks that the bridge lock is not held at completion. */
	(void)kebab_block_device->reportMediaState(&result->mediaPresent);
}

extern "C" bool
kebab_blockstorage_selftest(void)
{
	KebabBlockStorageDevice *device = kebab_block_device;
	UInt64 blockSize = 0, last = 0;
	bool protectedMedia = false;
	if (device == NULL || device->reportBlockSize(&blockSize) != kIOReturnSuccess ||
	    blockSize != 4096 || device->reportMaxValidBlock(&last) != kIOReturnSuccess ||
	    device->reportWriteProtection(&protectedMedia) != kIOReturnSuccess ||
	    !protectedMedia) {
		IOLog("KEBAB DRIVER TEST: UFS requires online 4K read-only media\n");
		return false;
	}
	IOBufferMemoryDescriptor *data = IOBufferMemoryDescriptor::withOptions(
	    kIODirectionIn, (vm_size_t)blockSize, PAGE_SIZE);
	if (data == NULL) {
		return false;
	}
	KebabBlockTestCompletion result = {};
	IOStorageCompletion completion = { &result, kebab_block_test_completed, NULL };
	IOReturn read = device->doAsyncReadWrite(data, 0, 1, NULL, &completion);
	uint32_t magic = 0;
	data->readBytes(32, &magic, sizeof(magic));
	bool passed = read == kIOReturnSuccess && result.calls == 1 &&
	    result.status == kIOReturnSuccess && result.bytes == blockSize &&
	    result.mediaPresent && magic == 0x4253584e; /* NXSB, read only */
	data->setLength((vm_size_t)blockSize - 1);
	IOReturn shortBuffer = device->doAsyncReadWrite(data, 0, 1, NULL, &completion);
	data->setLength((vm_size_t)blockSize);
	IOReturn bounds = device->doAsyncReadWrite(data, last, 2, NULL, &completion);
	data->setDirection(kIODirectionOut);
	IOReturn write = device->doAsyncReadWrite(data, 0, 1, NULL, &completion);
	passed &= shortBuffer == kIOReturnBadArgument && bounds == kIOReturnBadArgument &&
	    write == kIOReturnNotWritable && result.calls == 1;
	data->release();
	IOLog("KEBAB DRIVER TEST: UFS NXSB/completion/reentry/short/bounds/RO %s "
	    "(0x%x/0x%x/0x%x/0x%x, calls=%u)\n", passed ? "PASS" : "FAIL",
	    read, shortBuffer, bounds, write, result.calls);
	return passed;
}

#endif /* KEBAB */
