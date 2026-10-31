#define _DEFAULT_SOURCE 1
#include <strings.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#define KEBAB 1
#define CP_RAW_KEY_WRAPPEDKEY 1u
#define kIOReturnBadArgument 0xe00002c2u
#define kIOReturnError 0xe00002bcu
#define kIOReturnNotPermitted 0xe00002e2u
#define kIOReturnNotReady 0xe00002d8u
#define kIOReturnUnsupported 0xe00002c7u
#define kIOReturnNoMemory 0xe00002bdu
#define Z_WAITOK 1
#define Z_ZERO 2
#define kalloc_data(n,f) calloc(1,n)
static void kfree_data(void *p,size_t n){for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);free(p);}
#include "kebab_file_keys.h"
typedef struct kebab_aks_raw_key cp_raw_key_s;
typedef struct kebab_aks_wrapped_key cp_wrapped_key_s;
typedef AES_KEY aes_encrypt_ctx;
typedef AES_KEY aes_decrypt_ctx;
static unsigned aes_calls,aes_failure,lock_depth;
static int panicflag,irq,preempt,interrupts=1;
static bool panic_active(void){return panicflag;}
static bool ml_at_interrupt_context(void){return irq;}
static bool ml_get_interrupts_enabled(void){return interrupts;}
int get_preemption_level(void){return preempt;}
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) int n
static void lck_mtx_lock(int *p){(void)p;assert(!lock_depth);lock_depth++;}
static void lck_mtx_unlock(int *p){(void)p;assert(lock_depth==1);lock_depth--;}
static unsigned rng;static void read_random(void *p,unsigned n){assert(lock_depth);for(unsigned i=0;i<n;i++)((uint8_t*)p)[i]=(uint8_t)(rng++);}
static int aes_encrypt_key128(const uint8_t *k,AES_KEY *c){return AES_set_encrypt_key(k,128,c);}
static int aes_decrypt_key128(const uint8_t *k,AES_KEY *c){return AES_set_decrypt_key(k,128,c);}
static int aes_encrypt_key256(const uint8_t *k,AES_KEY *c){return AES_set_encrypt_key(k,256,c);}
static int aes_decrypt_key256(const uint8_t *k,AES_KEY *c){return AES_set_decrypt_key(k,256,c);}
static int aes_encrypt(const uint8_t *i,uint8_t *o,AES_KEY *c){if(++aes_calls==aes_failure)return -1;AES_encrypt(i,o,c);return 0;}
static int aes_decrypt(const uint8_t *i,uint8_t *o,AES_KEY *c){if(++aes_calls==aes_failure)return -1;AES_decrypt(i,o,c);return 0;}
static void mac(const void *di,unsigned long n,const void *key,unsigned long len,const void *data,unsigned char *out){assert(di==(void*)1);unsigned size;assert(HMAC(EVP_sha256(),key,(int)n,data,len,out,&size)&&size==32);}
static struct crypto {const void *ccsha256_di;void(*cchmac_fn)(const void*,unsigned long,const void*,unsigned long,const void*,unsigned char*);} funcs={(void*)1,mac},*g_crypto_funcs=&funcs;
static LCK_GRP_DECLARE(kfk_group, "kebab file keys");
static LCK_MTX_DECLARE(kfk_lock, &kfk_group);
static uint8_t kfk_root_kek[32], kfk_class_d[32], kfk_uuid[16];
static bool kfk_pending, kfk_ready, kfk_bag_ready;

static void
kfk_wipe(void *memory, size_t bytes)
{
 volatile uint8_t *p=memory;
 while (bytes--) *p++=0;
}

static bool
kfk_context(void)
{
 return !panic_active() && !ml_at_interrupt_context() && !get_preemption_level() && ml_get_interrupts_enabled();
}

static bool
kfk_range(const void *p, size_t bytes)
{
 return p && bytes && (uintptr_t)p<=UINTPTR_MAX-bytes;
}

