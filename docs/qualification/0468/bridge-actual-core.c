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
static void lck_mtx_lock(int *p){assert(!*p);*p=1;lock_depth++;}
static void lck_mtx_unlock(int *p){assert(*p&&lock_depth);*p=0;lock_depth--;}
static unsigned rng;static bool zero_rng;static void read_random(void *p,unsigned n){assert(lock_depth);for(unsigned i=0;i<n;i++)((uint8_t*)p)[i]=zero_rng?0:(uint8_t)(rng++);}
static int aes_encrypt_key128(const uint8_t *k,AES_KEY *c){return AES_set_encrypt_key(k,128,c);}
static int aes_decrypt_key128(const uint8_t *k,AES_KEY *c){return AES_set_decrypt_key(k,128,c);}
static int aes_encrypt_key256(const uint8_t *k,AES_KEY *c){return AES_set_encrypt_key(k,256,c);}
static int aes_decrypt_key256(const uint8_t *k,AES_KEY *c){return AES_set_decrypt_key(k,256,c);}
static int aes_encrypt(const uint8_t *i,uint8_t *o,AES_KEY *c){if(++aes_calls==aes_failure)return -1;AES_encrypt(i,o,c);return 0;}
static int aes_decrypt(const uint8_t *i,uint8_t *o,AES_KEY *c){if(++aes_calls==aes_failure)return -1;AES_decrypt(i,o,c);return 0;}
static void mac(const void *di,unsigned long n,const void *key,unsigned long len,const void *data,unsigned char *out){assert(di==(void*)1);unsigned size;assert(HMAC(EVP_sha256(),key,(int)n,data,len,out,&size)&&size==32);}
static void digest(const void*di,unsigned long n,const void*input,void*out){assert(di);SHA256(input,n,out);}
static struct crypto {const void *ccsha256_di;void(*ccdigest_fn)(const void*,unsigned long,const void*,void*);void(*cchmac_fn)(const void*,unsigned long,const void*,unsigned long,const void*,unsigned char*);} funcs={(void*)1,digest,mac},*g_crypto_funcs=&funcs;

#define kIOReturnIOError 0xe00002cau
#define CP_ACTION_LOCKED 0
#define CP_ACTION_UNLOCKED 1
int cp_key_store_action_for_volume(uint8_t *,int);
/* Owner keybag/file-key core. No AKS table or user client is published here.
 * Only audited class D is enabled; unknown classes and flags fail closed. */
#if defined(KEBAB)
#include "kebab_file_keys.h"
extern int get_preemption_level(void);
_Static_assert(sizeof(cp_raw_key_s)==sizeof(struct kebab_aks_raw_key) &&
    offsetof(cp_raw_key_s,iv_key)==offsetof(struct kebab_aks_raw_key,iv_key) &&
    offsetof(cp_raw_key_s,flags)==offsetof(struct kebab_aks_raw_key,flags), "native raw key compatibility");
_Static_assert(sizeof(cp_wrapped_key_s)==sizeof(struct kebab_aks_wrapped_key), "native wrapped key compatibility");

_Static_assert(CP_RAW_KEY_WRAPPEDKEY==KEBAB_RAW_KEY_WRAPPED,"native wrapped-key flag");
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

/* Math readiness only. Does not generate a key, change bag state, publish
 * class availability or replace any existing C/D result. Public KAT inputs. */
extern int kebab_x25519(uint8_t[32],const uint8_t[32],const uint8_t[32]);
extern int kebab_class_b_concat_kdf(uint8_t[32],const uint8_t[32],const uint8_t[32],const uint8_t[32],int(*)(const uint8_t*,size_t,uint8_t[32]));
static uint32_t kcb_math_state;
static int
kcb_sha256(const uint8_t *input,size_t bytes,uint8_t output[32])
{
 if(!g_crypto_funcs||!g_crypto_funcs->ccsha256_di||!g_crypto_funcs->ccdigest_fn)return 0;
 g_crypto_funcs->ccdigest_fn(g_crypto_funcs->ccsha256_di,(unsigned long)bytes,input,output);return 1;
}
static int
kcb_math_selftest(void)
{
 uint32_t state=__atomic_load_n(&kcb_math_state,__ATOMIC_ACQUIRE),expected=0;
 if(state>=2)return state==2;
 if(!__atomic_compare_exchange_n(&kcb_math_state,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
 static const uint8_t scalar[32]={0xa5,0x46,0xe3,0x6b,0xf0,0x52,0x7c,0x9d,0x3b,0x16,0x15,0x4b,0x82,0x46,0x5e,0xdd,0x62,0x14,0x4c,0x0a,0xc1,0xfc,0x5a,0x18,0x50,0x6a,0x22,0x44,0xba,0x44,0x9a,0xc4};
 static const uint8_t point[32]={0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c};
 static const uint8_t shared[32]={0xc3,0xda,0x55,0x37,0x9d,0xe9,0xc6,0x90,0x8e,0x94,0xea,0x4d,0xf2,0x8d,0x08,0x4f,0x32,0xec,0xcf,0x03,0x49,0x1c,0x71,0xf7,0x54,0xb4,0x07,0x55,0x77,0xa2,0x85,0x52};
 static const uint8_t ephemeral[32]={0x1c,0x9f,0xd8,0x8f,0x45,0x60,0x6d,0x93,0x2a,0x80,0xc7,0x18,0x24,0xae,0x15,0x1d,0x15,0xd7,0x3e,0x77,0xde,0x38,0xe8,0xe0,0x00,0x85,0x2e,0x61,0x4f,0xae,0x70,0x19};
 static const uint8_t peer[32]={0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c};
 static const uint8_t derived[32]={0x11,0x77,0xaa,0x99,0x70,0xc8,0x0b,0x7d,0xe6,0xaa,0xf7,0xa6,0xcf,0x6c,0xd7,0x1a,0x69,0x78,0x89,0xd2,0xb6,0x4b,0x93,0x07,0x50,0x49,0x9d,0x16,0x48,0xe5,0x40,0xda};
 uint8_t output[32]={0},zero[32]={0};
 int ok=kebab_x25519(output,scalar,point)&&!memcmp(output,shared,32);
 if(ok)ok=kebab_class_b_concat_kdf(output,shared,ephemeral,peer,kcb_sha256)&&!memcmp(output,derived,32);
 if(ok){memset(output,0xa5,32);ok=!kebab_x25519(output,scalar,zero);for(unsigned i=0;i<32;++i)ok=ok&&(output[i]==0xa5);}
 kfk_wipe(output,sizeof(output));kfk_wipe(zero,sizeof(zero));
 __atomic_store_n(&kcb_math_state,ok?2u:3u,__ATOMIC_RELEASE);
 printf("KEBAB CLASS-B CRYPTO: X25519/ConcatKDF KAT %s; math only, service/lock readiness checked separately\n",ok?"PASS":"FAIL");
 return ok;
}

static void kdc_clear_locked(void);
static void kbs_clear_locked(void);
/* Persistent owner class keys. Independent from Data UUID and the synthetic
 * proof bag. Factory no-passcode policy only; no SEP/passcode equivalence. */
static uint8_t kdb_class_c[32],kdb_class_d[32],kdb_identifier[16];
static bool kdb_ready;
static uint8_t kdb_record[4096];

static void
kdb_clear_locked(void)
{
 kbs_clear_locked();kdc_clear_locked();kdb_ready=false;kfk_wipe(kdb_record,sizeof(kdb_record));kfk_wipe(kdb_class_c,32);kfk_wipe(kdb_class_d,32);kfk_wipe(kdb_identifier,16);
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
/* Hardware-Root/bank-bound factory-no-passcode class-B private/public state.
 * Caller holds kfk_lock for *_locked helpers. Persistent DCB1 bytes and
 * random C/D keys are not changed. Lock destroys the cached B private key;
 * public-only new-file encryption remains possible until bank revocation. */
static uint8_t kbs_private[32],kbs_public[32];
static uint8_t kbs_binding[56];
static bool kbs_ready,kbs_unlocked,kbs_bootstrap_done;
static uint64_t kbs_generation;
/* Public identity metadata, retained across revocation to deny old raw I/O
 * under a different bank and to purge B vnodes when they subsequently close. */
static uint32_t kbs_raw_seen;
static uint8_t kbs_raw_bank[16],kbs_raw_volume[16];

static void
kbs_clear_locked(void)
{
 kbs_ready=false;kbs_unlocked=false;++kbs_generation;
 kfk_wipe(kbs_private,sizeof(kbs_private));kfk_wipe(kbs_public,sizeof(kbs_public));kfk_wipe(kbs_binding,sizeof(kbs_binding));
}

static bool
kbs_factory_locked(void)
{
 uint32_t version,policy;memcpy(&version,kdb_record+16,4);memcpy(&policy,kdb_record+60,4);
 bool ready=kfk_ready&&kdb_ready&&version==1&&policy==KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE&&
     kdb_nonzero(kdb_identifier,16)&&kdb_nonzero(kdb_class_c,32)&&kdb_nonzero(kdb_class_d,32)&&
     memcmp(kdb_class_c,kdb_class_d,32);
 if(ready&&kbs_ready)ready=!memcmp(kbs_binding,kdb_identifier,16)&&!memcmp(kbs_binding+16,kdb_record+24,32)&&
     !memcmp(kbs_binding+48,kdb_record+16,4)&&!memcmp(kbs_binding+52,kdb_record+60,4);
 return ready;
}

static int
kbs_restore_locked(void)
{
 kfk_wipe(kbs_private,sizeof(kbs_private));kbs_unlocked=false;++kbs_generation;
 if(!kbs_factory_locked()||!kcb_math_selftest())return (int)kIOReturnNotReady;
 static const uint8_t domain[]="UniXNU.kebab.protected-unless-open.class-key.v1";
 uint8_t context[sizeof(domain)+56],secret[32]={0},pub[32]={0},base[32]={9};
 memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),kdb_identifier,16);
 memcpy(context+sizeof(domain)+16,kdb_record+24,32);
 memcpy(context+sizeof(domain)+48,kdb_record+16,4);memcpy(context+sizeof(domain)+52,kdb_record+60,4);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(context),context,secret);
 int error=kdb_nonzero(secret,32)&&kebab_x25519(pub,secret,base)?0:(int)kIOReturnError;
 if(!error){
  memcpy(kbs_private,secret,32);memcpy(kbs_public,pub,32);
  memcpy(kbs_binding,kdb_identifier,16);memcpy(kbs_binding+16,kdb_record+24,32);
  memcpy(kbs_binding+48,kdb_record+16,4);memcpy(kbs_binding+52,kdb_record+60,4);
  kbs_ready=true;kbs_unlocked=true;++kbs_generation;
 }
 kfk_wipe(context,sizeof(context));kfk_wipe(secret,sizeof(secret));kfk_wipe(pub,sizeof(pub));kfk_wipe(base,sizeof(base));
 return error;
}

int
kebab_device_keybag_bootstrap(void)
{
 if(!kfk_context())return (int)kIOReturnBadArgument;
 lck_mtx_lock(&kfk_lock);
 int error=kbs_bootstrap_done?(kbs_ready&&kbs_factory_locked()?0:(int)kIOReturnNotReady):kbs_restore_locked();
 if(!error)kbs_bootstrap_done=true;
 lck_mtx_unlock(&kfk_lock);
 printf("KEBAB KEYBAG BOOT: authenticated no-passcode bootstrap result=%x; existing bank retained\n",error);
 return error;
}

int
kebab_device_keybag_get_lock_state(uint32_t *output)
{
 if(!kfk_context()||!kfk_range(output,sizeof(*output))||
     kfk_overlap(output,4,kbs_private,32)||kfk_overlap(output,4,kbs_public,32)||
     kfk_overlap(output,4,kfk_root_kek,32)||kfk_overlap(output,4,kdb_record,4096)||
     kfk_overlap(output,4,kdb_class_c,32)||kfk_overlap(output,4,kdb_class_d,32))return (int)kIOReturnBadArgument;
 lck_mtx_lock(&kfk_lock);
 int error=kbs_factory_locked()&&kbs_ready&&kbs_bootstrap_done?0:(int)kIOReturnNotReady;
 if(!error)*output=kbs_unlocked?4u:5u; /* original bit0 locked, bit2 been-unlocked */
 lck_mtx_unlock(&kfk_lock);return error;
}

