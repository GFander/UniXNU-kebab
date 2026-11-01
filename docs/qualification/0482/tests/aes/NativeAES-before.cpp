#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "OriginalAESABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/crypto/aes.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextern-c-compat"
#include <libkern/crypto/crypto_internal.h>
#pragma clang diagnostic pop
#include <corecrypto/cc.h>
#include <kern/task.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include "../../../bsd/kern/kebab_data_setup_policy.h"

extern "C" {
volatile uint32_t native_aes_kat_status = 0;
volatile uint32_t native_aes_registered = 0;
volatile uint32_t native_aes_interface_status = 0;
volatile uint32_t native_aes_completed = 0;
volatile uint32_t native_aes_unsupported_key = 0;
}

// One board provider, one immutable per-boot nonce key. No Apple UID claim.
static unsigned char native_nonce_key[16] __attribute__((aligned(64)));
static unsigned native_nonce_state; // 0 unavailable, 1 prepared, 2 published
static bool native_nonce_selftest;

class NativeAES : public IOAESAccelerator {
  OSDeclareDefaultStructors(NativeAES)
public:
  bool start(IOService *) override;
  IOWorkLoop *getWorkLoop() const override { return IOService::getWorkLoop(); }
  IOReturn callPlatformFunction(const OSSymbol *name, bool wait, void *a, void *b, void *c, void *d) override {
    return IOService::callPlatformFunction(name, wait, a, b, c, d);
  }
  const char *getAlgorithmName() override { return "AES-CBC"; }
  bool isKeySupported(unsigned selector) override { return selector == 0 || (selector == 2212 && __atomic_load_n(&native_nonce_state, __ATOMIC_ACQUIRE) == 2); }
  unsigned getSupportedOptions() override { return 1; }
  IOReturn performAES(void *, void *, task_t, unsigned long long, IOAESIV *,
                     IOAESOperation, IOAESKeyData *, IOAESCompletion, void *) override;
  IOReturn performAES(IOMemoryDescriptor *, IOMemoryDescriptor *, unsigned long long,
                     IOAESIV *, IOAESOperation, IOAESKeyData *, unsigned long long,
                     unsigned long long, IOAESCompletion, void *) override;
};
OSDefineMetaClassAndStructors(NativeAES, IOAESAccelerator)
static NativeAES *native_nonce_provider; // retained until reset, including failure paths


// No padding, hidden device keys, or change to the registered corecrypto modes.
static int cbc(const unsigned char *input, unsigned char *output, size_t bytes,
               unsigned char iv[16], const IOAESKeyData &key, IOAESOperation operation)
{
  union { aes_encrypt_ctx encrypt; aes_decrypt_ctx decrypt; } context;
  unsigned char nextIV[16];
  int result;
  if (operation == IOAESDecrypt) {
    memcpy(nextIV, input + bytes - 16, 16);
    result = aes_decrypt_key(key.bytes, key.bits / 8, &context.decrypt);
    if (!result) result = aes_decrypt_cbc(input, iv, static_cast<unsigned>(bytes / 16), output, &context.decrypt);
  } else {
    result = aes_encrypt_key(key.bytes, key.bits / 8, &context.encrypt);
    if (!result) result = aes_encrypt_cbc(input, iv, static_cast<unsigned>(bytes / 16), output, &context.encrypt);
    if (!result) memcpy(nextIV, output + bytes - 16, 16);
  }
  if (!result) memcpy(iv, nextIV, 16);
  cc_clear(sizeof(context), &context);
  cc_clear(sizeof(nextIV), nextIV);
  return result;
}