static bool
kfk_overlap(const void *a, size_t n, const void *b, size_t m)
{
 return (uintptr_t)a<(uintptr_t)b+m && (uintptr_t)b<(uintptr_t)a+n;
}

/* RFC3394, using the already-qualified constant-time native AES provider.
 * Sizes are bounded to private file/keybag/metadata envelopes and public KAT input. */
static int
kfk_wrap(const uint8_t kek[32], const uint8_t *input, size_t bytes, uint8_t *output, bool encrypt)
{
 if (!kfk_range(input, bytes) || !kfk_range(output, encrypt?bytes+8:bytes-8) ||
     bytes%8 || bytes<(encrypt?16u:24u) || bytes>(encrypt?120u:128u)) return (int)kIOReturnBadArgument;
 uint8_t work[128]={0}, block[16]={0};
 aes_encrypt_ctx enc; aes_decrypt_ctx dec;
 kfk_wipe(&enc,sizeof(enc)); kfk_wipe(&dec,sizeof(dec));
 unsigned n=(unsigned)(encrypt?bytes/8:bytes/8-1);
 int error=encrypt?aes_encrypt_key256(kek,&enc):aes_decrypt_key256(kek,&dec);
 if (error) { error=(int)kIOReturnError; goto done; }
 if (encrypt) { memset(work,0xa6,8);memcpy(work+8,input,bytes); }
 else memcpy(work,input,bytes);
 for (unsigned step=0;step<6*n;++step) {
  unsigned j=encrypt?step/n:5-step/n;
  unsigned i=encrypt?step%n+1:n-step%n;
  uint64_t t=(uint64_t)n*j+i;
  memcpy(block,work,8);memcpy(block+8,work+8*i,8);
  if (!encrypt) for (unsigned b=0;b<8;++b) block[7-b]^=(uint8_t)(t>>(8*b));
  if (encrypt?aes_encrypt(block,block,&enc):aes_decrypt(block,block,&dec)) { error=(int)kIOReturnError;goto done; }
  if (encrypt) for (unsigned b=0;b<8;++b) block[7-b]^=(uint8_t)(t>>(8*b));
  memcpy(work,block,8);memcpy(work+8*i,block+8,8);
 }
 if (!encrypt) {
  unsigned diff=0;for(unsigned i=0;i<8;++i)diff|=work[i]^0xa6;
  if (diff) { error=(int)kIOReturnNotPermitted;goto done; }
 }
 memcpy(output,work+(encrypt?0:8),encrypt?bytes+8:bytes-8);
 error=0;
done:
 kfk_wipe(work,sizeof(work));kfk_wipe(block,sizeof(block));
 kfk_wipe(&enc,sizeof(enc));kfk_wipe(&dec,sizeof(dec));
 return error;
}


static void kdc_clear_locked(void){} /* explicit unrelated CPX teardown model */
/* Persistent owner class keys. Independent from Data UUID and the synthetic
 * proof bag. Factory no-passcode policy only; no SEP/passcode equivalence. */
static uint8_t kdb_class_c[32],kdb_class_d[32],kdb_identifier[16];
static bool kdb_ready;
static uint8_t kdb_record[4096];

static void
kdb_clear_locked(void)
{
 kdc_clear_locked();kdb_ready=false;kfk_wipe(kdb_record,sizeof(kdb_record));kfk_wipe(kdb_class_c,32);kfk_wipe(kdb_class_d,32);kfk_wipe(kdb_identifier,16);
}

static bool
kdb_nonzero(const uint8_t *bytes,unsigned count)
{
 unsigned any=0;for(unsigned i=0;i<count;++i)any|=bytes[i];return any!=0;
}

static bool
kdb_identity(const uint8_t container[16],const uint8_t system[16])
{
 return kfk_range(container,16)&&kfk_range(system,16)&&kdb_nonzero(container,16)&&
     kdb_nonzero(system,16)&&memcmp(container,system,16);
}

