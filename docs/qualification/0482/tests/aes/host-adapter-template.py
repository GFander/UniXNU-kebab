"""Actual NativeAES CBC/KDF/dispatch code; host IOKit and OpenSSL AES adapters."""
from pathlib import Path
import subprocess,sys,tempfile,re
s=Path(sys.argv[1]).read_text()
globals=s[s.index('extern "C" {'):s.index('class NativeAES :')]
core=s[s.index('static NativeAES *native_nonce_provider;'):s.index('bool NativeAES::start(IOService *provider)')]
body=s[s.index('IOReturn NativeAES::performAES(IOMemoryDescriptor'):s.rindex('#endif')]
flush='  __asm__ volatile("dc civac, %0" :: "r"(native_nonce_key) : "memory");\n  __asm__ volatile("dsb sy" ::: "memory");'
assert core.count(flush)==1
core=core.replace(flush,'  host_cache_clean(native_nonce_key);')
support=re.search(r'bool isKeySupported\(unsigned selector\) override \{([^\n]+)\}',s).group(1)
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <openssl/evp.h>
using IOReturn=uint32_t;using task_t=void*;using mach_vm_address_t=uintptr_t;using vm_offset_t=uintptr_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnBadArgument=0xe00002c2,kIOReturnUnsupported=0xe00002c7,kIOReturnNoMemory=0xe00002bd,kIOReturnIOError=0xe00002ca,kIOReturnNotReady=0xe00002d8;
constexpr int kIODirectionInOut=3,kIODirectionOut=1,kIODirectionIn=2;
enum IOAESOperation{IOAESDecrypt=0,IOAESEncrypt=1};
struct IOAESIV{unsigned char bytes[16];};
struct IOAESKeyData{unsigned bits;unsigned char bytes[32];unsigned selector;};
using IOAESCompletion=void(*)(void*,int);
task_t kernel_task=(void*)1;
vm_offset_t vm_kernel_stext=0,vm_kext_base=0,static_memory_end=0;
static unsigned published,cleans;static int negativeFlag,fault;static bool cryptoFail;
static void cc_clear(size_t n,void *p){volatile unsigned char *v=(unsigned char*)p;for(size_t i=0;i<n;i++)v[i]=0;}
static int IOLog(const char *f,...){(void)f;return 0;}
static void *IOMalloc(size_t n){return fault==1?nullptr:malloc(n);}
static void IOFree(void *p,size_t n){(void)n;free(p);}
static bool PE_parse_boot_argn(const char *k,void *p,unsigned n){assert(!strcmp(k,"kebab_nonce_negative_kat")&&n==4);*(unsigned*)p=negativeFlag;return true;}
class IOMemoryDescriptor{
protected:unsigned char *data;size_t length;bool owned;
public:
 IOMemoryDescriptor(unsigned char *p,size_t n,bool o):data(p),length(n),owned(o){}
 virtual ~IOMemoryDescriptor(){if(owned)free(data);}
 void release(){delete this;}
 size_t getLength(){return length;}
 size_t readBytes(size_t off,void *out,size_t n){if(off>length||n>length-off||fault==2)return 0;memcpy(out,data+off,n);return n;}
 size_t writeBytes(size_t off,const void *in,size_t n){if(off>length||n>length-off||fault==3)return 0;memcpy(data+off,in,n);return n;}
 IOReturn prepare(){return fault==4?kIOReturnIOError:0;}
 void complete(){}
 static IOMemoryDescriptor *withAddressRange(mach_vm_address_t p,size_t n,int dir,task_t task){(void)dir;assert(task==kernel_task);return new IOMemoryDescriptor((unsigned char*)p,n,false);}
};
class IOBufferMemoryDescriptor:public IOMemoryDescriptor{
public:explicit IOBufferMemoryDescriptor(size_t n):IOMemoryDescriptor((unsigned char*)calloc(1,n),n,true){}
 static IOBufferMemoryDescriptor *withCapacity(size_t n,int d){assert(d==kIODirectionInOut);return new IOBufferMemoryDescriptor(n);}
};
struct aes_encrypt_ctx{unsigned char key[32];unsigned n;};using aes_decrypt_ctx=aes_encrypt_ctx;
static int aes_encrypt_key(const unsigned char *p,unsigned n,aes_encrypt_ctx *c){if(cryptoFail)return -1;assert(n==16||n==24||n==32);memcpy(c->key,p,n);c->n=n;return 0;}
static int aes_decrypt_key(const unsigned char *p,unsigned n,aes_decrypt_ctx *c){return aes_encrypt_key(p,n,c);}
static int crypt(const unsigned char *in,const unsigned char *iv,unsigned blocks,unsigned char *out,const aes_encrypt_ctx *c,bool enc){
 EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();assert(ctx);const EVP_CIPHER *alg=c->n==16?EVP_aes_128_cbc():c->n==24?EVP_aes_192_cbc():EVP_aes_256_cbc();
 assert(EVP_CipherInit_ex(ctx,alg,nullptr,c->key,iv,enc));assert(EVP_CIPHER_CTX_set_padding(ctx,0));int n=0,m=0;
 assert(EVP_CipherUpdate(ctx,out,&n,in,blocks*16)&&EVP_CipherFinal_ex(ctx,out+n,&m));assert((unsigned)(n+m)==blocks*16);EVP_CIPHER_CTX_free(ctx);return 0;
}
static int aes_encrypt_cbc(const unsigned char *p,const unsigned char *v,unsigned n,unsigned char *o,const aes_encrypt_ctx *c){return crypt(p,v,n,o,c,true);}
static int aes_decrypt_cbc(const unsigned char *p,const unsigned char *v,unsigned n,unsigned char *o,const aes_decrypt_ctx *c){return crypt(p,v,n,o,c,false);}
static void host_cache_clean(const unsigned char *p){for(unsigned i=0;i<16;i++)assert(!p[i]);cleans++;}
'''
declaration='''
class NativeAES {
public:
 bool isKeySupported(unsigned selector){'''+support+'''}
 IOReturn performAES(void *,void *,task_t,unsigned long long,IOAESIV *,IOAESOperation,IOAESKeyData *,IOAESCompletion,void *);
 IOReturn performAES(IOMemoryDescriptor *,IOMemoryDescriptor *,unsigned long long,IOAESIV *,IOAESOperation,IOAESKeyData *,unsigned long long,unsigned long long,IOAESCompletion,void *);
 void setProperty(const char *,bool){}
 void setProperty(const char *,unsigned,unsigned){}
 void registerService(){published++;}
};
'''
tests=r'''
int main(){NativeAES provider;unsigned tests=0;
 assert(knownAnswerTest(&provider,false));assert(!knownAnswerTest(&provider,true));tests+=2;
 assert(nonceKnownAnswerTest(&provider,false));assert(!nonceKnownAnswerTest(&provider,true));tests+=2;
 unsigned char root[32],zero[32]={};for(unsigned i=0;i<32;i++)root[i]=i;
 native_aes_kat_status=1;
 assert(!kebab_aes_nonce_prepare(root,32)&&!published);tests++;
 native_nonce_provider=&provider;
 assert(!kebab_aes_nonce_prepare(nullptr,32));assert(!kebab_aes_nonce_prepare(root,31));assert(!kebab_aes_nonce_prepare(zero,32));tests+=3;
 negativeFlag=1;assert(!kebab_aes_nonce_prepare(root,32)&&!native_nonce_state&&!published);tests++;
 negativeFlag=0;assert(kebab_aes_nonce_prepare(root,32)&&native_nonce_state==1&&!provider.isKeySupported(2212)&&!published);tests++;
 kebab_aes_nonce_cancel();assert(!native_nonce_state);for(auto b:native_nonce_key)assert(!b);tests++;
 assert(kebab_aes_nonce_prepare(root,32));assert(kebab_aes_nonce_publish()&&published==1&&provider.isKeySupported(2212));tests+=2;
 assert(!kebab_aes_nonce_publish()&&published==1);tests++;
 kebab_aes_nonce_cancel();assert(provider.isKeySupported(2212));tests++;
 unsigned char input[16],out[16];for(unsigned i=0;i<16;i++)input[i]=i;
 const unsigned char expected[16]={0xb0,0x34,0xaa,0x1f,0xc9,0x96,0xce,0x6e,0x5a,0xb1,0x9a,0x9a,0x21,0x9b,0x5f,0xca};
 IOAESKeyData key={128,{},2212};IOAESIV iv={};
 assert(!provider.performAES(input,out,kernel_task,16,&iv,IOAESEncrypt,&key,nullptr,nullptr)&&!memcmp(out,expected,16));tests++;
 key.bits=256;memset(out,0xa5,16);cc_clear(16,&iv);
 assert(provider.performAES(input,out,kernel_task,16,&iv,IOAESEncrypt,&key,nullptr,nullptr)==kIOReturnBadArgument);for(auto b:out)assert(b==0xa5);tests++;
 key.bits=128;key.selector=2001;
 assert(provider.performAES(input,out,kernel_task,16,&iv,IOAESEncrypt,&key,nullptr,nullptr)==kIOReturnUnsupported);tests++;
 key.selector=2212;
 for(int f=1;f<=4;f++){fault=f;assert(provider.performAES(input,out,kernel_task,16,&iv,IOAESEncrypt,&key,nullptr,nullptr)!=0);fault=0;tests++;}
 for(unsigned n=1;n<16;n++){assert(provider.performAES(input,out,kernel_task,n,&iv,IOAESEncrypt,&key,nullptr,nullptr)==kIOReturnBadArgument);tests++;}
 kebab_aes_nonce_wipe();assert(!native_nonce_state&&!provider.isKeySupported(2212)&&cleans==1);for(auto b:native_nonce_key)assert(!b);tests++;
 assert(provider.performAES(input,out,kernel_task,16,&iv,IOAESEncrypt,&key,nullptr,nullptr)==kIOReturnUnsupported);tests++;
 assert(provider.isKeySupported(0)&&!provider.isKeySupported(2001));tests++;
 assert(knownAnswerTest(&provider,false));tests++;
 fprintf(stdout,"PASS: %u actual-C++ CBC/KDF/selector/publish/negative/error/wipe cases; IOKit and cache mocked\n",tests);return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.cpp';c.write_text(prefix+globals+declaration+core+body+tests);out=Path(d)/'test'
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(c),'-lcrypto','-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
