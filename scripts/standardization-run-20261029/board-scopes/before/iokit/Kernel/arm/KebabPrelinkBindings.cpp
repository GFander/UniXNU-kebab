/*
 * Link-time bindings for the IOStorageFamily symbols the kernel-side block
 * storage driver needs.  These live in the prelinked IOStorageFamily kext, so a
 * static kernel cannot resolve them; each is bound to its address in the
 * prelinked image (donor address plus the fixed slide).  The addresses come from
 * scripts/recover-kext-symbols.py, recorded in docs/BRING-UP-STATE.md section 93.
 */

__asm__(".globl __ZN20IOBlockStorageDevice10gMetaClassE\n.set __ZN20IOBlockStorageDevice10gMetaClassE, 0xfffffe000ac411d0");
__asm__(".globl __ZN20IOBlockStorageDevice13doSetPriorityEP26IOBlockStorageDeviceExtentjh\n.set __ZN20IOBlockStorageDevice13doSetPriorityEP26IOBlockStorageDeviceExtentjh, 0xfffffe0008812068");
__asm__(".globl __ZN20IOBlockStorageDevice13setPropertiesEP8OSObject\n.set __ZN20IOBlockStorageDevice13setPropertiesEP8OSObject, 0xfffffe0008811f38");
__asm__(".globl __ZN20IOBlockStorageDevice18getWriteCacheStateEPb\n.set __ZN20IOBlockStorageDevice18getWriteCacheStateEPb, 0xfffffe0008812038");
__asm__(".globl __ZN20IOBlockStorageDevice18setWriteCacheStateEb\n.set __ZN20IOBlockStorageDevice18setWriteCacheStateEb, 0xfffffe0008812044");
__asm__(".globl __ZN20IOBlockStorageDevice20doGetProvisionStatusEyyPjP35IOBlockStorageProvisionDeviceExtenty\n.set __ZN20IOBlockStorageDevice20doGetProvisionStatusEyyPjP35IOBlockStorageProvisionDeviceExtenty, 0xfffffe000881208c");
__asm__(".globl __ZN20IOBlockStorageDeviceC2EPK11OSMetaClass\n.set __ZN20IOBlockStorageDeviceC2EPK11OSMetaClass, 0xfffffe0008811d98");
__asm__(".globl __ZN20IOBlockStorageDeviceD2Ev\n.set __ZN20IOBlockStorageDeviceD2Ev, 0xfffffe0008811db8");
__asm__(".globl __ZNK20IOBlockStorageDevice11getPropertyEPK8OSSymbol\n.set __ZNK20IOBlockStorageDevice11getPropertyEPK8OSSymbol, 0xfffffe0008811e88");
__asm__(".globl __ZTV20IOBlockStorageDevice\n.set __ZTV20IOBlockStorageDevice, 0xfffffe0009eabf50");