// NIST SP800-38A F.2.1/F.2.2, four CBC blocks, then split/in-place decryption.
static bool knownAnswerTest(NativeAES *provider, bool negative)
{
  const unsigned char input[64] = {
    0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
    0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51,
    0x30,0xc8,0x1c,0x46,0xa3,0x5c,0xe4,0x11,0xe5,0xfb,0xc1,0x19,0x1a,0x0a,0x52,0xef,
    0xf6,0x9f,0x24,0x45,0xdf,0x4f,0x9b,0x17,0xad,0x2b,0x41,0x7b,0xe6,0x6c,0x37,0x10};
  unsigned char expected[64] = {
    0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
    0x50,0x86,0xcb,0x9b,0x50,0x72,0x19,0xee,0x95,0xdb,0x11,0x3a,0x91,0x76,0x78,0xb2,
    0x73,0xbe,0xd6,0xb8,0xe3,0xc1,0x74,0x3b,0x71,0x16,0xe6,0x9e,0x22,0x22,0x95,0x16,
    0x3f,0xf1,0xca,0xa1,0x68,0x1f,0xac,0x09,0x12,0x0e,0xca,0x30,0x75,0x86,0xe1,0xa7};
  if (negative) expected[0] ^= 1; // Negative KAT never changes crypto or external inputs.
  IOAESKeyData key = {128, {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
                           0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c}, 0};
  unsigned char iv[16], output[64];
  for (unsigned i = 0; i < 16; ++i) iv[i] = static_cast<unsigned char>(i);
  bool result = !cbc(input, output, 64, iv, key, IOAESEncrypt) && !memcmp(output, expected, 64) &&
                !memcmp(iv, expected + 48, 16);
  for (unsigned i = 0; i < 16; ++i) iv[i] = static_cast<unsigned char>(i);
  result = result && !cbc(output, output, 16, iv, key, IOAESDecrypt) &&
           !cbc(output + 16, output + 16, 48, iv, key, IOAESDecrypt) && !memcmp(output, input, 64) &&
           !memcmp(iv, expected + 48, 16);
  const IOAESKeyData wideKeys[2] = {
    {192, {0x8e,0x73,0xb0,0xf7,0xda,0x0e,0x64,0x52,0xc8,0x10,0xf3,0x2b,0x80,0x90,0x79,0xe5,
           0x62,0xf8,0xea,0xd2,0x52,0x2c,0x6b,0x7b}, 0},
    {256, {0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
           0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4}, 0}};
  const unsigned char wideExpected[2][16] = {
    {0x4f,0x02,0x1d,0xb2,0x43,0xbc,0x63,0x3d,0x71,0x78,0x18,0x3a,0x9f,0xa0,0x71,0xe8},
    {0xf5,0x8c,0x4c,0x04,0xd6,0xe5,0xf1,0xba,0x77,0x9e,0xab,0xfb,0x5f,0x7b,0xfb,0xd6}};
  for (unsigned n = 0; n < 2; ++n) {
    for (unsigned i = 0; i < 16; ++i) iv[i] = static_cast<unsigned char>(i);
    result = result && !cbc(input, output, 16, iv, wideKeys[n], IOAESEncrypt) && !memcmp(output, wideExpected[n], 16);
    for (unsigned i = 0; i < 16; ++i) iv[i] = static_cast<unsigned char>(i);
    result = result && !cbc(output, output, 16, iv, wideKeys[n], IOAESDecrypt) && !memcmp(output, input, 16);
  }
  IOBufferMemoryDescriptor *source = IOBufferMemoryDescriptor::withCapacity(128, kIODirectionInOut);
  IOBufferMemoryDescriptor *destination = IOBufferMemoryDescriptor::withCapacity(128, kIODirectionInOut);
  IOAESIV publicIV;
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  struct Completion { unsigned count; int status; } completion = {};
  auto callback = [](void *argument, int status) {
    Completion *state = static_cast<Completion *>(argument);
    state->count++;
    state->status = status;
  };
  bool interface = source && destination && source->writeBytes(16, input, 64) == 64 &&
    !provider->performAES(source, destination, 64, &publicIV, IOAESEncrypt, &key, 16, 32, callback, &completion) &&
    destination->readBytes(32, output, 64) == 64 && !memcmp(output, expected, 64) &&
    !memcmp(publicIV.bytes, expected + 48, 16) && completion.count == 1 && completion.status == 0;
  IOLog("KEBAB AES KAT: descriptors=%u completed=%u\n", interface, native_aes_completed);
  key.selector = 2001; // Original hardware-key selector, deliberately unsupported.
  interface = interface && provider->performAES(source, destination, 64, &publicIV,
    IOAESEncrypt, &key, 16, 32, callback, &completion) == kIOReturnUnsupported && completion.count == 1 &&
    destination->readBytes(32, output, 64) == 64 && !memcmp(output, expected, 64) &&
    !memcmp(publicIV.bytes, expected + 48, 16) && !provider->isKeySupported(key.selector);
  IOLog("KEBAB AES KAT: unsupported=%u count=%u\n", interface, completion.count);
  key.selector = 0;
  interface = interface && provider->performAES(source, destination, 64, &publicIV,
    IOAESEncrypt, &key, 16, 80, callback, &completion) == kIOReturnBadArgument && completion.count == 1;
  IOLog("KEBAB AES KAT: bounds=%u count=%u\n", interface, completion.count);
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  extern vm_offset_t vm_kernel_stext, vm_kext_base, static_memory_end;
  IOLog("KEBAB AES KAT: input=%p native-start=0x%lx prelink-start=0x%lx static-end=0x%lx\n",
        input, vm_kernel_stext, vm_kext_base, static_memory_end);
  IOReturn rawStatus = kIOReturnNotReady;
  if (interface) rawStatus = provider->performAES(const_cast<unsigned char *>(input), output, kernel_task,
    64, &publicIV, IOAESEncrypt, &key, nullptr, nullptr);
  interface = interface && !rawStatus && !memcmp(output, expected, 64);
  IOLog("KEBAB AES KAT: raw-encrypt=%u status=0x%x first=%02x%02x%02x%02x iv=%02x completed=%u\n",
        interface, rawStatus, output[0], output[1], output[2], output[3], publicIV.bytes[0], native_aes_completed);
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  rawStatus = kIOReturnNotReady;
  if (interface) rawStatus = provider->performAES(output, output, kernel_task, 64, &publicIV,
    IOAESDecrypt, &key, nullptr, nullptr);
  interface = interface && !rawStatus && !memcmp(output, input, 64) && !memcmp(publicIV.bytes, expected + 48, 16);
  IOLog("KEBAB AES KAT: raw-decrypt=%u status=0x%x first=%02x%02x%02x%02x iv=%02x completed=%u\n",
        interface, rawStatus, output[0], output[1], output[2], output[3], publicIV.bytes[0], native_aes_completed);
  native_aes_interface_status = interface ? 1 : 2;
  if (source) source->release();
  if (destination) destination->release();
  result = result && interface;
  cc_clear(sizeof(key), &key);
  cc_clear(sizeof(output), output);
  return result;
}