/* Only call after validating the live Data UUID and successful crypto. */
static int
kbs_bind_raw_locked(const uint8_t volume[16])
{
 if(__atomic_load_n(&kbs_raw_seen,__ATOMIC_ACQUIRE)){
  return memcmp(kbs_raw_bank,kdb_identifier,16)||memcmp(kbs_raw_volume,volume,16)?(int)kIOReturnNotPermitted:0;
 }
 memcpy(kbs_raw_bank,kdb_identifier,16);memcpy(kbs_raw_volume,volume,16);
 __atomic_store_n(&kbs_raw_seen,1u,__ATOMIC_RELEASE);return 0;
}

bool
kebab_apfs_b_volume_seen(void)
{
 return __atomic_load_n(&kbs_raw_seen,__ATOMIC_ACQUIRE)!=0;
}

bool
kebab_apfs_b_volume_matches(const uint8_t volume[16])
{
 return kfk_range(volume,16)&&kebab_apfs_b_volume_seen()&&!memcmp(volume,kbs_raw_volume,16);
}

void
kebab_apfs_b_note_cpx_destroy(const void *key,unsigned bytes,const char *operation)
{
 if(bytes>64||!kebab_apfs_b_volume_seen())return;
 static uint32_t reports;uint32_t n=__atomic_fetch_add(&reports,1u,__ATOMIC_RELAXED);
 if(n<64)printf("KEBAB CPX STORAGE DESTROY: operation=%s storage-address=%p cached-bytes=%u; no key bytes logged\n",operation,key,bytes);
}
/* Device-bound keychain key wrapping, separate from APFS file class keys.
 * Exact original securityd mapping:7=CK,8=DK,10=CKU,11=DKU. Only the
 * authenticated factory-no-passcode bank is supported. AK/AKU share the real device lock lifecycle; AKPU needs
 * a passcode lifecycle; unknown/generation classes refuse.
 * No new bag, record, class downgrade, software Root or plaintext persistence.
 * This header is included after kebab_device_classbag_impl.h in its C owner. */
int
kebab_keychain_key_crypt(int32_t bag,uint32_t cls,const void *input,unsigned input_bytes,
    void *output,unsigned output_bytes,bool encrypt)
{
 if(!kfk_context()||bag||!kfk_range(input,input_bytes)||!kfk_range(output,output_bytes)||
     input_bytes!=(encrypt?32u:40u)||output_bytes!=(encrypt?40u:32u)||
     kfk_overlap(input,input_bytes,output,output_bytes))return (int)kIOReturnBadArgument;
 if(cls!=6&&cls!=7&&cls!=8&&cls!=9&&cls!=10&&cls!=11)return (int)kIOReturnUnsupported;
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
 /* AK/AKU are available only in the actual unlocked, authenticated
  * factory bag. Existing CK/DK/CKU/DKU derivation and ciphertext unchanged. */
 if(!error&&(cls==6||cls==9)&&(!kbs_ready||!kbs_bootstrap_done||!kbs_unlocked||!kbs_factory_locked()))error=(int)kIOReturnNotPermitted;
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

int
kebab_file_keys_prepare(const uint8_t *secret, unsigned bytes)
{
 if (!kfk_context() || !secret || bytes!=32 || !g_crypto_funcs ||
     !g_crypto_funcs->ccsha256_di || !g_crypto_funcs->cchmac_fn) return (int)kIOReturnBadArgument;
 (void)kcb_math_selftest();
 /* A separate domain from the root-record witness. Class keys are random
  * per bag, never derived directly from a stable volume UUID. */
 static const uint8_t domain[]="UniXNU.kebab.keybag-KEK.v1";
 uint8_t kek[32];
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,bytes,secret,sizeof(domain),domain,kek);
 lck_mtx_lock(&kfk_lock);
 kfk_wipe(kfk_root_kek,sizeof(kfk_root_kek));kfk_wipe(kfk_class_d,sizeof(kfk_class_d));kfk_wipe(kfk_uuid,sizeof(kfk_uuid));
 kdb_clear_locked();memcpy(kfk_root_kek,kek,32);kfk_pending=true;kfk_ready=false;kfk_bag_ready=false;
 lck_mtx_unlock(&kfk_lock);kfk_wipe(kek,sizeof(kek));
 return 0;
}

int
kebab_file_keys_commit(void)
{
 if (!kfk_context()) return (int)kIOReturnNotPermitted;
 lck_mtx_lock(&kfk_lock);
 int error=kfk_pending?0:(int)kIOReturnNotReady;
 if (!error) { kfk_pending=false;kfk_ready=true; }
 lck_mtx_unlock(&kfk_lock);return error;
}

bool
kebab_file_keys_available(void)
{
 if(!kfk_context())return false;
 lck_mtx_lock(&kfk_lock);bool ready=kfk_ready;lck_mtx_unlock(&kfk_lock);return ready;
}

void
kebab_file_keys_cancel(void)
{
 lck_mtx_lock(&kfk_lock);
 kfk_ready=kfk_pending=kfk_bag_ready=false;kdb_clear_locked();
 kfk_wipe(kfk_root_kek,sizeof(kfk_root_kek));kfk_wipe(kfk_class_d,sizeof(kfk_class_d));kfk_wipe(kfk_uuid,sizeof(kfk_uuid));
 lck_mtx_unlock(&kfk_lock);
}

int
kebab_keybag_d_create(const uint8_t uuid[16], void *output, unsigned capacity)
{
 if (!kfk_context() || !kfk_range(uuid,16) || !kfk_range(output,capacity) || capacity<88 || capacity>4096 ||
     kfk_overlap(uuid,16,output,capacity)) return (int)kIOReturnBadArgument;
 uint8_t plain[80]={0},sealed[88]={0};
 memcpy(plain,"KB01",4);plain[4]=1;plain[6]=0x10;memcpy(plain+8,uuid,16);
 lck_mtx_lock(&kfk_lock);
 int error=(int)kIOReturnNotReady;
 if (kfk_ready) {
  read_random(plain+24,48); /* Independent epoch and class D key. */
  error=kfk_wrap(kfk_root_kek,plain,sizeof(plain),sealed,true);
  if (!error) memcpy(output,sealed,sizeof(sealed));
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));return error;
}

int
kebab_keybag_d_open(const uint8_t uuid[16], const void *input, unsigned bytes)
{
 if (!kfk_context() || !kfk_range(uuid,16) || !kfk_range(input,bytes) || bytes!=88) return (int)kIOReturnBadArgument;
 uint8_t plain[80]={0};
 lck_mtx_lock(&kfk_lock);
 int error=kfk_ready?kfk_wrap(kfk_root_kek,input,bytes,plain,false):(int)kIOReturnNotReady;
 if (!error && (memcmp(plain,"KB01\1\0\20\0",8) || memcmp(plain+8,uuid,16))) error=(int)kIOReturnNotPermitted;
 for(unsigned i=72;!error && i<80;++i)if(plain[i])error=(int)kIOReturnNotPermitted;
 if (!error) { memcpy(kfk_class_d,plain+40,32);memcpy(kfk_uuid,uuid,16);kfk_bag_ready=true; }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(plain,sizeof(plain));return error;
}

static int
kfk_credential(const struct kebab_aks_credential *cred, uint32_t cls)
{
 if (!kfk_range(cred,sizeof(*cred))) return (int)kIOReturnBadArgument;
 if (cls!=4) return (int)kIOReturnUnsupported;
 if (!kfk_ready || !kfk_bag_ready) return (int)kIOReturnNotReady;
 if (memcmp(cred->bytes+22,kfk_uuid,16)) return (int)kIOReturnNotPermitted;
 for(unsigned i=8;i<12;++i)if(cred->bytes[i])return (int)kIOReturnUnsupported;
 for(unsigned i=38;i<80;++i)if(cred->bytes[i])return (int)kIOReturnUnsupported;
 return 0;
}

static bool
kfk_raw(struct kebab_aks_raw_key *raw)
{
 return kfk_range(raw,sizeof(*raw)) && raw->key_len>=32 && raw->key_len<=64 &&
     raw->iv_key_len>=16 && raw->iv_key_len<=64 && kfk_range(raw->key,raw->key_len) &&
     kfk_range(raw->iv_key,raw->iv_key_len) && !kfk_overlap(raw->key,raw->key_len,raw->iv_key,raw->iv_key_len);
}

static bool
kfk_output_alias(const struct kebab_aks_credential *cred, struct kebab_aks_raw_key *raw,
    const struct kebab_aks_wrapped_key *wrapped)
{
 const void *headers[]={cred,raw,wrapped};
 const size_t sizes[]={sizeof(*cred),sizeof(*raw),sizeof(*wrapped)};
 for(unsigned i=0;i<3;++i) {
  if (!kfk_range(headers[i],sizes[i]) || kfk_overlap(raw->key,raw->key_len,headers[i],sizes[i]) ||
      kfk_overlap(raw->iv_key,raw->iv_key_len,headers[i],sizes[i]) ||
      kfk_overlap(wrapped->key,wrapped->key_len,headers[i],sizes[i])) return true;
 }
 return false;
}

static void
kfk_plain(uint8_t out[88], const struct kebab_aks_credential *cred, const uint8_t raw[48])
{
 memset(out,0,88);memcpy(out,"KF01\1\0",6);memcpy(out+6,cred->bytes+20,2);
 memcpy(out+8,cred->bytes+22,16);memcpy(out+24,cred->bytes,8);out[32]=4;out[36]=32;out[38]=16;
 memcpy(out+40,raw,48);
}

static bool
kfk_identity(const uint8_t in[88], const struct kebab_aks_credential *cred)
{
 uint8_t expected[88];uint8_t zero[48]={0};kfk_plain(expected,cred,zero);
 unsigned diff=0;for(unsigned i=0;i<40;++i)diff|=in[i]^expected[i];kfk_wipe(expected,sizeof(expected));
 return diff==0;
}

int
kebab_file_key_new(const struct kebab_aks_credential *cred, uint32_t cls,
    struct kebab_aks_raw_key *raw, struct kebab_aks_wrapped_key *wrapped)
{
 if (!kfk_context() || !kfk_raw(raw) || !kfk_range(wrapped,sizeof(*wrapped)) ||
     wrapped->key_len<96 || wrapped->key_len>128 || !kfk_range(wrapped->key,wrapped->key_len) ||
     kfk_output_alias(cred,raw,wrapped) ||
     kfk_overlap(raw->key,raw->key_len,wrapped->key,wrapped->key_len) ||
     kfk_overlap(raw->iv_key,raw->iv_key_len,wrapped->key,wrapped->key_len)) return (int)kIOReturnBadArgument;
 uint8_t material[48]={0},plain[88]={0},sealed[96]={0};
 lck_mtx_lock(&kfk_lock);int error=kfk_credential(cred,cls);
 if (!error) {
  read_random(material,sizeof(material));kfk_plain(plain,cred,material);
  error=kfk_wrap(kfk_class_d,plain,sizeof(plain),sealed,true);
  if (!error) {
   memcpy(wrapped->key,sealed,96);wrapped->key_len=96;wrapped->dp_class=4;
   memcpy(raw->key,material,32);memcpy(raw->iv_key,material+32,16);
   raw->key_len=32;raw->iv_key_len=16;raw->flags=0;
  }
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(material,sizeof(material));kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));return error;
}

