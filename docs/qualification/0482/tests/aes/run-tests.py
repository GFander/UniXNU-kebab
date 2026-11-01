"""Actual production C++ bodies; IOKit/cache and low-level AES are host models."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
r=Path(__file__).resolve().parent;w=r/'fairplay-aes-0481'
source=w/'candidate/iokit/Kernel/arm/NativeAES.cpp';before=w/'before/iokit/Kernel/arm/NativeAES.cpp';s=source.read_text()
base=(w/'host-adapter-template.py').read_text()
base=base[:base.index('with tempfile.TemporaryDirectory()')]
base=base.replace('enum IOAESOperation{IOAESDecrypt=0,IOAESEncrypt=1};','enum IOAESOperation{IOAESEncrypt=0,IOAESDecrypt=1};')
base=base.replace('  __asm__ volatile("dsb sy" ::: "memory");\'', '  __asm__ volatile("dc civac, %0" :: "r"(native_fairplay_key) : "memory");\\n  __asm__ volatile("dsb sy" ::: "memory");\'')
base=base.replace("'  host_cache_clean(native_nonce_key);'","'  host_cache_clean(native_nonce_key);host_cache_clean(native_fairplay_key);'")
base=base.replace('cleans==1','cleans==2')
extra=r'''
 assert(fairPlayKnownAnswerTest(&provider));tests++;
 unsigned char fk[16],nonce[16];assert(deriveFairPlayKey(root,fk));assert(deriveNonceKey(root,nonce));assert(memcmp(fk,nonce,16));tests+=3;
 const unsigned char expectedFair[32]={EXPECTED};
 unsigned char input32[32],output32[32],savedNonce[16];for(unsigned i=0;i<32;i++)input32[i]=i;
 IOAESKeyData fairkey={128,{},2201};IOAESIV fairiv={};
 memset(output32,0xa5,32);
 assert(!provider.isKeySupported(2201));
 assert(provider.performAES(input32,output32,kernel_task,32,&fairiv,IOAESEncrypt,&fairkey,nullptr,nullptr)==kIOReturnUnsupported);
 for(auto c:output32)assert(c==0xa5);for(auto c:fairiv.bytes)assert(!c);tests++;
 assert(kebab_aes_nonce_prepare(root,32));assert(!provider.isKeySupported(2201));memcpy(savedNonce,native_nonce_key,16);
 assert(!memcmp(native_fairplay_key,fk,16));kebab_aes_nonce_cancel();
 for(auto c:native_nonce_key)assert(!c);for(auto c:native_fairplay_key)assert(!c);tests++;
 cryptoFail=true;assert(!kebab_aes_nonce_prepare(root,32));cryptoFail=false;
 assert(!native_nonce_state&&!provider.isKeySupported(2201));for(auto c:native_fairplay_key)assert(!c);tests++;
 assert(kebab_aes_nonce_prepare(root,32));assert(!memcmp(native_nonce_key,savedNonce,16));assert(kebab_aes_nonce_publish());
 assert(provider.isKeySupported(2201)&&provider.isKeySupported(2212)&&!provider.isKeySupported(2204));tests++;
 assert(!provider.performAES(input32,output32,kernel_task,32,&fairiv,IOAESEncrypt,&fairkey,nullptr,nullptr));
 assert(!memcmp(output32,expectedFair,32)&&!memcmp(fairiv.bytes,expectedFair+16,16));tests++;
 memset(&fairiv,0,sizeof(fairiv));
 assert(!provider.performAES(output32,output32,kernel_task,32,&fairiv,IOAESDecrypt,&fairkey,nullptr,nullptr));
 assert(!memcmp(output32,input32,32)&&!memcmp(fairiv.bytes,expectedFair+16,16));tests++;
 for(unsigned bits=192;bits<=256;bits+=64){fairkey.bits=bits;memset(output32,0xa5,32);memset(&fairiv,0,sizeof(fairiv));
  assert(provider.performAES(input32,output32,kernel_task,32,&fairiv,IOAESEncrypt,&fairkey,nullptr,nullptr)==kIOReturnBadArgument);
  for(auto c:output32)assert(c==0xa5);for(auto c:fairiv.bytes)assert(!c);tests++;}
 fairkey.bits=128;
 assert(provider.performAES(input32,output32,kernel_task,32,&fairiv,(IOAESOperation)7,&fairkey,nullptr,nullptr)==kIOReturnUnsupported);tests++;
 for(int f=1;f<=4;f++){fault=f;assert(provider.performAES(input32,output32,kernel_task,32,&fairiv,IOAESEncrypt,&fairkey,nullptr,nullptr)!=0);fault=0;tests++;}
 assert(!memcmp(savedNonce,native_nonce_key,16));tests++;
 unsigned beforeCleans=cleans;kebab_aes_nonce_wipe();assert(cleans==beforeCleans+2&&!provider.isKeySupported(2201)&&!provider.isKeySupported(2212));
 for(auto c:native_nonce_key)assert(!c);for(auto c:native_fairplay_key)assert(!c);tests++;
 assert(provider.performAES(input32,output32,kernel_task,32,&fairiv,IOAESEncrypt,&fairkey,nullptr,nullptr)==kIOReturnUnsupported);tests++;
'''
contract=json.loads((w/'donor-contract.json').read_text());extra=extra.replace('EXPECTED',','.join('0x'+contract['synthetic_ciphertext'][i:i+2] for i in range(0,64,2)))
base=base.replace(' fprintf(stdout,"PASS:',extra+'\n fprintf(stdout,"PASS:')
argv=sys.argv[:];sys.argv=[str(__file__),str(source)]
ns={};exec(compile(base,'host-adapter-and-production-extraction','exec'),ns);sys.argv=argv
def fixture(text):
    g=text[text.index('extern "C" {'):text.index('class NativeAES :')]
    core=text[text.index('static NativeAES *native_nonce_provider;'):text.index('bool NativeAES::start(IOService *provider)')]
    assert core.count(ns['flush'])==1
    core=core.replace(ns['flush'],'  host_cache_clean(native_nonce_key);host_cache_clean(native_fairplay_key);')
    body=text[text.index('IOReturn NativeAES::performAES(IOMemoryDescriptor'):text.rindex('#endif')]
    import re
    support=re.search(r'bool isKeySupported\(unsigned selector\) override \{([^\n]+)\}',text).group(1)
    decl=ns['declaration'].replace(ns['support'],support)
    return ns['prefix']+g+decl+core+body+ns['tests']
changes=[
 ('fairplay-aliased-to-nonce','selected.value.bytes,native_fairplay_key,16','selected.value.bytes,native_nonce_key,16'),
 ('fairplay-selector-not-supported','selector == 2212 || selector == 2201','selector == 2212 || selector == 2204'),
 ('fairplay-dispatch-unavailable','if(key->selector==2201 && (native_fairplay_selftest ||','if(key->selector==2204 && (native_fairplay_selftest ||'),
 ('fairplay-bits-unchecked','if(key->bits!=128)return kIOReturnBadArgument;\n    selected.value.selector=0;memcpy(selected.value.bytes,native_fairplay_key,16);','if(key->bits!=128)return kIOReturnSuccess;\n    selected.value.selector=0;memcpy(selected.value.bytes,native_fairplay_key,16);'),
 ('cancel-fairplay-not-cleared','cc_clear(sizeof(native_fairplay_key),native_fairplay_key);\n  __atomic_store_n(&native_nonce_state,0','/* missing clear */\n  __atomic_store_n(&native_nonce_state,0'),
 ('reset-fairplay-not-cleared','for(unsigned i=0;i<sizeof(native_fairplay_key);i++)bytes[i]=0;','for(unsigned i=0;i<0;i++)bytes[i]=0;'),
 ('prepare-wrong-purpose','deriveNonceKey(root,derived) && deriveFairPlayKey(root,fairplay)','deriveNonceKey(root,derived) && deriveNonceKey(root,fairplay)'),
 ('original-nonce-key-overwritten','memcpy(native_nonce_key,derived,16);memcpy(native_fairplay_key,fairplay,16);','memcpy(native_nonce_key,fairplay,16);memcpy(native_fairplay_key,fairplay,16);'),
 ('fairplay-key-not-published','memcpy(native_fairplay_key,fairplay,16);','memset(native_fairplay_key,0,16);'),
 ('fairplay-cleared-on-published-cancel','if(__atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)==2) return;','if(__atomic_load_n(&native_nonce_state,__ATOMIC_ACQUIRE)==3) return;'),
]
def kdf_body(text,name):
 a=text.index('static bool '+name+'(');z=text.index('\n}\n',a)+3;return text[a:z]
assert kdf_body(s,'deriveNonceKey')==kdf_body(before.read_text(),'deriveNonceKey')
env=os.environ.copy();env['TMPDIR']='/home/albert/.cache' if os.name!='nt' else tempfile.gettempdir()
with tempfile.TemporaryDirectory(dir=env['TMPDIR']) as directory:
    d=Path(directory);receipt=[]
    for label,a,z in [('baseline',None,None)]+changes:
        text=s
        if a is not None:
            assert text.count(a)==1,(label,text.count(a));text=text.replace(a,z)
        cpp=d/(label+'.cpp');cpp.write_text(fixture(text));exe=d/label
        built=subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(cpp),'-lcrypto','-o',str(exe)],capture_output=True,text=True,env=env)
        assert built.returncode==0,(label,'compile failure is not a behavioral rejection',built.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
        assert (run.returncode==0)==(label=='baseline'),(label,run.stdout,run.stderr)
        receipt.append(dict(name=label,compiled=True,exit_code=run.returncode,stdout=run.stdout.strip()))
        if label=='baseline':(w/'production-test.cpp').write_text(cpp.read_text(),newline='\n');print(run.stdout.strip())
q=dict(passed=True,production_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256((w/'production-test.cpp').read_bytes()).hexdigest(),actual_production_CXX_bodies=True,IOKit_and_cache_are_models=True,low_level_AES_adapter='OpenSSL EVP; actual CBC glue/KDF/dispatch body retained; native backend untested here',original_nonce_KDF_byte_identical=True,compiled_behavioral_mutants=len(changes),runs=receipt)
(w/'test-fairplay-aes-result-0481.json').write_text(json.dumps(q,indent=2)+'\n')
print('PASS actual production C++ and',len(changes),'compiled behavioral mutants; no native or physical qualification')