// Target donor KDF: base-key CBC encryption of the public 16-byte seed, IV=0.
// This board supplies a 256-bit SCM-derived software secret from a genuine
// hardware-wrapped key. It is a HiOS device root, not Apple's fused UID.
static bool deriveNonceKey(const unsigned char root[32], unsigned char output[16])
{
  const unsigned char seed[16] = {0x0c,0x31,0xf7,0xdf,0x81,0x92,0x4d,0x03,
                                 0x8c,0xb4,0x37,0xfa,0x03,0x60,0xf7,0xc9};
  IOAESKeyData key = {256, {}, 0};
  unsigned char iv[16] = {};
  memcpy(key.bytes, root, 32);
  bool ok = !cbc(seed, output, 16, iv, key, IOAESEncrypt);
  cc_clear(sizeof(key), &key); cc_clear(sizeof(iv), iv);
  return ok;
}

static bool nonceKnownAnswerTest(NativeAES *provider, bool negative)
{
  unsigned char root[32], derived[16], input[16], output[16];
  for (unsigned i=0;i<32;i++) root[i]=static_cast<unsigned char>(i);
  for (unsigned i=0;i<16;i++) input[i]=static_cast<unsigned char>(i);
  unsigned char expectedKey[16]={0xdc,0x82,0xe5,0x6c,0x6d,0x61,0x0d,0x77,
                                0x10,0x46,0x2a,0x90,0x90,0x82,0xcc,0x11};
  const unsigned char expected[16]={0xb0,0x34,0xaa,0x1f,0xc9,0x96,0xce,0x6e,
                                   0x5a,0xb1,0x9a,0x9a,0x21,0x9b,0x5f,0xca};
  if (negative) expectedKey[0]^=1;
  bool ok=deriveNonceKey(root,derived) && !memcmp(derived,expectedKey,16);
  IOBufferMemoryDescriptor *source=IOBufferMemoryDescriptor::withCapacity(64,kIODirectionInOut);
  IOBufferMemoryDescriptor *destination=IOBufferMemoryDescriptor::withCapacity(64,kIODirectionInOut);
  IOAESKeyData key={128,{},2212}; IOAESIV iv={};
  struct Completion { unsigned count;int status; } completion={};
  auto callback=[](void *p,int status){Completion *s=static_cast<Completion *>(p);s->count++;s->status=status;};
  if (ok && source && destination) {
    memcpy(native_nonce_key,derived,16);native_nonce_selftest=true;
    ok=!provider->isKeySupported(2212) && source->writeBytes(16,input,16)==16 &&
      !provider->performAES(source,destination,16,&iv,IOAESEncrypt,&key,16,32,callback,&completion) &&
      destination->readBytes(32,output,16)==16 && !memcmp(output,expected,16) &&
      !memcmp(iv.bytes,expected,16) && completion.count==1 && completion.status==0;
    cc_clear(sizeof(iv),&iv);
    ok=ok && !provider->performAES(destination,destination,16,&iv,IOAESDecrypt,&key,32,32,nullptr,nullptr) &&
      destination->readBytes(32,output,16)==16 && !memcmp(output,input,16) && !memcmp(iv.bytes,expected,16);
    key.bits=256;
    ok=ok && provider->performAES(source,destination,16,&iv,IOAESEncrypt,&key,16,32,callback,&completion)==kIOReturnBadArgument && completion.count==1;
    native_nonce_selftest=false;
  } else ok=false;
  cc_clear(sizeof(native_nonce_key),native_nonce_key);
  cc_clear(sizeof(root),root);cc_clear(sizeof(derived),derived);cc_clear(sizeof(output),output);
  if(source)source->release();if(destination)destination->release();
  IOLog("KEBAB AES NONCE: public KDF/selector/CBC/invalid-bits KAT %s negative=%u\n",ok?"PASS":"FAIL",negative);
  return ok;
}