int
kebab_file_key_unwrap(const struct kebab_aks_credential *cred, const struct kebab_aks_wrapped_key *wrapped,
    struct kebab_aks_raw_key *raw)
{
 if (!kfk_context() || !kfk_raw(raw) || !kfk_range(wrapped,sizeof(*wrapped)) ||
     wrapped->key_len!=96 || !kfk_range(wrapped->key,96) ||
     kfk_output_alias(cred,raw,wrapped) ||
     kfk_overlap(raw->key,raw->key_len,wrapped->key,96) ||
     kfk_overlap(raw->iv_key,raw->iv_key_len,wrapped->key,96)) return (int)kIOReturnBadArgument;
 uint8_t plain[88]={0};
 lck_mtx_lock(&kfk_lock);int error=kfk_credential(cred,wrapped->dp_class);
 if (!error) error=kfk_wrap(kfk_class_d,wrapped->key,96,plain,false);
 if (!error && !kfk_identity(plain,cred)) error=(int)kIOReturnNotPermitted;
 if (!error) {
  memcpy(raw->key,plain+40,32);memcpy(raw->iv_key,plain+72,16);raw->key_len=32;raw->iv_key_len=16;raw->flags=0;
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(plain,sizeof(plain));return error;
}

int
kebab_file_key_rewrap(const struct kebab_aks_credential *cred, uint32_t cls,
    const struct kebab_aks_wrapped_key *input, struct kebab_aks_wrapped_key *output)
{
 if (!kfk_context() || !kfk_range(input,sizeof(*input)) || !kfk_range(output,sizeof(*output)) ||
     input->key_len!=96 || output->key_len<96 || output->key_len>128 || !kfk_range(input->key,96) ||
     !kfk_range(output->key,output->key_len)) return (int)kIOReturnBadArgument;
 if (!kfk_range(cred,sizeof(*cred)) || kfk_overlap(output->key,output->key_len,cred,sizeof(*cred)) ||
     kfk_overlap(output->key,output->key_len,input,sizeof(*input)) ||
     kfk_overlap(output->key,output->key_len,output,sizeof(*output)) ||
     kfk_overlap(input->key,96,input,sizeof(*input)) || kfk_overlap(input->key,96,output,sizeof(*output))) return (int)kIOReturnBadArgument;
 uint8_t plain[88]={0};lck_mtx_lock(&kfk_lock);int error=kfk_credential(cred,cls);
 if (!error && input->dp_class!=4) error=(int)kIOReturnUnsupported;
 if (!error) error=kfk_wrap(kfk_class_d,input->key,96,plain,false);
 if (!error && !kfk_identity(plain,cred)) error=(int)kIOReturnNotPermitted;
 if (!error) { memmove(output->key,input->key,96);output->key_len=96;output->dp_class=4; }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(plain,sizeof(plain));return error;
}

/* Private volume-metadata envelopes for the audited +0x38/+0x48 descriptor
 * boundary. Numeric14 only, metadata credentials only, no AKS publication or
 * Class C unlock claim. This is a HiOS format, not Apple's SEP serialization. */
static bool
kfk_meta_credential(const struct kebab_aks_credential *cred)
{
 if(!kfk_range(cred,sizeof(*cred)))return false;
 unsigned nonzero=0;
 for(unsigned i=0;i<80;++i) {
  if(i>=22 && i<38)nonzero|=cred->bytes[i];
  else if(cred->bytes[i])return false;
 }
 return nonzero!=0;
}

static bool
kfk_meta_buffers(const struct kebab_aks_credential *cred, const struct kebab_aks_raw_key *raw,
    const struct kebab_aks_wrapped_key *wrapped)
{
 if(!kfk_range(cred,sizeof(*cred)) || !kfk_range(raw,sizeof(*raw)) ||
     !kfk_range(wrapped,sizeof(*wrapped)) || raw->key_len<16 || raw->key_len>64 ||
     raw->iv_key_len>64 || !kfk_range(raw->key,raw->key_len) ||
     (raw->iv_key_len && !kfk_range(raw->iv_key,raw->iv_key_len)) ||
     wrapped->key_len<64 || wrapped->key_len>128 || !kfk_range(wrapped->key,wrapped->key_len))return false;
 const void *regions[]={cred,raw,wrapped,raw->key,raw->iv_key,wrapped->key};
 const size_t sizes[]={sizeof(*cred),sizeof(*raw),sizeof(*wrapped),raw->key_len,raw->iv_key_len,wrapped->key_len};
 for(unsigned i=0;i<6;++i)for(unsigned j=i+1;j<6;++j)
  if(sizes[i] && sizes[j] && kfk_overlap(regions[i],sizes[i],regions[j],sizes[j]))return false;
 return true;
}

static bool
kfk_meta_length(unsigned key, unsigned iv)
{
 return (key==16 || key==32 || key==64) && (iv==0 || iv==16);
}

static int
kfk_meta_kek(const struct kebab_aks_credential *cred, uint8_t output[32])
{
 if(!kfk_ready || !g_crypto_funcs || !g_crypto_funcs->ccsha256_di ||
     !g_crypto_funcs->cchmac_fn)return (int)kIOReturnNotReady;
 static const uint8_t domain[]="UniXNU.kebab.metadata-KEK.v1";
 uint8_t input[sizeof(domain)+16];
 memcpy(input,domain,sizeof(domain));memcpy(input+sizeof(domain),cred->bytes+22,16);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(input),input,output);
 kfk_wipe(input,sizeof(input));return 0;
}

static void
kfk_meta_header(uint8_t output[40], const struct kebab_aks_credential *cred, unsigned key, unsigned iv)
{
 bzero(output,40);memcpy(output,"KM01\1\0",6);memcpy(output+8,cred->bytes+22,16);
 output[32]=14;output[36]=(uint8_t)key;output[38]=(uint8_t)iv;
}

int
kebab_metadata_key_wrap(const struct kebab_aks_credential *cred, uint32_t selector,
    const struct kebab_aks_raw_key *raw, struct kebab_aks_wrapped_key *wrapped)
{
 if(!kfk_context() || !kfk_meta_buffers(cred,raw,wrapped))return (int)kIOReturnBadArgument;
 if(selector!=14 || !kfk_meta_credential(cred) || raw->flags ||
     !kfk_meta_length(raw->key_len,raw->iv_key_len))return (int)kIOReturnUnsupported;
 size_t bytes=40u+raw->key_len+raw->iv_key_len;
 if(wrapped->key_len<bytes+8)return (int)kIOReturnBadArgument;
 uint8_t plain[120]={0},sealed[128]={0},kek[32]={0};
 kfk_meta_header(plain,cred,raw->key_len,raw->iv_key_len);
 memcpy(plain+40,raw->key,raw->key_len);
 if(raw->iv_key_len)memcpy(plain+40+raw->key_len,raw->iv_key,raw->iv_key_len);
 lck_mtx_lock(&kfk_lock);int error=kfk_meta_kek(cred,kek);
 if(!error)error=kfk_wrap(kek,plain,bytes,sealed,true);
 if(!error){memcpy(wrapped->key,sealed,bytes+8);wrapped->key_len=(uint32_t)(bytes+8);wrapped->dp_class=14;}
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(kek,sizeof(kek));return error;
}

int
kebab_metadata_key_unwrap(const struct kebab_aks_credential *cred,
    const struct kebab_aks_wrapped_key *wrapped, struct kebab_aks_raw_key *raw)
{
 if(!kfk_context() || !kfk_meta_buffers(cred,raw,wrapped))return (int)kIOReturnBadArgument;
 if(wrapped->dp_class!=14 || !kfk_meta_credential(cred))return (int)kIOReturnUnsupported;
 uint8_t plain[120]={0},expected[40]={0},kek[32]={0};
 lck_mtx_lock(&kfk_lock);int error=kfk_meta_kek(cred,kek);
 if(!error)error=kfk_wrap(kek,wrapped->key,wrapped->key_len,plain,false);
 unsigned key=plain[36],iv=plain[38];
 if(!error && (!kfk_meta_length(key,iv) || wrapped->key_len!=40u+key+iv+8 ||
     raw->key_len<key || raw->iv_key_len<iv))error=(int)kIOReturnBadArgument;
 if(!error) {
  kfk_meta_header(expected,cred,key,iv);unsigned difference=0;
  for(unsigned i=0;i<40;++i)difference|=plain[i]^expected[i];
  if(difference)error=(int)kIOReturnNotPermitted;
 }
 if(!error) {
  memcpy(raw->key,plain+40,key);if(iv)memcpy(raw->iv_key,plain+40+key,iv);
  raw->key_len=key;raw->iv_key_len=iv;raw->flags=0;
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(plain,sizeof(plain));kfk_wipe(expected,sizeof(expected));kfk_wipe(kek,sizeof(kek));return error;
}

int
kebab_metadata_keys_selftest(void)
{
 struct { uint8_t key[64],iv[16],sealed[128],back[64],back_iv[16]; } *work=kalloc_data(sizeof(*work),Z_WAITOK|Z_ZERO);
 if(!work)return (int)kIOReturnNoMemory;
 struct kebab_aks_credential cred={{0}};for(unsigned i=0;i<16;++i)cred.bytes[22+i]=(uint8_t)(0x30+i);
 for(unsigned i=0;i<64;++i)work->key[i]=(uint8_t)i;
 for(unsigned i=0;i<16;++i)work->iv[i]=(uint8_t)(0x90+i);
 int error=0;
 for(unsigned index=0;!error && index<3;++index)for(unsigned has_iv=0;!error && has_iv<2;++has_iv) {
  const unsigned lengths[]={16,32,64};unsigned bytes=lengths[index],iv=has_iv?16u:0u;
  struct kebab_aks_raw_key input={work->key,bytes,work->iv,iv,0};
  struct kebab_aks_wrapped_key wrapped={work->sealed,128,0};
  struct kebab_aks_raw_key output={work->back,64,work->back_iv,16,0};
  error=kebab_metadata_key_wrap(&cred,14,&input,&wrapped);
  if(!error)error=kebab_metadata_key_unwrap(&cred,&wrapped,&output);
  if(!error && (output.key_len!=bytes || output.iv_key_len!=iv ||
      memcmp(work->key,work->back,bytes) || (iv && memcmp(work->iv,work->back_iv,iv))))error=(int)kIOReturnError;
  if(!error) {
   work->sealed[0]^=1;memset(work->back,0xa5,64);output.key_len=64;output.iv_key_len=16;
   int rejected=kebab_metadata_key_unwrap(&cred,&wrapped,&output);work->sealed[0]^=1;
   if(!rejected)error=(int)kIOReturnError;
   for(unsigned i=0;i<64;++i)if(work->back[i]!=0xa5)error=(int)kIOReturnError;
  }
 }
 kfk_wipe(work,sizeof(*work));kfree_data(work,sizeof(*work));
 printf("KEBAB METADATA KEYS: Root-backed numeric14 volume wrap/unwrap/tamper %s (%d); no AKS publication\n",error?"FAIL":"PASS",error);
 return error;
}
/* HiOS volume-key envelope for the original FileVault descriptor boundary.
 * No FileVault service, state/unlock claim, or persistent volume is published.
 * Initial zero credential and absent auxiliary/passphrase inputs only. */
static bool
kfv_credential(const struct kebab_fv_credential *cred)
{
 if(!kfk_range(cred,sizeof(*cred)))return false;
 for(unsigned i=0;i<sizeof(*cred);++i)if(cred->bytes[i])return false;
 return true;
}

static bool
kfv_buffers(const struct kebab_fv_credential *cred, const struct kebab_fv_wrapped_key *wrapped,
    const struct kebab_fv_raw_key *raw, bool create)
{
 if(!kfk_range(cred,sizeof(*cred)) || !kfk_range(wrapped,sizeof(*wrapped)) ||
    !kfk_range(raw,sizeof(*raw)) || wrapped->key_len<96 ||
    wrapped->key_len>(create?512u:96u) || raw->key_len<32 || raw->key_len>64 ||
    !kfk_range(wrapped->key,(size_t)wrapped->key_len) || !kfk_range(raw->key,(size_t)raw->key_len))return false;
 const void *p[]={cred,wrapped,raw,wrapped->key,raw->key};
 const size_t n[]={sizeof(*cred),sizeof(*wrapped),sizeof(*raw),(size_t)wrapped->key_len,(size_t)raw->key_len};
 for(unsigned i=0;i<5;++i)for(unsigned j=i+1;j<5;++j)
  if(kfk_overlap(p[i],n[i],p[j],n[j]))return false;
 return true;
}

static int
kfv_kek(const uint8_t uuid[16], uint8_t output[32])
{
 if(!kfk_ready || !g_crypto_funcs || !g_crypto_funcs->ccsha256_di ||
    !g_crypto_funcs->cchmac_fn)return (int)kIOReturnNotReady;
 static const uint8_t domain[]="UniXNU.kebab.volume-VEK.v1";
 uint8_t input[sizeof(domain)+16];
 memcpy(input,domain,sizeof(domain));memcpy(input+sizeof(domain),uuid,16);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(input),input,output);
 kfk_wipe(input,sizeof(input));return 0;
}

/* Internal helpers require kfk_lock; both public representations commit under
 * the same Root epoch. No nested public calls or unlock/relock window. */
