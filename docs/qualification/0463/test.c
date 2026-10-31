#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "vectors.h"
int kebab_x25519(uint8_t[32],const uint8_t[32],const uint8_t[32]);
int kebab_class_b_concat_kdf(uint8_t[32],const uint8_t[32],const uint8_t[32],const uint8_t[32],int(*)(const uint8_t*,size_t,uint8_t[32]));
static unsigned wipes,bytes;
static int hash_result=1;
static int hash256(const uint8_t*input,size_t n,uint8_t out[32]){assert(n==100&&input[0]==0&&input[1]==0&&input[2]==0&&input[3]==1);SHA256(input,n,out);return hash_result;}
void kebab_x25519_wipe_observed(const void*p,size_t n){const uint8_t*b=p;for(size_t i=0;i<n;i++)assert(!b[i]);wipes++;bytes+=(unsigned)n;}
int main(void){
 uint8_t out[32],scalar[32],point[32],alias[96];
 for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++){
  memcpy(scalar,vectors[i][0],32);memcpy(point,vectors[i][1],32);unsigned w=wipes,b=bytes;
  assert(kebab_x25519(out,scalar,point)&&!memcmp(out,vectors[i][2],32));assert(wipes-w==17&&bytes-b==600);
  assert(!memcmp(scalar,vectors[i][0],32)&&!memcmp(point,vectors[i][1],32));
  point[31]^=0x80;assert(kebab_x25519(out,scalar,point)&&!memcmp(out,vectors[i][2],32));
  memcpy(alias,scalar,32);memcpy(alias+32,point,32);memset(alias+64,0x5a,32);
  assert(kebab_x25519(alias+16,alias,alias+32)&&!memcmp(alias+16,vectors[i][2],32));
  for(unsigned j=64;j<96;j++)assert(alias[j]==0x5a);
 }
 for(unsigned i=0;i<5;i++){
  memset(point,0,32);if(i==1)point[0]=1;if(i>=2){memset(point,0xff,32);point[31]=0x7f;point[0]=(uint8_t)(0xec+i-2);}
  memset(out,0x5a,32);unsigned w=wipes,b=bytes;
  assert(!kebab_x25519(out,scalar,point));for(unsigned j=0;j<32;j++)assert(out[j]==0x5a);
  assert(wipes-w==17&&bytes-b==600);
 }
 unsigned w=wipes;assert(!kebab_x25519(NULL,scalar,point)&&!kebab_x25519(out,NULL,point)&&!kebab_x25519(out,scalar,NULL));assert(wipes==w);
 uint8_t k[32]={9},u[32]={9},prev[32];
 for(unsigned i=0;i<1000;i++){memcpy(prev,k,32);assert(kebab_x25519(k,k,u));memcpy(u,prev,32);}
 assert(!memcmp(k,iteration1000,32));
 for(unsigned i=0;i<sizeof(kdf_vectors)/sizeof(kdf_vectors[0]);i++){
  unsigned w=wipes,b=bytes;
  assert(kebab_class_b_concat_kdf(out,kdf_vectors[i][0],kdf_vectors[i][1],kdf_vectors[i][2],hash256));
  assert(!memcmp(out,kdf_vectors[i][3],32)&&wipes-w==2&&bytes-b==132);
  memcpy(alias,kdf_vectors[i][0],32);memcpy(alias+32,kdf_vectors[i][1],32);memcpy(alias+64,kdf_vectors[i][2],32);
  assert(kebab_class_b_concat_kdf(alias+16,alias,alias+32,alias+64,hash256)&&!memcmp(alias+16,kdf_vectors[i][3],32));
 }
 for(unsigned mode=0;mode<3;mode++){memset(out,0x5a,32);hash_result=mode==0?0:mode==1?-1:2;assert(!kebab_class_b_concat_kdf(out,kdf_vectors[0][0],kdf_vectors[0][1],kdf_vectors[0][2],hash256));for(unsigned i=0;i<32;i++)assert(out[i]==0x5a);}hash_result=1;
 memset(point,0,32);assert(!kebab_class_b_concat_kdf(out,point,point,point,hash256));assert(!kebab_class_b_concat_kdf(out,k,k,k,NULL));
 printf("PASS %zu independent vectors +high-bit/overlap/low-order/1000iterations; all17wipe regions/600bytes per exchange verified\n",sizeof(vectors)/sizeof(vectors[0]));
 puts("PASS 514 classB concatKDF cross-checks, provider failure/zero-secret/alias gates and2wipe regions/132bytes per KDF");
}