extern "C" bool kebab_aes_nonce_prepare(const unsigned char *, unsigned);
extern "C" bool kebab_aes_nonce_publish(void);
extern "C" void kebab_aes_nonce_cancel(void);
extern "C" void kebab_aes_nonce_wipe(void);

bool kebab_aes_nonce_prepare(const unsigned char *root, unsigned size)
{
  if(!root || size!=32 || !native_nonce_provider || native_aes_kat_status!=1 ||
     __atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)) return false;
  unsigned nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=root[i];if(!nonzero)return false;
  uint32_t negative=0;PE_parse_boot_argn("kebab_nonce_negative_kat",&negative,sizeof(negative));
  if(!nonceKnownAnswerTest(native_nonce_provider,negative==1)) return false;
  unsigned char derived[16];
  bool derivedOK=deriveNonceKey(root,derived);
  if(derivedOK)memcpy(native_nonce_key,derived,16);
  cc_clear(sizeof(derived),derived);
  if(!derivedOK)return false;
  __atomic_store_n(&native_nonce_state,1,__ATOMIC_RELEASE);
  IOLog("KEBAB AES NONCE: hardware-root KDF prepared; service not yet published\n");
  return true;
}

bool kebab_aes_nonce_publish(void)
{
  if(!native_nonce_provider || __atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)!=1) return false;
  native_nonce_provider->setProperty("NativeAESSoftwareKeysOnly",false);
  native_nonce_provider->setProperty("HiOSHardwareWrappedNonceRoot",true);
  native_nonce_provider->setProperty("HiOSNonceSelector",2212,32);
  __atomic_store_n(&native_nonce_state,2,__ATOMIC_RELEASE);
  native_aes_registered=1;
  native_nonce_provider->registerService();
  IOLog("KEBAB AES NONCE: NativeAES registered with hardware-root selector 2212\n");
  return true;
}

