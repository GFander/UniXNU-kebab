/* Registry properties only; source IOService ABI, no private IOMedia fields.
 * The iterator owns matched services. Every ancestor copy is released. */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <sys/errno.h>
#include "../../../bsd/kern/kebab_data.h"

static uint64_t
kebab_backing_id(IORegistryEntry *entry)
{
 uint64_t result = 0;
 entry->retain();
 for (unsigned depth = 0; entry && depth < 16; depth++) {
  if (entry->metaCast("KebabBlockStorageDevice")) {
   result = entry->getRegistryEntryID();
   break;
  }
  IORegistryEntry *parent = entry->copyParentEntry(gIOServicePlane);
  entry->release();
  entry = parent;
 }
 OSSafeReleaseNULL(entry);
 return result;
}

static int
kebab_lookup_uuid(const char *uuid, struct kebab_volume_identity *out)
{
 bzero(out, sizeof(*out));
 OSDictionary *matching = IOService::serviceMatching("IOMedia");
 OSDictionary *properties = OSDictionary::withCapacity(1);
 OSString *value = OSString::withCString(uuid);
 bool ready = matching && properties && value && properties->setObject("UUID", value) &&
     matching->setObject("IOPropertyMatch", properties);
 OSSafeReleaseNULL(value);
 OSSafeReleaseNULL(properties);
 if (!ready) { OSSafeReleaseNULL(matching); return ENOMEM; }
 OSIterator *iterator = IOService::getMatchingServices(matching);
 matching->release();
 if (!iterator) return ENOENT;
 unsigned count = 0;
 int error = ENOENT;
 while (count < 2) {
  IOService *service = OSDynamicCast(IOService, iterator->getNextObject());
  if (!service) break;
  count++;
  OSString *name = OSDynamicCast(OSString, service->getProperty("BSD Name"));
  OSNumber *major = OSDynamicCast(OSNumber, service->getProperty("BSD Major"));
  OSNumber *minor = OSDynamicCast(OSNumber, service->getProperty("BSD Minor"));
  uint64_t backing = kebab_backing_id(service);
  if (service->isInactive() || !name || !major || !minor || !backing ||
      major->unsigned64BitValue() > 255 || minor->unsigned64BitValue() > 0xffffff ||
      name->getLength() == 0 || name->getLength() >= sizeof(out->bsd_name)) { error = EINVAL; break; }
  const char *text = name->getCStringNoCopy();
  bool valid = true;
  for (unsigned i = 0; i < name->getLength(); i++) {
   char c = text[i];
   if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) valid = false;
  }
  if (!valid) { error = EINVAL; break; }
  out->major_number = major->unsigned32BitValue();
  out->minor_number = minor->unsigned32BitValue();
  out->backing_id = backing;
  strlcpy(out->bsd_name, text, sizeof(out->bsd_name));
  error = 0;
 }
 if (!iterator->isValid() || count != 1) error = EINVAL;
 iterator->release();
 if (error) bzero(out, sizeof(*out));
 return error;
}

extern "C" int
kebab_lookup_apfs_data(struct kebab_volume_identity *system, struct kebab_volume_identity *data)
{
 if (!system || !data) return EINVAL;
 int error = kebab_lookup_uuid("DADD93F8-DBBF-4B17-A1DF-0F11E64172F7", system);
 if (!error) error = kebab_lookup_uuid("4C56203C-CFE8-4DFF-BF8E-BB48D852C936", data);
 if (!error && (system->backing_id != data->backing_id ||
     (system->major_number == data->major_number && system->minor_number == data->minor_number))) error = EINVAL;
 return error;
}
#endif