static bool
kfv_new_uuid_valid(const uint8_t uuid[16],const struct kebab_fv_credential *cred,
    const struct kebab_fv_wrapped_key *wrapped,const struct kebab_fv_raw_key *raw)
{
 if(!kfk_range(uuid,16))return false;
 unsigned nonzero=0;for(unsigned i=0;i<16;++i)nonzero|=uuid[i];
 return nonzero && !kfk_overlap(uuid,16,cred,sizeof(*cred)) &&
  !kfk_overlap(uuid,16,wrapped,sizeof(*wrapped)) && !kfk_overlap(uuid,16,raw,sizeof(*raw)) &&
  !kfk_overlap(uuid,16,wrapped->key,(size_t)wrapped->key_len) &&
  !kfk_overlap(uuid,16,raw->key,(size_t)raw->key_len);
}

static int
kfv_generate_locked(const uint8_t uuid[16],uint8_t sealed[96],uint8_t key[32])
{
 uint8_t plain[72]={0},kek[32]={0};
 memcpy(plain,"KV01\1\0",6);memcpy(plain+8,uuid,16);plain[36]=32;memcpy(sealed,uuid,16);
 int error=kfv_kek(uuid,kek);
 if(!error) {
  read_random(plain+24,8);read_random(plain+40,32);
  error=kfk_wrap(kek,plain,72,sealed+16,true);
 }
 if(!error)memcpy(key,plain+40,32);
 kfk_wipe(plain,sizeof(plain));kfk_wipe(kek,sizeof(kek));return error;
}

static int
kfv_open_locked(const uint8_t sealed[96],uint8_t key[32])
{
 uint8_t plain[72]={0},header[40]={0},kek[32]={0};
 int error=kfv_kek(sealed,kek);
 if(!error)error=kfk_wrap(kek,sealed+16,80,plain,false);
 if(!error) {
  memcpy(header,"KV01\1\0",6);memcpy(header+8,sealed,16);memcpy(header+24,plain+24,8);header[36]=32;
  unsigned difference=0,nonzero=0;
  for(unsigned i=0;i<40;++i)difference|=header[i]^plain[i];
  for(unsigned i=0;i<16;++i)nonzero|=sealed[i];
  if(difference || !nonzero)error=(int)kIOReturnNotPermitted;
 }
 if(!error)memcpy(key,plain+40,32);
 kfk_wipe(plain,sizeof(plain));kfk_wipe(header,sizeof(header));kfk_wipe(kek,sizeof(kek));return error;
}

int
kebab_volume_key_new(const struct kebab_fv_credential *cred,const void *auxiliary,
    const void *passphrase,const uint8_t uuid[16],struct kebab_fv_wrapped_key *wrapped,
    struct kebab_fv_raw_key *raw)
{
 if(!kfk_context() || !kfv_buffers(cred,wrapped,raw,true) ||
    !kfv_new_uuid_valid(uuid,cred,wrapped,raw))return (int)kIOReturnBadArgument;
 if(!kfv_credential(cred) || auxiliary || passphrase || raw->flags)return (int)kIOReturnUnsupported;
 uint8_t volume[16],sealed[96]={0},key[32]={0};memcpy(volume,uuid,16);
 lck_mtx_lock(&kfk_lock);int error=kfv_generate_locked(volume,sealed,key);
 if(!error) {
  memcpy(wrapped->key,sealed,96);memcpy(raw->key,key,32);
  wrapped->key_len=96;raw->key_len=32;raw->flags=0;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(volume,sizeof(volume));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));return error;
}

int
kebab_volume_key_unwrap(const struct kebab_fv_credential *cred,const void *auxiliary,
    const void *passphrase,const struct kebab_fv_wrapped_key *wrapped,struct kebab_fv_raw_key *raw)
{
 if(!kfk_context() || !kfv_buffers(cred,wrapped,raw,false))return (int)kIOReturnBadArgument;
 if(!kfv_credential(cred) || auxiliary || passphrase)return (int)kIOReturnUnsupported;
 uint8_t sealed[96],key[32]={0};memcpy(sealed,wrapped->key,96);
 lck_mtx_lock(&kfk_lock);int error=kfv_open_locked(sealed,key);
 if(!error){memcpy(raw->key,key,32);raw->key_len=32;raw->flags=0;}
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));return error;
}

int
kebab_volume_keys_selftest(void)
{
 struct Work {uint8_t sealed[96],key[64],back[64];};
 struct Work *work=kalloc_data(sizeof(*work),Z_WAITOK|Z_ZERO);if(!work)return (int)kIOReturnNoMemory;
 struct kebab_fv_credential cred={{0}};uint8_t uuid[16];for(unsigned i=0;i<16;++i)uuid[i]=(uint8_t)(0xb0+i);
 struct kebab_fv_wrapped_key wrapped={work->sealed,96};
 struct kebab_fv_raw_key raw={work->key,64,0},output={work->back,64,0};
 int error=kebab_volume_key_new(&cred,NULL,NULL,uuid,&wrapped,&raw);
 if(!error)error=kebab_volume_key_unwrap(&cred,NULL,NULL,&wrapped,&output);
 if(!error && (raw.key_len!=32 || output.key_len!=32 || output.flags || memcmp(work->key,work->back,32)))error=(int)kIOReturnError;
 if(!error) {
  work->sealed[0]^=1;memset(work->back,0xa5,64);output.key_len=64;output.flags=0xdead;
  if(!kebab_volume_key_unwrap(&cred,NULL,NULL,&wrapped,&output) || output.key_len!=64 || output.flags!=0xdead)error=(int)kIOReturnError;
  for(unsigned i=0;i<64;++i)if(work->back[i]!=0xa5)error=(int)kIOReturnError;
 }
 kfk_wipe(work,sizeof(*work));kfree_data(work,sizeof(*work));kfk_wipe(uuid,sizeof(uuid));
 printf("KEBAB VOLUME KEYS: Root/UUID-backed VEK32 envelope/tamper %s (%d); no FileVault publication or Data format\n",error?"FAIL":"PASS",error);
 return error;
}

/* Original FV +0x20 selector1 requests wrapped-VEK capacity. Other selectors
 * are not inferred from the separate APFS container operation selector2. */
int
kebab_volume_key_length(const struct kebab_fv_credential *cred, uint32_t selector, uint64_t *output)
{
 if(!kfk_context() || !kfk_range(cred,sizeof(*cred)) || !kfk_range(output,sizeof(*output)) ||
    ((uintptr_t)output&7) || kfk_overlap(cred,sizeof(*cred),output,sizeof(*output)))return (int)kIOReturnBadArgument;
 if(!kfv_credential(cred) || selector!=1)return (int)kIOReturnUnsupported;
 lck_mtx_lock(&kfk_lock);int error=kfk_ready?0:(int)kIOReturnNotReady;
 if(!error)*output=96;
 lck_mtx_unlock(&kfk_lock);return error;
}
/* Original FV state/control ABI for our authenticated vendor VEK format.
 * Factory no-passcode C/D policy only. No foreign blob, passcode, SEP capability
 * or unknown control is acknowledged. One active owner volume at a time. */
static uint8_t kdc_uuid[16],kdc_record_hash[32],kdc_c[32],kdc_d[32];
static bool kdc_registered,kdc_loaded,kdc_native_factory;

static void
kdc_clear_locked(void)
{
 kdc_registered=kdc_loaded=kdc_native_factory=false;kfk_wipe(kdc_uuid,16);kfk_wipe(kdc_record_hash,32);
 kfk_wipe(kdc_c,32);kfk_wipe(kdc_d,32);
}

static bool
kdc_parameters(const struct kebab_fv_credential *parameters)
{
 if(!parameters)return true; /* Original class-unload caller passes NULL. */
 if(!kfk_range(parameters,sizeof(*parameters)))return false;
 unsigned nonzero=0;for(unsigned i=0;i<sizeof(*parameters);++i)nonzero|=parameters->bytes[i];
 return nonzero==0;
}

static int
kdc_authenticated_locked(const uint8_t sealed[96],uint8_t key[32])
{
 if(!kfk_ready||!kdb_ready)return (int)kIOReturnNotReady;
 return kfv_open_locked(sealed,key);
}

static void
kdc_derive_locked(const uint8_t uuid[16],uint32_t cls,uint8_t output[32])
{
 static const uint8_t domain[]="UniXNU.kebab.volume-class.v1";
 uint8_t context[sizeof(domain)+36];
 memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),kdb_identifier,16);
 memcpy(context+sizeof(domain)+16,uuid,16);memcpy(context+sizeof(domain)+32,&cls,4);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,cls==3?kdb_class_c:kdb_class_d,
     sizeof(context),context,output);kfk_wipe(context,sizeof(context));
}

/* Trusted boot policy invokes this ONLY after the pinned real Data has
 * mounted through original APFS and authenticated its encrypted root tree.
 * Authenticated device bank policy1 is factory/no-passcode C/D. This is not an
 * Apple FV blob/SEP unlock and cannot take over a different live FV owner. */
int
kebab_volume_factory_activate(const uint8_t uuid[16])
{
 static const uint8_t owner[16]={0x61,0x70,0x66,0x73,0x75,0x75,0x69,0x64,0x00,0x40,0x76,0x6f,0x6c,0x75,0x6d,0x01};
 if(!kfk_context()||!kfk_range(uuid,16))return (int)kIOReturnBadArgument;
 if(memcmp(uuid,owner,16))return (int)kIOReturnNotPermitted;
 uint8_t c[32]={0},d[32]={0};
 lck_mtx_lock(&kfk_lock);int error=(int)kIOReturnNotReady;
 if(kfk_ready&&kdb_ready){
  error=0;
  if(kdc_registered&&(!kdc_native_factory||memcmp(uuid,kdc_uuid,16)))error=(int)kIOReturnNotPermitted;
  if(!error){
   kdc_derive_locked(uuid,3,c);kdc_derive_locked(uuid,4,d);
   memcpy(kdc_c,c,32);memcpy(kdc_d,d,32);memcpy(kdc_uuid,uuid,16);
   kfk_wipe(kdc_record_hash,32);kdc_registered=kdc_loaded=kdc_native_factory=true;
  }
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(c,32);kfk_wipe(d,32);return error;
}

bool
kebab_volume_class_available(const uint8_t uuid[16],uint32_t cls)
{
 if(!kfk_context()||!kfk_range(uuid,16)||(cls!=3&&cls!=4))return false;
 lck_mtx_lock(&kfk_lock);
 bool ready=kfk_ready&&kdb_ready&&kdc_registered&&kdc_loaded&&!memcmp(uuid,kdc_uuid,16);
 lck_mtx_unlock(&kfk_lock);return ready;
}

int
kebab_volume_blob_state(const struct kebab_fv_credential *parameters,
    const struct kebab_fv_wrapped_key *wrapped,struct kebab_fv_blob_state *output)
{
 if(!kfk_context()||!kfk_range(wrapped,sizeof(*wrapped))||!kfk_range(output,sizeof(*output))||
     wrapped->key_len!=96||!kfk_range(wrapped->key,96))return (int)kIOReturnBadArgument;
 if(!kdc_parameters(parameters))return (int)kIOReturnUnsupported;
 if(kfk_overlap(output,sizeof(*output),wrapped,sizeof(*wrapped))||
     kfk_overlap(output,sizeof(*output),wrapped->key,96)||
     kfk_overlap(wrapped,sizeof(*wrapped),wrapped->key,96)||
     (parameters&&(kfk_overlap(output,sizeof(*output),parameters,sizeof(*parameters))||
     kfk_overlap(wrapped,sizeof(*wrapped),parameters,sizeof(*parameters))||
     kfk_overlap(wrapped->key,96,parameters,sizeof(*parameters)))))return (int)kIOReturnBadArgument;
 uint8_t sealed[96],key[32]={0};struct kebab_fv_blob_state state={{0}};
 memcpy(sealed,wrapped->key,96);lck_mtx_lock(&kfk_lock);
 int error=kdc_authenticated_locked(sealed,key);
 if(!error){
  const uint32_t type=1,version=1; /* Official FV state producer: VEK1, KEK2. */
  memcpy(state.bytes,&type,4);memcpy(state.bytes+4,sealed,16);memcpy(state.bytes+20,sealed,16);
  memcpy(state.bytes+40,&version,4);
  /* Flags/capabilities/SiDP fields remain zero: this backend has none of the
   * Apple SEP, credential, migration or hardware-volume features they encode.
   * Availability is a separate real loaded-state query, never an invented bit. */
  memcpy(output,&state,sizeof(state));
 }
 lck_mtx_unlock(&kfk_lock);kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));kfk_wipe(&state,sizeof(state));return error;
}