void kebab_aes_nonce_cancel(void)
{
  if(__atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)==2) return;
  cc_clear(sizeof(native_nonce_key),native_nonce_key);
  __atomic_store_n(&native_nonce_state,0,__ATOMIC_RELEASE);
}

// Reset path: fixed resident bytes only, no allocation, lock, driver call or SMC.
void kebab_aes_nonce_wipe(void)
{
  volatile unsigned char *bytes=native_nonce_key;
  for(unsigned i=0;i<sizeof(native_nonce_key);i++)bytes[i]=0;
  native_nonce_selftest=false;
  __atomic_store_n(&native_nonce_state,0,__ATOMIC_RELEASE);
  __asm__ volatile("dc civac, %0" :: "r"(native_nonce_key) : "memory");
  __asm__ volatile("dsb sy" ::: "memory");
}

bool NativeAES::start(IOService *provider)
{
  // CBC qualification does not provide Apple's UID/GID hardware keys.
  // Keep the incomplete nonce-manager integration explicitly opt-in.
  uint32_t enabled = 0;
  PE_parse_boot_argn("kebab_software_aes", &enabled, sizeof(enabled));
  uint32_t hardware=0;PE_parse_boot_argn("kebab_hardware_nonce",&hardware,sizeof(hardware));
  if (kebab_image4_live_nonce_profile()) hardware=1;
  if (enabled!=1 && hardware!=1) return false;
  if (!g_crypto_funcs || !g_crypto_funcs->ccaes_cbc_encrypt || !g_crypto_funcs->ccaes_cbc_decrypt ||
      !IOService::start(provider)) return false;
  // The native class is only a subclass of this target's original base.
  const OSMetaClass *aes = getMetaClass()->getSuperClass();
  const OSMetaClass *crypto = aes ? aes->getSuperClass() : nullptr;
  const OSMetaClass *service = crypto ? crypto->getSuperClass() : nullptr;
  IOLog("KEBAB AES: ABI aes=%p/%u crypto=%p/%u service=%p/%u\n",
        aes, aes ? aes->getClassSize() : 0, crypto, crypto ? crypto->getClassSize() : 0,
        service, service ? service->getClassSize() : 0);
  if (!aes || aes->getClassSize() != 0x120 || strcmp(aes->getClassName(), "IOAESAccelerator") ||
      !crypto || crypto->getClassSize() != 0x88 || strcmp(crypto->getClassName(), "IOCryptoAccelerator") ||
      !service || service != IOService::metaClass || service->getClassSize() != 0x88) return false;
  uint32_t negative = 0;
  PE_parse_boot_argn("kebab_aes_negative_kat", &negative, sizeof(negative));
  native_aes_kat_status = knownAnswerTest(this, negative == 1) ? 1 : 2;
  IOLog("KEBAB AES: KAT=%u interface=%u completed=%u unsupported=%u negative=%u\n",
        native_aes_kat_status, native_aes_interface_status, native_aes_completed,
        native_aes_unsupported_key, negative == 1);
  if (native_aes_kat_status != 1) return false;
  if (hardware==1) {
    if(native_nonce_provider)return false;
    native_nonce_provider=this;retain();
    IOLog("KEBAB AES NONCE: provider waiting for restored hardware keyblob; unregistered\n");
    return true;
  }
  setProperty("NativeAESSoftwareKeysOnly", true);
  native_aes_registered = 1;
  registerService();
  IOLog("KEBAB AES: NativeAES registered; software raw keys only\n");
  return true;
}