static void
kdb_header(uint8_t header[128],const uint8_t container[16],const uint8_t system[16])
{
 memset(header,0,128);memcpy(header,"HIOS-DEVICE-BAG",16);
 const uint32_t version=1,size=4096,wrapped=120,policy=1;
 memcpy(header+16,&version,4);memcpy(header+20,&size,4);
 memcpy(header+24,container,16);memcpy(header+40,system,16);
 memcpy(header+56,&wrapped,4);memcpy(header+60,&policy,4);
}

static void
kdb_kek_locked(const uint8_t container[16],const uint8_t system[16],uint8_t kek[32])
{
 static const uint8_t domain[]="UniXNU.kebab.device-classbag.v1";
 uint8_t context[sizeof(domain)+32];
 memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),container,16);
 memcpy(context+sizeof(domain)+16,system,16);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(context),context,kek);
 kfk_wipe(context,sizeof(context));
}

int
kebab_device_classbag_create(const uint8_t container[16],const uint8_t system[16],void *output,unsigned bytes)
{
 if(!kfk_context()||!kdb_identity(container,system)||bytes!=4096||!kfk_range(output,bytes)||
     kfk_overlap(container,16,output,bytes)||kfk_overlap(system,16,output,bytes))return (int)kIOReturnBadArgument;
 uint8_t plain[112]={0},sealed[120]={0},kek[32]={0};
 memcpy(plain,"DCB1",4);plain[4]=1;plain[5]=1;plain[6]=3;plain[7]=4;
 lck_mtx_lock(&kfk_lock);
 int error=(int)kIOReturnNotReady;
 if(kfk_ready){
  read_random(plain+16,96); /* Bag ID16, independent C32/D32, epoch16. */
  if(!kdb_nonzero(plain+16,16)||!kdb_nonzero(plain+32,32)||!kdb_nonzero(plain+64,32)||
      !kdb_nonzero(plain+96,16)||!memcmp(plain+32,plain+64,32))error=(int)kIOReturnError;
  else{
   kdb_kek_locked(container,system,kek);error=kfk_wrap(kek,plain,112,sealed,true);
   if(!error){memset(output,0,4096);kdb_header(output,container,system);memcpy((uint8_t*)output+128,sealed,120);}
  }
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(kek,sizeof(kek));return error;
}

int
kebab_device_classbag_open(const uint8_t container[16],const uint8_t system[16],const void *input,unsigned bytes)
{
 if(!kfk_context()||!kdb_identity(container,system)||bytes!=4096||!kfk_range(input,bytes))return (int)kIOReturnBadArgument;
 const uint8_t *record=input;uint8_t header[128],sealed[120],plain[112]={0},kek[32]={0};
 kdb_header(header,container,system);memcpy(sealed,record+128,120);
 unsigned diff=memcmp(record,header,128)!=0;
 for(unsigned i=248;i<4096;++i)diff|=record[i];
 lck_mtx_lock(&kfk_lock);kdb_clear_locked();
 int error=!kfk_ready?(int)kIOReturnNotReady:diff?(int)kIOReturnNotPermitted:0;
 if(!error){kdb_kek_locked(container,system,kek);error=kfk_wrap(kek,sealed,120,plain,false);}
 if(!error){
  if(memcmp(plain,"DCB1\1\1\3\4",8))error=(int)kIOReturnNotPermitted;
  for(unsigned i=8;i<16;++i)if(plain[i])error=(int)kIOReturnNotPermitted;
  if(!kdb_nonzero(plain+16,16)||!kdb_nonzero(plain+32,32)||!kdb_nonzero(plain+64,32)||
      !kdb_nonzero(plain+96,16)||!memcmp(plain+32,plain+64,32))error=(int)kIOReturnNotPermitted;
 }
 if(!error){memcpy(kdb_identifier,plain+16,16);memcpy(kdb_class_c,plain+32,32);memcpy(kdb_class_d,plain+64,32);memcpy(kdb_record,record,4096);kdb_ready=true;}
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(header,sizeof(header));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(plain,sizeof(plain));kfk_wipe(kek,sizeof(kek));return error;
}

bool
kebab_device_classbag_available(void)
{
 if(!kfk_context())return false;
 lck_mtx_lock(&kfk_lock);bool ready=kfk_ready&&kdb_ready;lck_mtx_unlock(&kfk_lock);return ready;
}

/* Snapshot public policy and actual unwrapped class availability under the
 * same lock used by authentication/revocation. Unknown policies fail closed.
 * A status read never generates, loads, modifies or exports any key material. */
int
kebab_device_classbag_get_status(struct kebab_device_classbag_status *output)
{
 if(!kfk_context()||!kfk_range(output,sizeof(*output))||
     kfk_overlap(output,sizeof(*output),kdb_record,sizeof(kdb_record))||
     kfk_overlap(output,sizeof(*output),kdb_class_c,sizeof(kdb_class_c))||
     kfk_overlap(output,sizeof(*output),kdb_class_d,sizeof(kdb_class_d))||
     kfk_overlap(output,sizeof(*output),kdb_identifier,sizeof(kdb_identifier)))return (int)kIOReturnBadArgument;
 struct kebab_device_classbag_status status={0};
 lck_mtx_lock(&kfk_lock);
 int error=(kfk_ready&&kdb_ready)?0:(int)kIOReturnNotReady;
 if(!error){
  memcpy(&status.version,kdb_record+16,sizeof(status.version));
  memcpy(&status.policy,kdb_record+60,sizeof(status.policy));
  if(kdb_nonzero(kdb_class_c,sizeof(kdb_class_c)))status.available_classes|=1u<<3;
  if(kdb_nonzero(kdb_class_d,sizeof(kdb_class_d)))status.available_classes|=1u<<4;
  if(status.version!=1||status.policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE||
      status.available_classes!=KEBAB_DEVICE_BAG_CLASS_C_D_MASK||
      !memcmp(kdb_class_c,kdb_class_d,sizeof(kdb_class_c)))error=(int)kIOReturnNotReady;
  if(!error)memcpy(output,&status,sizeof(status));
 }
 lck_mtx_unlock(&kfk_lock);
 return error;
}

/* Export only the already-authenticated sealed bank, never its plaintext
 * C/D keys. Root cancellation and failed authentication revoke this copy. */
int
kebab_device_classbag_export(void *output,unsigned bytes)
{
 if(!kfk_context()||bytes!=4096||!kfk_range(output,bytes)||
     kfk_overlap(output,bytes,kdb_record,sizeof(kdb_record)))return (int)kIOReturnBadArgument;
 lck_mtx_lock(&kfk_lock);
 int error=(kfk_ready&&kdb_ready)?0:(int)kIOReturnNotReady;
 if(!error)memcpy(output,kdb_record,4096);
 lck_mtx_unlock(&kfk_lock);return error;
}

/* AKS cookie is opaque to original APFS/userland. This backend authenticates
 * the real bank ID, container and System IDs under the restored hardware Root.
 * Only factory device context0 is implemented, not alternate users/passcodes. */
int
kebab_device_classbag_cookie(const uint8_t context[16],void *output,unsigned bytes)
{
 if(!kfk_context()||bytes!=34||!kfk_range(context,16)||!kfk_range(output,bytes)||
     kfk_overlap(context,16,output,bytes))return (int)kIOReturnBadArgument;
 unsigned diff=0;for(unsigned i=0;i<16;++i)diff|=context[i];
 if(diff)return (int)kIOReturnNotPermitted;
 static const uint8_t domain[]="UniXNU.kebab.system-bag-volume-cookie.v1";
 uint8_t message[sizeof(domain)+64],result[34]={0x4b,1};
 lck_mtx_lock(&kfk_lock);int error=(kfk_ready&&kdb_ready)?0:(int)kIOReturnNotReady;
 if(!error){
  memcpy(message,domain,sizeof(domain));memcpy(message+sizeof(domain),kdb_identifier,16);
  memcpy(message+sizeof(domain)+16,context,16);memcpy(message+sizeof(domain)+32,kdb_record+24,32);
  g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(message),message,result+2);
  memcpy(output,result,sizeof(result));
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(message,sizeof(message));kfk_wipe(result,sizeof(result));return error;
}
/* Device-bound keychain key wrapping, separate from APFS file class keys.
 * Exact original securityd mapping:7=CK,8=DK,10=CKU,11=DKU. Only the
 * authenticated factory-no-passcode bank is supported. AK/AKU/AKPU need
 * their own real lock/passcode lifecycle; unknown/generation classes refuse.
 * No new bag, record, class downgrade, software Root or plaintext persistence.
 * This header is included after kebab_device_classbag_impl.h in its C owner. */
int
kebab_keychain_key_crypt(int32_t bag,uint32_t cls,const void *input,unsigned input_bytes,
    void *output,unsigned output_bytes,bool encrypt)
{
 if(!kfk_context()||bag||!kfk_range(input,input_bytes)||!kfk_range(output,output_bytes)||
     input_bytes!=(encrypt?32u:40u)||output_bytes!=(encrypt?40u:32u)||
     kfk_overlap(input,input_bytes,output,output_bytes))return (int)kIOReturnBadArgument;
 if(cls!=7&&cls!=8&&cls!=10&&cls!=11)return (int)kIOReturnUnsupported;
 /* The kernel-only API must not alias secret owner storage, even on errors. */
 const void *private_ranges[]={kfk_root_kek,kfk_class_d,kfk_uuid,kdb_record,kdb_class_c,kdb_class_d,kdb_identifier};
 const size_t private_bytes[]={sizeof(kfk_root_kek),sizeof(kfk_class_d),sizeof(kfk_uuid),sizeof(kdb_record),sizeof(kdb_class_c),sizeof(kdb_class_d),sizeof(kdb_identifier)};
 for(unsigned i=0;i<sizeof(private_ranges)/sizeof(private_ranges[0]);++i){
  if(kfk_overlap(input,input_bytes,private_ranges[i],private_bytes[i])||
      kfk_overlap(output,output_bytes,private_ranges[i],private_bytes[i]))return (int)kIOReturnBadArgument;
 }
 static const uint8_t domain[]="UniXNU.kebab.device-keychain.RFC3394.v1";
 /* Domain including NUL, authenticated bag ID, container/System IDs,
  * record version/policy and exact keychain class. This is a backend-owned
  * format:40 bytes of RFC3394 ciphertext, not an Apple SEP wrapped blob. */
 uint8_t context[sizeof(domain)+16+32+12]={0},kek[32]={0},result[40]={0};
 lck_mtx_lock(&kfk_lock);
 uint32_t version,policy;
 memcpy(&version,kdb_record+16,4);memcpy(&policy,kdb_record+60,4);
 int error=(kfk_ready&&kdb_ready)?0:(int)kIOReturnNotReady;
 if(!error&&(version!=1||policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE||
     !kdb_nonzero(kdb_identifier,16)||!kdb_nonzero(kdb_class_c,32)||
     !kdb_nonzero(kdb_class_d,32)||!memcmp(kdb_class_c,kdb_class_d,32)))error=(int)kIOReturnNotReady;
 if(!error){
  memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),kdb_identifier,16);
  memcpy(context+sizeof(domain)+16,kdb_record+24,32);
  memcpy(context+sizeof(domain)+48,&version,4);memcpy(context+sizeof(domain)+52,&policy,4);
  memcpy(context+sizeof(domain)+56,&cls,4);
  g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(context),context,kek);
  error=kfk_wrap(kek,input,input_bytes,result,encrypt);
  if(!error)memcpy(output,result,output_bytes);
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(context,sizeof(context));kfk_wipe(kek,sizeof(kek));kfk_wipe(result,sizeof(result));
 return error;
}
static const uint8_t restored_root[32]={50,88,119,171,52,79,157,221,196,226,4,195,79,26,210,238,100,216,67,131,131,246,117,152,128,104,79,88,146,87,58,137};
static const uint8_t reference[4][40]={{118,69,118,190,135,9,205,150,194,41,118,232,49,151,164,144,245,93,242,34,12,249,74,51,114,193,65,91,122,5,74,52,214,148,10,170,4,19,116,14},{231,209,174,147,252,125,211,74,17,94,252,47,24,203,249,176,58,58,32,157,124,44,133,162,14,63,176,113,190,191,123,218,151,143,36,177,135,64,187,61},{85,78,131,218,116,166,184,99,199,105,253,182,22,182,211,44,210,211,159,79,234,36,42,90,225,60,198,160,193,222,118,150,149,188,216,152,71,51,119,211},{255,158,232,133,34,95,149,203,239,149,57,11,161,239,77,88,187,137,195,244,1,138,25,102,153,175,6,93,214,76,86,119,105,122,29,196,44,57,156,233}};