int
kebab_volume_set_protection(const struct kebab_fv_credential *parameters,uint32_t operation,uint64_t flags,
    const struct kebab_fv_wrapped_key *auxiliary,const struct kebab_fv_wrapped_key *passphrase,
    struct kebab_fv_wrapped_key *wrapped,struct kebab_fv_wrapped_key *result)
{
 if(!kfk_context())return (int)kIOReturnNotPermitted;
 if(operation!=5&&operation!=18&&operation!=19)return (int)kIOReturnUnsupported;
 if(flags||auxiliary||passphrase||result||!kdc_parameters(parameters))return (int)kIOReturnUnsupported;
 if(!kfk_range(wrapped,sizeof(*wrapped))||wrapped->key_len!=96||!kfk_range(wrapped->key,96)||
     kfk_overlap(wrapped,sizeof(*wrapped),wrapped->key,96)||
     (parameters&&(kfk_overlap(parameters,sizeof(*parameters),wrapped,sizeof(*wrapped))||
     kfk_overlap(parameters,sizeof(*parameters),wrapped->key,96))))return (int)kIOReturnBadArgument;
 uint8_t sealed[96],key[32]={0},hash[32]={0},c[32]={0},d[32]={0};
 memcpy(sealed,wrapped->key,96);
 SHA256_CTX digest;SHA256_Init(&digest);SHA256_Update(&digest,sealed,96);SHA256_Final(hash,&digest);
 lck_mtx_lock(&kfk_lock);
 int error=kdc_authenticated_locked(sealed,key);
 if(!error&&kdc_native_factory)error=(int)kIOReturnNotPermitted;
 if(!error&&kdc_registered&&(memcmp(kdc_uuid,sealed,16)||memcmp(kdc_record_hash,hash,32)))error=(int)kIOReturnNotPermitted;
 if(!error&&operation==19){
  if(!kdc_registered)error=(int)kIOReturnNotReady;
  else{kdc_loaded=false;kfk_wipe(kdc_c,32);kfk_wipe(kdc_d,32);}
 }else if(!error){
  /* Both commit and cold class-load establish actual UUID-bound keys from
   * independently random device classes in the authenticated durable bank. */
  kdc_derive_locked(sealed,3,c);kdc_derive_locked(sealed,4,d);
  memcpy(kdc_c,c,32);memcpy(kdc_d,d,32);memcpy(kdc_uuid,sealed,16);memcpy(kdc_record_hash,hash,32);
  kdc_registered=kdc_loaded=true;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));kfk_wipe(hash,sizeof(hash));
 kfk_wipe(c,sizeof(c));kfk_wipe(d,sizeof(d));kfk_wipe(&digest,sizeof(digest));return error;
}
/* Included after native Data class mapping definitions. No VFS operation is
 * invoked while holding kfk_lock. A separate event mutex serializes lock/
 * unlock broadcasts. The stock callback is a broadcast, not a per-FS ack. */
static LCK_MTX_DECLARE(kbs_event_lock,&kfk_group);

bool
kebab_apfs_b_raw_io_available(void)
{
 if(!kebab_apfs_b_volume_seen())return true; /* retain earlier raw32 probe scope */
 if(!kfk_context())return false;
 lck_mtx_lock(&kfk_lock);
 bool ready=kbs_factory_locked()&&kdc_registered&&kdc_loaded&&
     !memcmp(kbs_raw_bank,kdb_identifier,16)&&!memcmp(kbs_raw_volume,kdc_uuid,16);
 lck_mtx_unlock(&kfk_lock);return ready; /* B lock does not revoke already-open I/O */
}

int
kebab_device_keybag_set_locked(bool locked)
{
 if(!kfk_context())return (int)kIOReturnBadArgument;
 uint8_t volume[16]={0};uint64_t generation=0;
 lck_mtx_lock(&kbs_event_lock);lck_mtx_lock(&kfk_lock);
 int error=kbs_factory_locked()&&kbs_bootstrap_done&&kdc_registered&&kdc_loaded?0:(int)kIOReturnNotReady;
 if(!error){
  if(locked){kfk_wipe(kbs_private,32);kbs_unlocked=false;++kbs_generation;}
  else error=kbs_restore_locked();
  if(!error){memcpy(volume,kdc_uuid,16);generation=kbs_generation;}
 }
 lck_mtx_unlock(&kfk_lock);
 int broadcast=0;
 if(!error){
  broadcast=cp_key_store_action_for_volume(volume,locked?CP_ACTION_LOCKED:CP_ACTION_UNLOCKED);
  lck_mtx_lock(&kfk_lock);
  if(!kbs_factory_locked()||generation!=kbs_generation)error=(int)kIOReturnNotReady;
  else if(broadcast){
   error=(int)kIOReturnIOError;
   if(!locked){kfk_wipe(kbs_private,32);kbs_unlocked=false;++kbs_generation;}
  }
  lck_mtx_unlock(&kfk_lock);
 }
 lck_mtx_unlock(&kbs_event_lock);
 printf("KEBAB KEYBAG EVENT: requested=%s broadcast=%d result=%x; no per-filesystem ack claimed\n",locked?"locked":"unlocked",broadcast,error);
 kfk_wipe(volume,sizeof(volume));return error;
}
/* Authenticated HiOS opaque VEK object, not an Apple SEP blob or plaintext
 * labelled wrapped. Scope0 generic /3C/4D and Header24 are bound into the KEK; body40 is RFC3394(key32).
 * The native storage bridge alone unwraps this64B representation for AES-XTS. */
static int
kch_kek(const uint8_t header[24], uint8_t output[32])
{
 if(!kfk_ready || !g_crypto_funcs || !g_crypto_funcs->ccsha256_di ||
    !g_crypto_funcs->cchmac_fn)return (int)kIOReturnNotReady;
 uint32_t cls=0;memcpy(&cls,header+4,4);
 if(cls){
  if(cls!=3&&cls!=4)return (int)kIOReturnUnsupported;
  if(!kdb_ready||!kdc_registered||!kdc_loaded)return (int)kIOReturnNotReady;
  if(memcmp(header+8,kdc_uuid,16))return (int)kIOReturnNotPermitted;
  static const uint8_t domain[]="UniXNU.kebab.cpx-class-handle.v1";
  uint8_t context[sizeof(domain)+24];memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),header,24);
  g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,cls==3?kdc_c:kdc_d,sizeof(context),context,output);
  kfk_wipe(context,sizeof(context));return 0;
 }
 static const uint8_t domain[]="UniXNU.kebab.cpx-VEK-handle.v1";
 uint8_t input[sizeof(domain)+24];
 memcpy(input,domain,sizeof(domain));memcpy(input+sizeof(domain),header,24);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(input),input,output);
 kfk_wipe(input,sizeof(input));return 0;
}

/* Internal encoding requires kfk_lock and stable private inputs. */
static int
kch_create_scope_locked(const uint8_t uuid[16],uint32_t cls,const uint8_t key[32],uint8_t output[64])
{
 uint8_t token[64]={0},kek[32]={0};memcpy(token,"HT01",4);memcpy(token+4,&cls,4);memcpy(token+8,uuid,16);
 unsigned nonzero=0;for(unsigned i=8;i<24;++i)nonzero|=token[i];
 int error=nonzero?kch_kek(token,kek):(int)kIOReturnBadArgument;
 if(!error)error=kfk_wrap(kek,key,32,token+24,true);
 if(!error)memcpy(output,token,64);
 kfk_wipe(token,sizeof(token));kfk_wipe(kek,sizeof(kek));return error;
}

static int
kch_create_locked(const uint8_t uuid[16],const uint8_t key[32],uint8_t output[64])
{
 return kch_create_scope_locked(uuid,0,key,output);
}

static int
kch_class_create_locked(const uint8_t uuid[16],uint32_t cls,const uint8_t key[32],uint8_t output[64])
{
 if(cls!=3&&cls!=4)return (int)kIOReturnUnsupported;
 return kch_create_scope_locked(uuid,cls,key,output);
}

/* Internal HB01/64B CPX ciphertext, not an Apple SEP blob or on-disk KF03.
 * APFS receives no plaintext file key. The storage bridge opens this handle
 * only into its scoped I/O key buffer, whose destructor wipes it. The
 * original APFS FIODEVICELOCKED/open policy owns B descriptor admission;
 * already-open and publicly-created locked files must retain I/O access.
 * Persistent KF03 unwrap still requires the actual unlocked B private key.
 * Caller holds kfk_lock in *_locked helpers; no VFS operation under it. */
static int
khb_kek_locked(const uint8_t header[24],uint8_t output[32])
{
 uint32_t cls=0;memcpy(&cls,header+4,4);
 if(memcmp(header,"HB01",4)||cls!=2)return (int)kIOReturnUnsupported;
 if(!g_crypto_funcs||!g_crypto_funcs->ccsha256_di||!g_crypto_funcs->cchmac_fn)return (int)kIOReturnNotReady;
 if(!kbs_factory_locked()||!kbs_ready||!kbs_bootstrap_done||!kdc_registered||!kdc_loaded)return (int)kIOReturnNotReady;
 if(memcmp(header+8,kdc_uuid,16))return (int)kIOReturnNotPermitted;
 static const uint8_t domain[]="UniXNU.kebab.protected-unless-open.cpx.v1";
 uint8_t context[sizeof(domain)+80];
 memcpy(context,domain,sizeof(domain));memcpy(context+sizeof(domain),kdb_identifier,16);
 memcpy(context+sizeof(domain)+16,kdb_record+24,32);
 memcpy(context+sizeof(domain)+48,kdb_record+16,4);memcpy(context+sizeof(domain)+52,kdb_record+60,4);
 memcpy(context+sizeof(domain)+56,header,24);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(context),context,output);
 kfk_wipe(context,sizeof(context));return 0;
}

static int
khb_create_locked(const uint8_t uuid[16],const uint8_t key[32],uint8_t output[64])
{
 uint8_t token[64]={0},kek[32]={0};uint32_t cls=2;
 memcpy(token,"HB01",4);memcpy(token+4,&cls,4);memcpy(token+8,uuid,16);
 int error=khb_kek_locked(token,kek);
 if(!error)error=kfk_wrap(kek,key,32,token+24,true);
 if(!error)memcpy(output,token,64);
 kfk_wipe(token,sizeof(token));kfk_wipe(kek,sizeof(kek));return error;
}

/* Only called after public handle-open range/context/alias validation. */
static int
khb_open(const uint8_t input[64],uint8_t key[32])
{
 uint8_t token[64],kek[32]={0},plain[32]={0};memcpy(token,input,64);
 lck_mtx_lock(&kfk_lock);int error=khb_kek_locked(token,kek);
 if(!error)error=kfk_wrap(kek,token+24,40,plain,false);
 if(!error)memcpy(key,plain,32);
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(token,sizeof(token));kfk_wipe(kek,sizeof(kek));kfk_wipe(plain,sizeof(plain));
 static uint32_t reports;uint32_t n=__atomic_fetch_add(&reports,1u,__ATOMIC_RELAXED);
 if(error||n<16)printf("KEBAB B CPX: opaque64 -> io-local32 result=%x; APFS cache contains ciphertext, no key bytes logged\n",error);
 return error;
}

int
kebab_vek_cpx_handle_create(const uint8_t uuid[16],const uint8_t key[32],uint8_t output[64])
{
 if(!kfk_context() || !kfk_range(uuid,16) || !kfk_range(key,32) || !kfk_range(output,64) ||
    kfk_overlap(uuid,16,key,32) || kfk_overlap(uuid,16,output,64) ||
    kfk_overlap(key,32,output,64))return (int)kIOReturnBadArgument;
 lck_mtx_lock(&kfk_lock);int error=kch_create_locked(uuid,key,output);
 lck_mtx_unlock(&kfk_lock);return error;
}

