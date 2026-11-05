/* SPDX-License-Identifier: MIT */
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include "KebabBacklightNits.h"
class KebabBacklightControl : public IOService {
 OSDeclareDefaultStructors(KebabBacklightControl);
public:
 IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(KebabBacklightControl,IOService);
static IOService *kebab_backlight_service;
static uint32_t kebab_backlight_published,kebab_backlight_service_requests,kebab_backlight_service_error,kebab_backlight_last_nits;
static bool kebab_backlight_parameter(OSDictionary *parameters,const char *key,uint32_t maximum,uint32_t actual)
{
 OSDictionary *range=OSDictionary::withCapacity(3);OSNumber *minimum=OSNumber::withNumber(0ULL,32),*max=OSNumber::withNumber((uint64_t)maximum,32),*value=OSNumber::withNumber((uint64_t)actual,32);
 bool ok=range&&minimum&&max&&value;
 if(ok)ok=range->setObject("min",minimum)&&range->setObject("max",max)&&range->setObject("value",value)&&parameters->setObject(key,range);
 if(value)value->release();if(max)max->release();if(minimum)minimum->release();if(range)range->release();return ok;
}
static bool kebab_backlight_service_update_locked(uint32_t level)
{
 if(!kebab_backlight_service)return true;
 if(level>2047)return false;
 OSDictionary *parameters=OSDictionary::withCapacity(2);if(!parameters)return false;
 bool ok=kebab_backlight_parameter(parameters,"brightness",2047,level)&&kebab_backlight_parameter(parameters,"BrightnessMilliNits",540000,kebab_backlight_level_millinits(level));
 if(ok)ok=kebab_backlight_service->setProperty("IODisplayParameters",parameters);
 parameters->release();return ok;
}
static void kebab_native_backlight_bind(IOService *service)
{
 if(kebab_backlight_service||!service)return;
 KebabBacklightControl *backlight=new KebabBacklightControl;
 if(!backlight){IOLog("KEBAB BACKLIGHT: child allocation failed\n");return;}
 if(!backlight->init()){backlight->release();IOLog("KEBAB BACKLIGHT: child init failed\n");return;}
 if(!backlight->attach(service)){backlight->release();IOLog("KEBAB BACKLIGHT: child attach failed\n");return;}
 if(!backlight->start(service)){backlight->detach(service);backlight->release();IOLog("KEBAB BACKLIGHT: child start failed\n");return;}
 backlight->setName("KebabBacklightControl");kebab_backlight_service=backlight;
 /* A fresh child remains unregistered until the panel has passed its real
  * command/readback qualification. Re-registering an already matched FB
  * cannot deliver a new IOServiceFirstMatch notification (IOService.cpp). */
}
static void kebab_backlight_service_ready(void)
{
 if(!kebab_backlight_service||!kebab_dsi_lock)return;
 bool publish=false;IOLockLock(kebab_dsi_lock);
 if(kebab_brightness_ready&&!kebab_backlight_published&&kebab_backlight_service_update_locked(kebab_brightness_applied)){
  bool built=kebab_backlight_service->setProperty("built-in",true);
  bool control=built&&kebab_backlight_service->setProperty("backlight-control",true);
  if(control){kebab_backlight_published=1;publish=true;}
 }
 IOLockUnlock(kebab_dsi_lock);
 if(publish){kebab_backlight_service->registerService();IOLog("KEBAB BACKLIGHT: native backlight-control published after real DCS qualification; nominal540nits/2047levels, linear reference profile\n");}
}
static bool kebab_backlight_service_handles(OSObject *properties)
{
 OSDictionary *dict=OSDynamicCast(OSDictionary,properties);
 return dict&&(dict->getObject("brightness-nits")||dict->getObject("brightness"));
}
static void kebab_backlight_service_status_locked(uint32_t out[3])
{out[0]=kebab_backlight_service_requests;out[1]=kebab_backlight_last_nits;out[2]=kebab_backlight_service_error;}
static IOReturn kebab_backlight_service_set_properties(IOService *service,OSObject *properties)
{
 OSDictionary *dict=OSDynamicCast(OSDictionary,properties);if(!dict||dict->getCount()!=1)return kIOReturnBadArgument;
 bool nits=dict->getObject("brightness-nits")!=NULL;OSNumber *number=OSDynamicCast(OSNumber,dict->getObject(nits?"brightness-nits":"brightness"));
 if(!number)return kIOReturnBadArgument;
 uint64_t input=number->unsigned64BitValue();uint32_t level=0;
 if(nits){if(kebab_backlight_nits_q16_level(input,&level))return kIOReturnBadArgument;}
 else {if(input>2047)return kIOReturnBadArgument;level=(uint32_t)input;}
 if(!service||service!=kebab_backlight_service||service->isInactive()||!kebab_dsi_lock)return kIOReturnNoDevice;
 IOLockLock(kebab_dsi_lock);int error=95;
 if(kebab_brightness_ready&&!panic_active()){
  kebab_backlight_service_requests++;kebab_backlight_last_nits=nits?(uint32_t)input:(uint32_t)(((uint64_t)level*KEBAB_BACKLIGHT_NITS_Q16_MAX)/2047);
  error=kebab_native_backlight_apply_locked(level);
 }
 kebab_backlight_service_error=(uint32_t)error;IOLockUnlock(kebab_dsi_lock);
 IOLog("KEBAB BACKLIGHT: service input=%llu unit=%s target=%u error=%u; actual DCS51/52 before success\n",input,nits?"nits-Q16.16":"device-level",level,(unsigned)error);
 return !error?kIOReturnSuccess:error==95?kIOReturnNotReady:error==12?kIOReturnNoMemory:kIOReturnIOError;
}
IOReturn KebabBacklightControl::setProperties(OSObject *properties)
{
 if(kebab_backlight_service_handles(properties))return kebab_backlight_service_set_properties(this,properties);
 return IOService::setProperties(properties);
}