static unsigned cases;
static uint8_t container[16],system_id[16],saved[4096],plain[32],cipher[48],out[48];
static void setup(void){
 memcpy(kfk_root_kek,restored_root,32);kfk_ready=true;kdb_ready=false;
 for(unsigned i=0;i<16;i++){container[i]=32+i;system_id[i]=64+i;}
 rng=0;assert(!kebab_device_classbag_create(container,system_id,saved,4096));
 assert(!kebab_device_classbag_open(container,system_id,saved,4096));
 for(unsigned i=0;i<32;i++)plain[i]=128+i;
}
static void untouched(const uint8_t *p,unsigned bytes){for(unsigned i=0;i<bytes;i++)assert(p[i]==0x5a);}
static int crypt(uint32_t cls,bool encrypt){return kebab_keychain_key_crypt(0,cls,encrypt?plain:cipher,encrypt?32:40,out,encrypt?40:32,encrypt);}
static void refuses(uint32_t cls,bool encrypt){memset(out,0x5a,sizeof(out));assert(crypt(cls,encrypt));untouched(out,sizeof(out));assert(!lock_depth);cases++;}
int main(void){setup();uint8_t bank_before[4096],c_before[32],d_before[32];
 memcpy(bank_before,kdb_record,4096);memcpy(c_before,kdb_class_c,32);memcpy(d_before,kdb_class_d,32);
 unsigned old_rng=rng;uint32_t classes[]={7,8,10,11};
 for(unsigned index=0;index<4;index++){
  uint32_t cls=classes[index];memset(out,0x5a,sizeof(out));assert(!crypt(cls,true));assert(!memcmp(out,reference[index],40));untouched(out+40,8);memcpy(cipher,out,40);cases++;
  memset(out,0x5a,sizeof(out));assert(!crypt(cls,false));assert(!memcmp(out,plain,32));untouched(out+32,16);cases++;
  for(unsigned other=0;other<4;other++)if(other!=index)refuses(classes[other],false);
  for(unsigned bit=0;bit<320;bit++){cipher[bit/8]^=1u<<(bit%8);refuses(cls,false);cipher[bit/8]^=1u<<(bit%8);}
  for(unsigned fault=1;fault<=24;fault++)for(unsigned encrypt=0;encrypt<2;encrypt++){
   aes_failure=aes_calls+fault;refuses(cls,encrypt);aes_failure=0;
  }
 }
 for(unsigned cls=0;cls<512;cls++)if(cls!=7&&cls!=8&&cls!=10&&cls!=11){refuses(cls,true);refuses(cls,false);}
 refuses(UINT32_MAX,true);refuses(UINT32_MAX,false);
 for(unsigned bytes=0;bytes<65;bytes++)if(bytes!=32){memset(out,0x5a,48);assert(kebab_keychain_key_crypt(0,7,plain,bytes,out,40,true));untouched(out,48);cases++;}
 for(unsigned bytes=0;bytes<65;bytes++)if(bytes!=40){memset(out,0x5a,48);assert(kebab_keychain_key_crypt(0,7,plain,32,out,bytes,true));untouched(out,48);cases++;}
 for(unsigned i=0;i<4;i++){panicflag=i==0;irq=i==1;preempt=i==2;interrupts=i!=3;refuses(7,true);panicflag=irq=preempt=0;interrupts=1;}
 for(unsigned encrypt=0;encrypt<2;encrypt++){
  kfk_ready=false;refuses(7,encrypt);kfk_ready=true;
  kdb_ready=false;refuses(7,encrypt);kdb_ready=true;
  for(unsigned off=16;off<=60;off+=44){uint32_t old,invalid=99;memcpy(&old,kdb_record+off,4);memcpy(kdb_record+off,&invalid,4);refuses(7,encrypt);memcpy(kdb_record+off,&old,4);}
 }
 void *priv[]={kfk_root_kek,kfk_class_d,kfk_uuid,kdb_record,kdb_class_c,kdb_class_d,kdb_identifier};
 for(unsigned i=0;i<sizeof(priv)/sizeof(priv[0]);i++){
  assert(kebab_keychain_key_crypt(0,7,priv[i],32,out,40,true));assert(kebab_keychain_key_crypt(0,7,plain,32,priv[i],40,true));cases+=2;
 }
 assert(kebab_keychain_key_crypt(0,7,NULL,32,out,40,true));assert(kebab_keychain_key_crypt(0,7,plain,32,NULL,40,true));cases+=2;
 assert(kebab_keychain_key_crypt(-1,7,plain,32,out,40,true));assert(kebab_keychain_key_crypt(1,7,plain,32,out,40,true));cases+=2;
 assert(kebab_keychain_key_crypt(0,7,plain,32,plain+1,40,true));assert(kebab_keychain_key_crypt(0,7,(void*)(UINTPTR_MAX-4),32,out,40,true));cases+=2;
 /* Derivation binds the original authenticated bank identity and root. */
 memcpy(cipher,reference[0],40);
 for(unsigned i=0;i<16;i++){kdb_identifier[i]^=1;refuses(7,false);kdb_identifier[i]^=1;}
 for(unsigned i=24;i<56;i++){kdb_record[i]^=1;refuses(7,false);kdb_record[i]^=1;}
 for(unsigned i=0;i<32;i++){kfk_root_kek[i]^=1;refuses(7,false);kfk_root_kek[i]^=1;}
 /* Real bank revoke/auth-fail and reopen, without replacing old C/D material. */
 kdb_clear_locked();refuses(7,false);assert(!kebab_device_classbag_open(container,system_id,saved,4096));assert(!crypt(7,false)&&!memcmp(out,plain,32));cases++;
 saved[130]^=1;assert(kebab_device_classbag_open(container,system_id,saved,4096));refuses(7,false);saved[130]^=1;
 assert(!kebab_device_classbag_open(container,system_id,saved,4096));assert(!crypt(7,false)&&!memcmp(out,plain,32));cases++;
 assert(rng==old_rng&&!memcmp(bank_before,kdb_record,4096)&&!memcmp(c_before,kdb_class_c,32)&&!memcmp(d_before,kdb_class_d,32));
 printf("PASS %u keychain core assertions; real bank create/open/revoke, 4 independent wraps, class/root/bag binding, faults/output preservation; original C/D record unchanged\n",cases);
}