int
kebab_vek_cpx_handle_open(const uint8_t input[64], uint8_t key[32])
{
 if(!kfk_context() || !kfk_range(input,64) || !kfk_range(key,32) ||
    kfk_overlap(input,64,key,32))return (int)kIOReturnBadArgument;
 if(!memcmp(input,"HB01",4))return khb_open(input,key);
 uint8_t token[64],kek[32]={0},plain[32]={0};memcpy(token,input,64);
 unsigned difference=0,nonzero=0;
 for(unsigned i=0;i<4;++i)difference|=token[i]^((const uint8_t *)"HT01")[i];
 uint32_t cls=0;memcpy(&cls,token+4,4);if(cls!=0&&cls!=3&&cls!=4)difference|=1;
 for(unsigned i=8;i<24;++i)nonzero|=token[i];
 int error=(int)kIOReturnUnsupported;
 if(!difference && nonzero) {
  lck_mtx_lock(&kfk_lock);error=kch_kek(token,kek);
  if(!error)error=kfk_wrap(kek,token+24,40,plain,false);
  if(!error)memcpy(key,plain,32);
  lck_mtx_unlock(&kfk_lock);
 }
 kfk_wipe(token,sizeof(token));kfk_wipe(kek,sizeof(kek));kfk_wipe(plain,sizeof(plain));return error;
}
/* Original FV raw descriptor now carries a real opaque wrapped object64, not
 * raw32 with a wrapped bit. Both WVEK and CPX object commit atomically under
 * one Root lock; state/lock/class policy is unchanged and remains unavailable. */
int
kebab_volume_key_new_wrapped(const struct kebab_fv_credential *cred,const void *auxiliary,
    const void *passphrase,const uint8_t uuid[16],struct kebab_fv_wrapped_key *wrapped,
    struct kebab_fv_raw_key *raw)
{
 if(!kfk_context() || !kfv_buffers(cred,wrapped,raw,true) || raw->key_len!=64 ||
    !kfv_new_uuid_valid(uuid,cred,wrapped,raw))return (int)kIOReturnBadArgument;
 if(!kfv_credential(cred) || auxiliary || passphrase || raw->flags)return (int)kIOReturnUnsupported;
 uint8_t volume[16],sealed[96]={0},key[32]={0},token[64]={0};memcpy(volume,uuid,16);
 lck_mtx_lock(&kfk_lock);int error=kfv_generate_locked(volume,sealed,key);
 if(!error)error=kch_create_locked(volume,key,token);
 if(!error) {
  memcpy(wrapped->key,sealed,96);memcpy(raw->key,token,64);
  wrapped->key_len=96;raw->key_len=64;raw->flags=KEBAB_RAW_KEY_WRAPPED;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(volume,sizeof(volume));kfk_wipe(sealed,sizeof(sealed));
 kfk_wipe(key,sizeof(key));kfk_wipe(token,sizeof(token));return error;
}

int
kebab_volume_key_unwrap_wrapped(const struct kebab_fv_credential *cred,const void *auxiliary,
    const void *passphrase,const struct kebab_fv_wrapped_key *wrapped,struct kebab_fv_raw_key *raw)
{
 if(!kfk_context() || !kfv_buffers(cred,wrapped,raw,false) || raw->key_len!=64)return (int)kIOReturnBadArgument;
 if(!kfv_credential(cred) || auxiliary || passphrase)return (int)kIOReturnUnsupported;
 uint8_t sealed[96],key[32]={0},token[64]={0};memcpy(sealed,wrapped->key,96);
 lck_mtx_lock(&kfk_lock);int error=kfv_open_locked(sealed,key);
 if(!error)error=kch_create_locked(sealed,key,token);
 if(!error){memcpy(raw->key,token,64);raw->key_len=64;raw->flags=KEBAB_RAW_KEY_WRAPPED;}
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));kfk_wipe(token,sizeof(token));return error;
}
/* Canonical original metadata creation: capacity64/IV16 in, opaque64/IV16
 * out, wrapped record40. Numeric14 Root and numeric3 loaded ClassC; no passcode/SEP claim.
 * Private KM01 codec remains separate and is not this native contract. */
static bool
kmd_buffers(const struct kebab_aks_credential *cred,const struct kebab_aks_raw_key *raw,
    const struct kebab_aks_wrapped_key *wrapped,bool create)
{
 if(!kfk_range(cred,sizeof(*cred)) || !kfk_range(raw,sizeof(*raw)) ||
    !kfk_range(wrapped,sizeof(*wrapped)) || raw->key_len!=64 || raw->iv_key_len!=16 ||
    wrapped->key_len<40 || wrapped->key_len>(create?128u:40u) ||
    !kfk_range(raw->key,64) || !kfk_range(raw->iv_key,16) ||
    !kfk_range(wrapped->key,wrapped->key_len))return false;
 const void *p[]={cred,raw,wrapped,raw->key,raw->iv_key,wrapped->key};
 const size_t n[]={sizeof(*cred),sizeof(*raw),sizeof(*wrapped),64,16,wrapped->key_len};
 for(unsigned i=0;i<6;++i)for(unsigned j=i+1;j<6;++j)
  if(kfk_overlap(p[i],n[i],p[j],n[j]))return false;
 return true;
}

static int
kmd_kek(const uint8_t uuid[16],uint32_t cls,uint8_t key[32])
{
 if(!kfk_ready || !g_crypto_funcs || !g_crypto_funcs->ccsha256_di ||
    !g_crypto_funcs->cchmac_fn)return (int)kIOReturnNotReady;
 if(cls==3){
  if(!kdb_ready||!kdc_registered||!kdc_loaded)return (int)kIOReturnNotReady;
  if(memcmp(uuid,kdc_uuid,16))return (int)kIOReturnNotPermitted;
  static const uint8_t domain[]="UniXNU.kebab.apfs-metadata-class.v1";
  uint8_t context[sizeof(domain)+20];memcpy(context,domain,sizeof(domain));
  memcpy(context+sizeof(domain),uuid,16);memcpy(context+sizeof(domain)+16,&cls,4);
  g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kdc_c,sizeof(context),context,key);
  kfk_wipe(context,sizeof(context));return 0;
 }
 if(cls!=14)return (int)kIOReturnUnsupported;
 static const uint8_t domain[]="UniXNU.kebab.apfs-metadata.v1";
 uint8_t input[sizeof(domain)+20];
 memcpy(input,domain,sizeof(domain));memcpy(input+sizeof(domain),uuid,16);
 bzero(input+sizeof(domain)+16,4);input[sizeof(domain)+16]=14;
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,kfk_root_kek,sizeof(input),input,key);
 kfk_wipe(input,sizeof(input));return 0;
}

static void
kmd_iv(const uint8_t uuid[16],const uint8_t key[32],uint8_t iv[16])
{
 static const uint8_t domain[]="UniXNU.kebab.apfs-metadata-IV.v1";
 uint8_t input[sizeof(domain)+16],digest[32];
 memcpy(input,domain,sizeof(domain));memcpy(input+sizeof(domain),uuid,16);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,key,sizeof(input),input,digest);
 memcpy(iv,digest,16);kfk_wipe(input,sizeof(input));kfk_wipe(digest,sizeof(digest));
}

