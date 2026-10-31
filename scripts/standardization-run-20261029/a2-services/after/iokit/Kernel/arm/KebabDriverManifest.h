/* Names describe the native implementation, not private Apple ABI coverage.
 * A NULL service means the backend is a resident C core with no IOKit identity.
 * DRV? independently queries the registry for every non-NULL class. */
#define KEBAB_DRIVER_MANIFEST(X) \
 X("platform", "KebabPlatformExpert", "ADT platform/resources") \
 X("cpu", "KebabCPU", "single boot CPU; no SMP qualification") \
 X("ufs", "KebabBlockStorageDevice", "boot C UFS + native storage bridge") \
 X("usb", NULL, "resident DWC3 C core; no normal IOService") \
 X("spmi", "KebabSPMIBus", "ADT arbiter + boot C backend") \
 X("pon", "KebabPon", "scoped PON; panic uses C core directly") \
 X("keys", "KebabPmicKeys", "raw PMIC sampling; no HID") \
 X("rpmh", "KebabVrm", "RSC policy/readback; no IOPM") \
 X("geni", "KebabGeniBus", "SE13/bitbang transport; boot C mappings") \
 X("touch", "KebabTouch", "typed TCM state; C polling lifetime; no HID") \
 X("display", "AppleCLCD", "CPU framebuffer; no GPU/vsync qualification") \
 X("surface", "IOCoreSurfaceRoot", "compatibility selectors; private ABI incomplete")