IOReturn NativeAES::performAES(IOMemoryDescriptor *input, IOMemoryDescriptor *output,
                              unsigned long long bytes, IOAESIV *iv, IOAESOperation operation,
                              IOAESKeyData *key, unsigned long long inputOffset,
                              unsigned long long outputOffset, IOAESCompletion callback, void *refcon)
{
  if (!input || !output || !key || !iv || !bytes || (bytes & 15) ||
      (key->bits != 128 && key->bits != 192 && key->bits != 256)) return kIOReturnBadArgument;
  struct SelectedKey { IOAESKeyData value; ~SelectedKey(){cc_clear(sizeof(value),&value);} } selected={*key};
  if(key->selector==2212 && (native_nonce_selftest || __atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)==2)) {
    if(key->bits!=128)return kIOReturnBadArgument;
    selected.value.selector=0;memcpy(selected.value.bytes,native_nonce_key,16);
  }
  if (selected.value.selector || (operation != IOAESEncrypt && operation != IOAESDecrypt)) {
    IOLog("KEBAB AES: rejected selector=%u bits=%u operation=%u bytes=%llu\n",
          key->selector, key->bits, static_cast<unsigned>(operation), bytes);
    __atomic_fetch_add(&native_aes_unsupported_key, 1, __ATOMIC_RELAXED);
    return kIOReturnUnsupported;
  }
  if (inputOffset > input->getLength() || bytes > input->getLength() - inputOffset ||
      outputOffset > output->getLength() || bytes > output->getLength() - outputOffset) return kIOReturnBadArgument;
  unsigned char *buffer = static_cast<unsigned char *>(IOMalloc(4096));
  if (!buffer) return kIOReturnNoMemory;
  IOReturn result = input->prepare();
  if (result) {
    IOFree(buffer, 4096);
    return result;
  }
  result = output->prepare();
  if (result) {
    input->complete();
    IOFree(buffer, 4096);
    return result;
  }
  unsigned char chaining[16];
  memcpy(chaining, iv->bytes, 16);
  for (unsigned long long done = 0; done < bytes; ) {
    size_t count = static_cast<size_t>(bytes - done > 4096 ? 4096 : bytes - done);
    if (input->readBytes(inputOffset + done, buffer, count) != count ||
        cbc(buffer, buffer, count, chaining, selected.value, operation) ||
        output->writeBytes(outputOffset + done, buffer, count) != count) {
      result = kIOReturnIOError;
      break;
    }
    done += count;
  }
  if (!result) {
    memcpy(iv->bytes, chaining, 16);
    __atomic_fetch_add(&native_aes_completed, 1, __ATOMIC_RELAXED);
  }
  cc_clear(4096, buffer);
  IOFree(buffer, 4096);
  cc_clear(sizeof(chaining), chaining);
  output->complete();
  input->complete();
  if (callback) {
    callback(refcon, result);
    return kIOReturnSuccess;
  }
  return result;
}

IOReturn NativeAES::performAES(void *input, void *output, task_t task, unsigned long long bytes,
                              IOAESIV *iv, IOAESOperation operation, IOAESKeyData *key,
                              IOAESCompletion callback, void *refcon)
{
  if (!task || !input || !output || !bytes) return kIOReturnBadArgument;
  IOMemoryDescriptor *source = IOMemoryDescriptor::withAddressRange(
      reinterpret_cast<mach_vm_address_t>(input), bytes, kIODirectionOut, task);
  IOMemoryDescriptor *destination = IOMemoryDescriptor::withAddressRange(
      reinterpret_cast<mach_vm_address_t>(output), bytes, kIODirectionIn, task);
  if (!source || !destination) {
    if (source) source->release();
    if (destination) destination->release();
    return kIOReturnNoMemory;
  }
  IOReturn result = performAES(source, destination, bytes, iv, operation, key, 0, 0, callback, refcon);
  source->release();
  destination->release();
  return result;
}

#endif /* KEBAB */