int
kebab_metadata_key_create_wrapped(const struct kebab_aks_credential *cred,uint32_t selector,
    struct kebab_aks_raw_key *raw,struct kebab_aks_wrapped_key *wrapped)
{
 if(!kfk_context() || !kmd_buffers(cred,raw,wrapped,true))return (int)kIOReturnBadArgument;
 if((selector!=14 && selector!=3) || !kfk_meta_credential(cred) || raw->flags)return (int)kIOReturnUnsupported;
 uint8_t uuid[16],key[32]={0},kek[32]={0},iv[16]={0},sealed[40]={0},token[64]={0};memcpy(uuid,cred->bytes+22,16);
 lck_mtx_lock(&kfk_lock);int error=kmd_kek(uuid,selector,kek);
 if(!error){read_random(key,32);kmd_iv(uuid,key,iv);error=kfk_wrap(kek,key,32,sealed,true);}
 if(!error)error=selector==3?kch_class_create_locked(uuid,3,key,token):kch_create_locked(uuid,key,token);
 if(!error) {
  memcpy(wrapped->key,sealed,40);memcpy(raw->key,token,64);memcpy(raw->iv_key,iv,16);
  wrapped->key_len=40;wrapped->dp_class=selector;raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(uuid,sizeof(uuid));kfk_wipe(key,sizeof(key));kfk_wipe(kek,sizeof(kek));
 kfk_wipe(iv,sizeof(iv));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(token,sizeof(token));return error;
}

int
kebab_metadata_key_unwrap_wrapped(const struct kebab_aks_credential *cred,
    const struct kebab_aks_wrapped_key *wrapped,struct kebab_aks_raw_key *raw)
{
 if(!kfk_context() || !kmd_buffers(cred,raw,wrapped,false))return (int)kIOReturnBadArgument;
 if((wrapped->dp_class!=14 && wrapped->dp_class!=3) || !kfk_meta_credential(cred))return (int)kIOReturnUnsupported;
 uint8_t uuid[16],sealed[40],key[32]={0},kek[32]={0},iv[16]={0},token[64]={0};
 memcpy(uuid,cred->bytes+22,16);memcpy(sealed,wrapped->key,40);
 lck_mtx_lock(&kfk_lock);int error=kmd_kek(uuid,wrapped->dp_class,kek);
 if(!error)error=kfk_wrap(kek,sealed,40,key,false);
 if(!error){kmd_iv(uuid,key,iv);error=wrapped->dp_class==3?kch_class_create_locked(uuid,3,key,token):kch_create_locked(uuid,key,token);}
 if(!error) {
  memcpy(raw->key,token,64);memcpy(raw->iv_key,iv,16);
  raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(uuid,sizeof(uuid));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(key,sizeof(key));
 kfk_wipe(kek,sizeof(kek));kfk_wipe(iv,sizeof(iv));kfk_wipe(token,sizeof(token));return error;
}
/* Native APFS file callbacks: authenticated KF02/96B persistent envelope,
 * opaque HT01 class-scoped64B CPX object, IV16. Factory C/D only.
 * Legacy synthetic KF01/raw32 proof remains a separate private core. */
static bool
kfn_credential(const struct kebab_aks_credential *credential)
{
 if(!kfk_range(credential,sizeof(*credential)))return false;
 for(unsigned i=8;i<12;++i)if(credential->bytes[i])return false;
 for(unsigned i=38;i<80;++i)if(credential->bytes[i])return false;
 return kdb_nonzero(credential->bytes+22,16);
}

static int
kfn_class_locked(const uint8_t uuid[16],uint32_t cls,const uint8_t **key)
{
 if(cls!=3&&cls!=4){static unsigned reports;if(reports++<8)printf("KEBAB APFS FILE: unsupported requested class=0x%x\n",cls);return (int)kIOReturnUnsupported;}
 if(!kfk_ready||!kdb_ready||!kdc_registered||!kdc_loaded)return (int)kIOReturnNotReady;
 if(memcmp(uuid,kdc_uuid,16))return (int)kIOReturnNotPermitted;
 *key=cls==3?kdc_c:kdc_d;return 0;
}

static int
kfn_kek_locked(const uint8_t uuid[16],uint32_t cls,uint8_t output[32])
{
 const uint8_t *key=NULL;int error=kfn_class_locked(uuid,cls,&key);if(error)return error;
 static const uint8_t domain[]="UniXNU.kebab.apfs-file.v1";
 uint8_t context[sizeof(domain)+20];memcpy(context,domain,sizeof(domain));
 memcpy(context+sizeof(domain),uuid,16);memcpy(context+sizeof(domain)+16,&cls,4);
 g_crypto_funcs->cchmac_fn(g_crypto_funcs->ccsha256_di,32,key,sizeof(context),context,output);
 kfk_wipe(context,sizeof(context));return 0;
}

static void
kfn_plain(uint8_t out[88],const struct kebab_aks_credential *credential,uint32_t cls,const uint8_t material[48])
{
 memset(out,0,88);memcpy(out,"KF02\1\0",6);memcpy(out+6,credential->bytes+20,2);
 memcpy(out+8,credential->bytes+22,16);memcpy(out+24,credential->bytes,8);
 memcpy(out+32,&cls,4);out[36]=32;out[38]=16;memcpy(out+40,material,48);
}

static bool
kfn_identity(const uint8_t plain[88],const struct kebab_aks_credential *credential,uint32_t cls)
{
 uint8_t expected[88],zero[48]={0};kfn_plain(expected,credential,cls,zero);
 unsigned diff=0;for(unsigned i=0;i<40;++i)diff|=plain[i]^expected[i];
 kfk_wipe(expected,sizeof(expected));return diff==0;
}

static bool
kfn_buffers(const struct kebab_aks_credential *credential,const struct kebab_aks_raw_key *raw,
    const struct kebab_aks_wrapped_key *wrapped,bool create)
{
 return kmd_buffers(credential,raw,wrapped,true)&&wrapped->key_len>=96&&
     (create||wrapped->key_len==96);
}

/* Protected Unless Open: ephemeral Curve25519 public32 || AES-KW96(KF03).
 * The authenticated inner88 binds exact UUID/revision/crypto_id/class and
 * key32/IV16. CPX receives HB01/64B ciphertext, never the plaintext file key.
 * Existing C/D KF02/HT01 encoding is left untouched. Caller holds kfk_lock
 * in helpers ending _locked. No VFS/IOKit call occurs under that lock. */
static int
kbn_ready_locked(const uint8_t volume[16],bool private_required)
{
 if(!kbs_factory_locked()||!kbs_ready||!kdc_registered||!kdc_loaded)return (int)kIOReturnNotReady;
 if(memcmp(volume,kdc_uuid,16))return (int)kIOReturnNotPermitted;
 if(private_required&&!kbs_unlocked)return (int)kIOReturnNotPermitted;
 if(!kdb_nonzero(kbs_public,32)||(private_required&&!kdb_nonzero(kbs_private,32)))return (int)kIOReturnNotReady;
 return 0;
}

static void
kbn_plain(uint8_t plain[88],const struct kebab_aks_credential *cred,const uint8_t material[48])
{
 kfn_plain(plain,cred,2,material);memcpy(plain,"KF03",4);
}

static bool
kbn_identity(const uint8_t plain[88],const struct kebab_aks_credential *cred)
{
 uint8_t expected[88],zero[48]={0};kbn_plain(expected,cred,zero);
 unsigned diff=0;for(unsigned i=0;i<40;i++)diff|=plain[i]^expected[i];
 kfk_wipe(expected,sizeof(expected));kfk_wipe(zero,sizeof(zero));return !diff;
}

static int
kbn_seal_locked(const struct kebab_aks_credential *cred,const uint8_t material[48],uint8_t output[128])
{
 int error=kbn_ready_locked(cred->bytes+22,false);if(error)return error;
 uint8_t scalar[32]={0},pub[32]={0},shared[32]={0},kek[32]={0},plain[88]={0},sealed[128]={0},base[32]={9};
 read_random(scalar,32);
 if(!kdb_nonzero(scalar,32)||!kebab_x25519(pub,scalar,base)||
     !kebab_x25519(shared,scalar,kbs_public)||
     !kebab_class_b_concat_kdf(kek,shared,pub,kbs_public,kcb_sha256))error=(int)kIOReturnError;
 if(!error){kbn_plain(plain,cred,material);memcpy(sealed,pub,32);error=kfk_wrap(kek,plain,88,sealed+32,true);}
 if(!error)memcpy(output,sealed,128);
 kfk_wipe(scalar,sizeof(scalar));kfk_wipe(pub,sizeof(pub));kfk_wipe(shared,sizeof(shared));kfk_wipe(kek,sizeof(kek));
 kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(base,sizeof(base));return error;
}

static int
kbn_open_locked(const struct kebab_aks_credential *cred,const uint8_t input[128],uint8_t plain[88])
{
 int error=kbn_ready_locked(cred->bytes+22,true);if(error)return error;
 uint8_t shared[32]={0},kek[32]={0},result[88]={0};
 if(!kebab_x25519(shared,kbs_private,input)||
     !kebab_class_b_concat_kdf(kek,shared,input,kbs_public,kcb_sha256))error=(int)kIOReturnNotPermitted;
 if(!error)error=kfk_wrap(kek,input+32,96,result,false);
 if(!error)memcpy(plain,result,88);
 kfk_wipe(shared,sizeof(shared));kfk_wipe(kek,sizeof(kek));kfk_wipe(result,sizeof(result));return error;
}

static int
kbn_new(const struct kebab_aks_credential *cred,struct kebab_aks_raw_key *raw,struct kebab_aks_wrapped_key *wrapped)
{
 if(!kfk_context()||!kmd_buffers(cred,raw,wrapped,true)||wrapped->key_len<128)return (int)kIOReturnBadArgument;
 if(!kfn_credential(cred)||raw->flags)return (int)kIOReturnUnsupported;
 uint8_t material[48]={0},sealed[128]={0},token[64]={0};
 lck_mtx_lock(&kfk_lock);int error=kbn_ready_locked(cred->bytes+22,false);
 if(!error){read_random(material,48);if(!kdb_nonzero(material,32)||!kdb_nonzero(material+32,16))error=(int)kIOReturnError;}
 if(!error)error=kbn_seal_locked(cred,material,sealed);
 if(!error)error=khb_create_locked(cred->bytes+22,material,token);
 if(!error)error=kbs_bind_raw_locked(cred->bytes+22);
 if(!error){
  memcpy(wrapped->key,sealed,128);memcpy(raw->key,token,64);memcpy(raw->iv_key,material+32,16);
  wrapped->key_len=128;wrapped->dp_class=2;raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;
 }
 lck_mtx_unlock(&kfk_lock);
 uint64_t crypto_id;memcpy(&crypto_id,cred->bytes,sizeof(crypto_id));
 printf("KEBAB APFS B NEW: crypto-id=%llu raw-address=%p result=%x; opaque64/IV16, no key bytes logged\n",
     (unsigned long long)crypto_id,raw->key,error);
 kfk_wipe(material,sizeof(material));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(token,sizeof(token));return error;
}

static int
kbn_unwrap(const struct kebab_aks_credential *cred,const struct kebab_aks_wrapped_key *wrapped,struct kebab_aks_raw_key *raw)
{
 if(!kfk_context()||!kmd_buffers(cred,raw,wrapped,true)||wrapped->key_len!=128)return (int)kIOReturnBadArgument;
 if(!kfn_credential(cred))return (int)kIOReturnUnsupported;
 uint8_t sealed[128],plain[88]={0},token[64]={0};memcpy(sealed,wrapped->key,128);
 lck_mtx_lock(&kfk_lock);int error=kbn_open_locked(cred,sealed,plain);
 if(!error&&!kbn_identity(plain,cred))error=(int)kIOReturnNotPermitted;
 if(!error)error=khb_create_locked(cred->bytes+22,plain+40,token);
 if(!error)error=kbs_bind_raw_locked(cred->bytes+22);
 if(!error){memcpy(raw->key,token,64);memcpy(raw->iv_key,plain+72,16);raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;}
 lck_mtx_unlock(&kfk_lock);
 uint64_t crypto_id;memcpy(&crypto_id,cred->bytes,sizeof(crypto_id));
 printf("KEBAB APFS B UNWRAP: crypto-id=%llu raw-address=%p result=%x; no key bytes logged\n",
     (unsigned long long)crypto_id,raw->key,error);
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(plain,sizeof(plain));kfk_wipe(token,sizeof(token));return error;
}

static int
kbn_rewrap(const struct kebab_aks_credential *cred,uint32_t target,
    const struct kebab_aks_wrapped_key *input,struct kebab_aks_wrapped_key *output)
{
 if(!kfk_context()||!kfk_range(cred,sizeof(*cred))||!kfk_range(input,sizeof(*input))||!kfk_range(output,sizeof(*output)))return (int)kIOReturnBadArgument;
 uint32_t source=input->dp_class;unsigned inbytes=source==2?128u:96u,outbytes=target==2?128u:96u;
 if((source!=2&&source!=3&&source!=4)||(target!=2&&target!=3&&target!=4)||!kfn_credential(cred))return (int)kIOReturnUnsupported;
 if(input->key_len!=inbytes||output->key_len<outbytes||output->key_len>128||!kfk_range(input->key,inbytes)||!kfk_range(output->key,output->key_len))return (int)kIOReturnBadArgument;
 const void *headers[]={cred,input,output};const size_t sizes[]={sizeof(*cred),sizeof(*input),sizeof(*output)};
 for(unsigned i=0;i<3;i++)if(kfk_overlap(input->key,inbytes,headers[i],sizes[i])||kfk_overlap(output->key,output->key_len,headers[i],sizes[i]))return (int)kIOReturnBadArgument;
 if((input!=output&&kfk_overlap(input,sizeof(*input),output,sizeof(*output)))||
     kfk_overlap(cred,sizeof(*cred),input,sizeof(*input))||kfk_overlap(cred,sizeof(*cred),output,sizeof(*output))||
     (input->key!=output->key&&kfk_overlap(input->key,inbytes,output->key,output->key_len)))return (int)kIOReturnBadArgument;
 uint8_t sealed[128]={0},plain[88]={0},result[128]={0},kek[32]={0};
 struct kebab_aks_credential original=*cred;memcpy(sealed,input->key,inbytes);
 lck_mtx_lock(&kfk_lock);int error;
 if(source==2)error=kbn_open_locked(cred,sealed,plain);
 else{error=kfn_kek_locked(cred->bytes+22,source,kek);if(!error)error=kfk_wrap(kek,sealed,96,plain,false);}
 /* As in qualified C/D rewrap, APFS supplies the new target crypto_id.
  * Authenticate the old envelope before using its old id for identity checks. */
 if(!error){memcpy(original.bytes,plain+24,8);if(!(source==2?kbn_identity(plain,&original):kfn_identity(plain,&original,source)))error=(int)kIOReturnNotPermitted;}
 if(!error){
  if(target==2)error=kbn_seal_locked(cred,plain+40,result);
  else{
   error=kfn_kek_locked(cred->bytes+22,target,kek);
   if(!error){uint8_t next[88];kfn_plain(next,cred,target,plain+40);error=kfk_wrap(kek,next,88,result,true);kfk_wipe(next,sizeof(next));}
  }
 }
 if(!error){memcpy(output->key,result,outbytes);output->key_len=outbytes;output->dp_class=target;}
 lck_mtx_unlock(&kfk_lock);
 printf("KEBAB APFS B REWRAP: source=%u target=%u result=%x; existing material retained\n",source,target,error);
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(plain,sizeof(plain));kfk_wipe(result,sizeof(result));kfk_wipe(kek,sizeof(kek));kfk_wipe(&original,sizeof(original));return error;
}

int
kebab_apfs_file_key_new(const struct kebab_aks_credential *credential,uint32_t cls,
    struct kebab_aks_raw_key *raw,struct kebab_aks_wrapped_key *wrapped)
{
 if(cls==2)return kbn_new(credential,raw,wrapped);
 if(!kfk_context()||!kfn_buffers(credential,raw,wrapped,true))return (int)kIOReturnBadArgument;
 if(!kfn_credential(credential)||raw->flags||(cls!=3&&cls!=4))return (int)kIOReturnUnsupported;
 uint8_t uuid[16],material[48]={0},plain[88]={0},sealed[96]={0},kek[32]={0},token[64]={0};
 memcpy(uuid,credential->bytes+22,16);lck_mtx_lock(&kfk_lock);int error=kfn_kek_locked(uuid,cls,kek);
 if(!error){read_random(material,48);kfn_plain(plain,credential,cls,material);error=kfk_wrap(kek,plain,88,sealed,true);}
 if(!error)error=kch_class_create_locked(uuid,cls,material,token);
 if(!error){
  memcpy(wrapped->key,sealed,96);memcpy(raw->key,token,64);memcpy(raw->iv_key,material+32,16);
  wrapped->key_len=96;wrapped->dp_class=cls;raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;
 }
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(uuid,sizeof(uuid));kfk_wipe(material,sizeof(material));kfk_wipe(plain,sizeof(plain));
 kfk_wipe(sealed,sizeof(sealed));kfk_wipe(kek,sizeof(kek));kfk_wipe(token,sizeof(token));return error;
}

int
kebab_apfs_file_key_unwrap(const struct kebab_aks_credential *credential,
    const struct kebab_aks_wrapped_key *wrapped,struct kebab_aks_raw_key *raw)
{
 if(kfk_range(wrapped,sizeof(*wrapped))&&wrapped->dp_class==2)return kbn_unwrap(credential,wrapped,raw);
 if(!kfk_context()||!kfn_buffers(credential,raw,wrapped,false))return (int)kIOReturnBadArgument;
 uint32_t cls=wrapped->dp_class;
 if(!kfn_credential(credential)||(cls!=3&&cls!=4))return (int)kIOReturnUnsupported;
 uint8_t uuid[16],sealed[96],plain[88]={0},kek[32]={0},token[64]={0};
 memcpy(uuid,credential->bytes+22,16);memcpy(sealed,wrapped->key,96);
 lck_mtx_lock(&kfk_lock);int error=kfn_kek_locked(uuid,cls,kek);
 if(!error)error=kfk_wrap(kek,sealed,96,plain,false);
 if(!error&&!kfn_identity(plain,credential,cls))error=(int)kIOReturnNotPermitted;
 if(!error)error=kch_class_create_locked(uuid,cls,plain+40,token);
 if(!error){memcpy(raw->key,token,64);memcpy(raw->iv_key,plain+72,16);
  raw->key_len=64;raw->iv_key_len=16;raw->flags=KEBAB_RAW_KEY_WRAPPED;}
 lck_mtx_unlock(&kfk_lock);
 kfk_wipe(uuid,sizeof(uuid));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(plain,sizeof(plain));
 kfk_wipe(kek,sizeof(kek));kfk_wipe(token,sizeof(token));return error;
}

int
kebab_apfs_file_key_rewrap(const struct kebab_aks_credential *credential,uint32_t cls,
    const struct kebab_aks_wrapped_key *input,struct kebab_aks_wrapped_key *output)
{
 if(cls==2||(kfk_range(input,sizeof(*input))&&input->dp_class==2))return kbn_rewrap(credential,cls,input,output);
 if(!kfk_context()||!kfk_range(credential,sizeof(*credential))||!kfk_range(input,sizeof(*input))||
     !kfk_range(output,sizeof(*output))||input->key_len!=96||output->key_len<96||output->key_len>128||
     !kfk_range(input->key,96)||!kfk_range(output->key,output->key_len))return (int)kIOReturnBadArgument;
 /* Exact input/output descriptor and exact data in-place are supported;
  * other header/data or partial-buffer aliasing is refused. */
 const void *headers[]={credential,input,output};const size_t lengths[]={sizeof(*credential),sizeof(*input),sizeof(*output)};
 for(unsigned i=0;i<3;++i){
  if(kfk_overlap(input->key,96,headers[i],lengths[i])||kfk_overlap(output->key,output->key_len,headers[i],lengths[i]))return (int)kIOReturnBadArgument;
 }
 if((input!=output&&kfk_overlap(input,sizeof(*input),output,sizeof(*output)))||
     kfk_overlap(credential,sizeof(*credential),input,sizeof(*input))||kfk_overlap(credential,sizeof(*credential),output,sizeof(*output))||
     (input->key!=output->key&&kfk_overlap(input->key,96,output->key,output->key_len)))return (int)kIOReturnBadArgument;
 uint32_t source=input->dp_class;
 if(!kfn_credential(credential)||(source!=3&&source!=4)||(cls!=3&&cls!=4))return (int)kIOReturnUnsupported;
 uint8_t uuid[16],sealed[96],plain[88]={0},kek[32]={0},result[96]={0};
 memcpy(uuid,credential->bytes+22,16);memcpy(sealed,input->key,96);
 lck_mtx_lock(&kfk_lock);int error=kfn_kek_locked(uuid,source,kek);
 if(!error)error=kfk_wrap(kek,sealed,96,plain,false);
 /* Original APFS may allocate a fresh crypto_id for the target class.
  * Its only credential carries that TARGET id (jobj.c change_dstream and
  * crypto_id_converter), not the authenticated source id inside KF02.
  * Authenticate the complete old AES-KW envelope first, retain exact UUID,
  * revision, source class and structure, then bind the target id on output.
  * Unwrap remains strict for every byte of its caller identity. */
 if(!error){
  struct kebab_aks_credential source_identity=*credential;
  memcpy(source_identity.bytes,plain+24,8);
  if(!kfn_identity(plain,&source_identity,source))error=(int)kIOReturnNotPermitted;
  kfk_wipe(&source_identity,sizeof(source_identity));
 }

 if(!error)error=kfn_kek_locked(uuid,cls,kek);
 if(!error){memcpy(plain+24,credential->bytes,8);memcpy(plain+32,&cls,4);error=kfk_wrap(kek,plain,88,result,true);}
 if(!error){memcpy(output->key,result,96);output->key_len=96;output->dp_class=cls;}
 lck_mtx_unlock(&kfk_lock);
 printf("KEBAB APFS FILE REWRAP: source=%u target=%u status=%x\n",source,cls,error);
 kfk_wipe(uuid,sizeof(uuid));kfk_wipe(sealed,sizeof(sealed));kfk_wipe(plain,sizeof(plain));kfk_wipe(kek,sizeof(kek));kfk_wipe(result,sizeof(result));return error;
}

int
kebab_apfs_file_key_is_available(const struct kebab_aks_credential *credential,uint32_t cls)
{
 if(!kfk_context()||!kfk_range(credential,sizeof(*credential)))return (int)kIOReturnBadArgument;
 if(cls==14){
  if(!kfk_meta_credential(credential))return (int)kIOReturnUnsupported;
  return kebab_file_keys_available()?0:(int)kIOReturnNotReady;
 }
 if(!kfn_credential(credential))return (int)kIOReturnUnsupported;
 const uint8_t *key=NULL;lck_mtx_lock(&kfk_lock);
 int error=cls==2?kbn_ready_locked(credential->bytes+22,false):kfn_class_locked(credential->bytes+22,cls,&key);
 lck_mtx_unlock(&kfk_lock);return error; /* Original slot returns IOReturn, not bool. */
}

int
kebab_xts128_crypt(const uint8_t key[32], uint64_t unit, const void *input, void *output, size_t bytes, bool encrypt)
{
 if (!kfk_range(key,32) || !kfk_range(input,bytes) || !kfk_range(output,bytes) || bytes%4096 ||
     bytes>16u*1024u*1024u || unit>UINT64_MAX-(bytes/4096-1) ||
     (input!=output && kfk_overlap(input,bytes,output,bytes))) return (int)kIOReturnBadArgument;
 aes_encrypt_ctx data_enc,tweak_enc; aes_decrypt_ctx data_dec;
 uint8_t tweak[16]={0},block[16]={0},iv[16]={0};
 kfk_wipe(&data_enc,sizeof(data_enc));kfk_wipe(&data_dec,sizeof(data_dec));kfk_wipe(&tweak_enc,sizeof(tweak_enc));
 int error=aes_encrypt_key128(key+16,&tweak_enc);
 if (!error) error=encrypt?aes_encrypt_key128(key,&data_enc):aes_decrypt_key128(key,&data_dec);
 if (error) { error=(int)kIOReturnError;goto done; }
 for(size_t offset=0;offset<bytes;offset+=4096,++unit) {
  memset(iv,0,16);for(unsigned i=0;i<8;++i)iv[i]=(uint8_t)(unit>>(8*i));
  if (aes_encrypt(iv,tweak,&tweak_enc)) { error=(int)kIOReturnError;goto done; }
  for(unsigned at=0;at<4096;at+=16) {
   const uint8_t *in=(const uint8_t *)input+offset+at;uint8_t *out=(uint8_t *)output+offset+at;
   for(unsigned i=0;i<16;++i)block[i]=in[i]^tweak[i];
   if (encrypt?aes_encrypt(block,block,&data_enc):aes_decrypt(block,block,&data_dec)) { error=(int)kIOReturnError;goto done; }
   for(unsigned i=0;i<16;++i)out[i]=block[i]^tweak[i];
   unsigned carry=0;
   for(unsigned i=0;i<16;++i) { unsigned next=tweak[i]>>7;tweak[i]=(uint8_t)((tweak[i]<<1)|carry);carry=next; }
   tweak[0]^=(uint8_t)(0x87u & (0u-carry));
  }
 }
done:
 kfk_wipe(&data_enc,sizeof(data_enc));kfk_wipe(&data_dec,sizeof(data_dec));kfk_wipe(&tweak_enc,sizeof(tweak_enc));
 kfk_wipe(tweak,sizeof(tweak));kfk_wipe(block,sizeof(block));kfk_wipe(iv,sizeof(iv));return error;
}

int
kebab_file_keys_selftest(void)
{
 static const uint8_t kw_expected[24]={0x64,0xe8,0xc3,0xf9,0xce,0x0f,0x5b,0xa2,0x63,0xe9,0x77,0x79,
     0x05,0x81,0x8a,0x2a,0x93,0xc8,0x19,0x1e,0x7d,0x6e,0x8a,0xe7};
 static const uint8_t xts_digest[32]={0xe4,0x41,0x9c,0x20,0x34,0x78,0x44,0xa0,0x13,0xee,0x15,0x9b,0xbd,0x42,0x7a,0x12,
     0x3c,0x33,0x0c,0x21,0xbf,0xe0,0x32,0x76,0x9d,0x0c,0x51,0x7f,0x5d,0xed,0xaa,0x05};
 struct { uint8_t plain[4096],cipher[4096],back[4096]; } *work=kalloc_data(sizeof(*work),Z_WAITOK|Z_ZERO);
 if (!work) return (int)kIOReturnNoMemory;
 uint8_t key[32],plain[16],sealed[24],restored[16],digest[32];
 for(unsigned i=0;i<32;++i)key[i]=(uint8_t)i;
 for(unsigned i=0;i<16;++i)plain[i]=(uint8_t)(17*i);
 int error=kfk_wrap(key,plain,16,sealed,true);
 if (!error && memcmp(sealed,kw_expected,24))error=(int)kIOReturnError;
 if (!error)error=kfk_wrap(key,sealed,24,restored,false);
 if (!error && memcmp(plain,restored,16))error=(int)kIOReturnError;
 for(unsigned i=0;i<4096;++i)work->plain[i]=(uint8_t)i;
 if (!error)error=kebab_xts128_crypt(key,0x123456789abcdefull,work->plain,work->cipher,4096,true);
 if (!error) {
  SHA256_CTX hash;SHA256_Init(&hash);SHA256_Update(&hash,work->cipher,4096);SHA256_Final(digest,&hash);
  if(memcmp(digest,xts_digest,32))error=(int)kIOReturnError;
 }
 if (!error)error=kebab_xts128_crypt(key,0x123456789abcdefull,work->cipher,work->back,4096,false);
 if (!error && memcmp(work->plain,work->back,4096))error=(int)kIOReturnError;
 printf("KEBAB FILE KEYS: native RFC3394/AES128-XTS public KAT %s\n",error?"FAIL":"PASS");
 kfk_wipe(key,sizeof(key));kfk_wipe(plain,sizeof(plain));kfk_wipe(sealed,sizeof(sealed));
 kfk_wipe(restored,sizeof(restored));kfk_wipe(digest,sizeof(digest));kfk_wipe(work,sizeof(*work));kfree_data(work,sizeof(*work));
 return error;
}
#endif

int cp_key_store_action_for_volume(uint8_t*v,int a){(void)v;(void)a;assert(!kfk_lock);return 0;}
void test_make_B_handle(uint8_t output[64]){
 uint8_t root[32],container[16],system[16],bag[4096],iv[16];for(unsigned i=0;i<32;i++)root[i]=i;
 for(unsigned i=0;i<16;i++){container[i]=32+i;system[i]=64+i;}
 static const uint8_t uuid[16]={0x61,0x70,0x66,0x73,0x75,0x75,0x69,0x64,0,0x40,0x76,0x6f,0x6c,0x75,0x6d,1};
 assert(!kebab_file_keys_prepare(root,32)&&!kebab_file_keys_commit());rng=0;
 assert(!kebab_device_classbag_create(container,system,bag,4096)&&!kebab_device_classbag_open(container,system,bag,4096));
 assert(!kebab_device_keybag_bootstrap()&&!kebab_volume_factory_activate(uuid));
 struct kebab_aks_credential cred={{0}};uint64_t id=42;uint16_t revision=1;
 memcpy(cred.bytes,&id,8);memcpy(cred.bytes+20,&revision,2);memcpy(cred.bytes+22,uuid,16);
 uint8_t sealed[128];struct kebab_aks_raw_key raw={output,64,iv,16,0};struct kebab_aks_wrapped_key wrapped={sealed,128,0};rng=0;
 assert(!kebab_apfs_file_key_new(&cred,2,&raw,&wrapped)&&raw.key_len==64&&raw.flags==KEBAB_RAW_KEY_WRAPPED&&!memcmp(output,"HB01",4));
}
void test_root_ready(int ready){kfk_ready=ready!=0;}
void test_lock_state(int locked){assert(!kebab_device_keybag_set_locked(locked!=0));}
void test_AES_fault(unsigned n){aes_failure=n?aes_calls+n:0;}
