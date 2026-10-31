#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <openssl/sha.h>
struct operations{const void*ccsha256_di;void(*ccdigest_fn)(const void*,unsigned long,const void*,void*);};
static unsigned digests;
static void digest(const void*di,unsigned long n,const void*input,void*out){assert(di);digests++;SHA256(input,n,out);}
static struct operations ops={(void*)1,digest},*g_crypto_funcs=&ops;
static void kfk_wipe(void*p,size_t n){volatile unsigned char*b=p;while(n--)*b++=0;}
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
 printf("KEBAB CLASS-B CRYPTO: X25519/ConcatKDF KAT %s; math only, classB services unavailable\n",ok?"PASS":"FAIL");
 return ok;
}

int main(void){
 assert(kcb_math_selftest()==1&&kcb_math_state==2&&digests==1);assert(kcb_math_selftest()==1&&digests==1);
 kcb_math_state=0;g_crypto_funcs=NULL;assert(!kcb_math_selftest()&&kcb_math_state==3);g_crypto_funcs=&ops;assert(!kcb_math_selftest());
 kcb_math_state=0;ops.ccdigest_fn=NULL;assert(!kcb_math_selftest()&&kcb_math_state==3);ops.ccdigest_fn=digest;
 kcb_math_state=0;ops.ccsha256_di=NULL;assert(!kcb_math_selftest()&&kcb_math_state==3);ops.ccsha256_di=(void*)1;
 kcb_math_state=1;assert(!kcb_math_selftest()&&kcb_math_state==1);
 kcb_math_state=0;assert(kcb_math_selftest()==1&&kcb_math_state==2);
 puts("PASS actual complete runtime math readiness KAT/one-time/provider-failure states; real host EC/KDF/SHA256, no bag service modeled as success");
}
