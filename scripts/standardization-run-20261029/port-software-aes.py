"""Target-specific integration of the pinned SoftwareAES component."""
from pathlib import Path
import json,subprocess,struct,sys
r=Path(__file__).parent;d=r/'software-aes';d.mkdir(exist_ok=True)
files=['iokit/conf/files.arm64','iokit/KernelConfigTables.cpp']
got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(files),text=True,capture_output=True,check=True).stdout)
for p,value in got.items():
    for folder in ['before','after']:
        f=d/folder/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(value['source'],newline='\n')
cpp=(r/'NativeAES-upstream.cpp').read_text()
cpp=cpp.replace('#include <kern/task.h>','#include <kern/task.h>\n#include <pexpert/pexpert.h>')
cpp=cpp.replace('static bool knownAnswerTest(NativeAES *provider)','static bool knownAnswerTest(NativeAES *provider, bool negative)')
cpp=cpp.replace('const unsigned char expected[64] = {','unsigned char expected[64] = {',1)
needle='  IOAESKeyData key = '
assert needle in cpp
cpp=cpp.replace(needle,'  if (negative) expected[0] ^= 1; // Negative KAT never changes crypto or external inputs.\n'+needle,1)
needle='  native_aes_kat_status = knownAnswerTest(this) ? 1 : 2;\n  if (native_aes_kat_status != 1) return false;'
assert needle in cpp
cpp=cpp.replace(needle,'''  // The native class is only a subclass of this target's original base.
  const OSMetaClass *aes = getMetaClass()->getSuperClass();
  const OSMetaClass *crypto = aes ? aes->getSuperClass() : nullptr;
  const OSMetaClass *service = crypto ? crypto->getSuperClass() : nullptr;
  IOLog("KEBAB AES: ABI aes=%p/%u crypto=%p/%u service=%p/%u\\n",
        aes, aes ? aes->getClassSize() : 0, crypto, crypto ? crypto->getClassSize() : 0,
        service, service ? service->getClassSize() : 0);
  if (!aes || aes->getClassSize() != 0x120 || strcmp(aes->getClassName(), "IOAESAccelerator") ||
      !crypto || crypto->getClassSize() != 0x88 || strcmp(crypto->getClassName(), "IOCryptoAccelerator") ||
      !service || service != IOService::metaClass || service->getClassSize() != 0x88) return false;
  uint32_t negative = 0;
  PE_parse_boot_argn("kebab_aes_negative_kat", &negative, sizeof(negative));
  native_aes_kat_status = knownAnswerTest(this, negative == 1) ? 1 : 2;
  IOLog("KEBAB AES: KAT=%u interface=%u completed=%u unsupported=%u negative=%u\\n",
        native_aes_kat_status, native_aes_interface_status, native_aes_completed,
        native_aes_unsupported_key, negative == 1);
  if (native_aes_kat_status != 1) return false;''',1)
cpp=cpp.replace('  registerService();\n  return true;','  registerService();\n  IOLog("KEBAB AES: NativeAES registered; software raw keys only\\n");\n  return true;',1)
target=d/'after/iokit/Kernel/arm';target.mkdir(parents=True,exist_ok=True)
(target/'NativeAES.cpp').write_text(cpp,newline='\n')
(target/'OriginalAESABI.h').write_text((r/'OriginalAESABI-upstream.h').read_text(),newline='\n')
f=d/'after/iokit/conf/files.arm64';s=f.read_text();assert 'NativeAES.cpp' not in s
s=s.replace('iokit/Kernel/arm/KebabPrelinkBindings.cpp\toptional iokitcpp','iokit/Kernel/arm/KebabPrelinkBindings.cpp\toptional iokitcpp\niokit/Kernel/arm/NativeAES.cpp\toptional iokitcpp\niokit/Kernel/arm/OriginalAESBindings.cpp\toptional iokitcpp');f.write_text(s,newline='\n')
f=d/'after/iokit/KernelConfigTables.cpp';s=f.read_text();needle='    "     \'IOClass\' = KebabGeniBus;"';assert needle in s
s=s.replace(needle,'''    "     'IOClass' = NativeAES;"
    "     'IOProviderClass' = IOResources;"
    "     'IOResourceMatch' = IOBSD;"
    "     'IOProbeScore' = 1000:32;"
    "   },"
    "   {"
'''+needle,1);f.write_text(s,newline='\n')
# Binding names are the upstream declaration; values are measured 20H392 slots.
sys.path.insert(0,str(r.parent/'p1-run/prelink-fix'))
from native_prelink import macho,backed_offset
raw=(r.parent/'p1-run/prelink-fix/prelink-0207.bin').read_bytes();image=macho(raw)
def pointer(va):
    at=backed_offset(image['segments'],va,8,len(raw));return struct.unpack_from('<Q',raw,at)[0]
slots={0:'D1Ev',1:'D0Ev',7:'12getMetaClassEv',86:'5startEP9IOService',111:'11getWorkLoopEv',117:'20callPlatformFunctionEPK8OSSymbolbPvS3_S3_S3_',
168:'16getAlgorithmNameEv',169:'10performAESEPvS0_P4taskyP7IOAESIV14IOAESOperationP12IOAESKeyDataPFvS0_iES0_',
170:'10performAESEP18IOMemoryDescriptorS1_yP7IOAESIV14IOAESOperationP12IOAESKeyDatayyPFvPviES7_',
171:'14isKeySupportedEj',172:'19getSupportedOptionsEv',173:'17performAESQuantumEP23IOAESAcceleratorCommand',174:'18completeAESQuantumEP23IOAESAcceleratorCommand',
175:'19performImmediateAESEP23IOAESAcceleratorCommand',176:'18disableHardwareKeyEj',177:'17createSpecialKeysEv',178:'12_queueLockedEP23IOAESAcceleratorCommand',179:'13_startCommandEv',180:'12_completeAESEP23IOAESAcceleratorCommand'}
bindings={('__ZNK' if i in (7,111) else '__ZN')+'16IOAESAccelerator'+name:pointer(0xfffffe0009ee09f8+16+i*8) for i,name in slots.items()}
bindings.update({'__ZN16IOAESAcceleratorC2EPK11OSMetaClass':0xfffffe0008911038,'__ZN16IOAESAcceleratorD2Ev':0xfffffe0008911078,'__ZN16IOAESAccelerator10gMetaClassE':0xfffffe000ac49050})
(target/'OriginalAESBindings.cpp').write_text('/* Pinned 20H392 original crypto-family bindings; see IOAESAccelerator.json. */\n'+''.join('__asm__(".globl '+name+'\\n.set '+name+', '+hex(va)+'");\n' for name,va in sorted(bindings.items())),newline='\n')
(d/'bindings-20H392.json').write_text(json.dumps({k:hex(v) for k,v in bindings.items()},indent=2)+'\n')
print('Prepared SoftwareAES target integration; bindings',len(bindings))
