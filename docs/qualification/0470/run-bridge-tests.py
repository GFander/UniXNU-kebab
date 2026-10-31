from pathlib import Path
import hashlib,json,subprocess
w=Path(__import__("sys").argv[1]).resolve();core=w/'bridge-actual-core.c';driver=w/'bridge-actual-driver.cpp'
cflags=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w)]
subprocess.run(cflags+['-c',str(core),'-o',str(w/'bridge-core.o')],check=True)
subprocess.run(cflags+['-c',str(w/'source/kebab_x25519.c'),'-o',str(w/'bridge-math.o')],check=True)
source=driver.read_text()
def run(label,code,expected):
 p=w/(label+'.cpp');p.write_text(code);exe=w/(label+'.bin')
 subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),str(w/'bridge-core.o'),str(w/'bridge-math.o'),'-lcrypto','-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45);assert(t.returncode==0)==expected,(label,t.stdout[-1200:],t.stderr[-1500:])
 if not expected:assert 'Assertion'in t.stderr
 print(label,t.stdout.strip().splitlines()[-1]if expected else'behavioral mutant rejected');return t.stdout.strip().splitlines()[-1]if expected else label
summary=run('bridge-actual',source,True)
mutants=[('keep-io-local-FEK','for(unsigned i=0;i<sizeof(key);++i)bytes[i]=0;','for(unsigned i=0;i<sizeof(key);++i)bytes[i]=0xa5;'),('copy-ciphertext-as-key','int error=kebab_vek_cpx_handle_open(static_cast<const uint8_t *>(cpx_key(cpx)),crypto.key);','int error=0;memcpy(crypto.key,cpx_key(cpx),32);'),('ignore-handle-error','if(error)return static_cast<IOReturn>(error);','if(false)return static_cast<IOReturn>(error);')]
for label,old,new in mutants:
 assert source.count(old)==1;run(label,source.replace(old,new),False)
(w/'test-class-b-cpx-bridge-result-0470.json').write_text(json.dumps(dict(passed=True,physical_test=False,actual_B_bank_math_AES_and_driver=True,independent_full4096B_XTS_vector=True,actual_native_driver_destructor_executes_and_storage_zero_checked=True,behavioral_mutants_rejected=len(mutants),models=['bufattrs/CPX metadata','VFS lock broadcast','hardware Root restoration','mutex/RNG'],original_APFS_locked_descriptor_admission_not_modeled_as_success=True,summary=summary,fixture_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in(core,driver)}),indent=2)+'\n')
