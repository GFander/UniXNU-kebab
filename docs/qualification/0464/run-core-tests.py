from pathlib import Path
import subprocess,json,hashlib
w=Path(__import__("sys").argv[1]).resolve();p=w/'keychain-core-production-test.c';s=p.read_text()
def run(label,source,expected):
 q=w/(label+'.c');q.write_text(source);exe=w/(label+'.bin')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w),str(q),'-lcrypto','-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
 assert (t.returncode==0)==expected,(label,t.stdout,t.stderr)
 if expected:print(t.stdout.strip());return t.stdout.strip()
 assert 'Assertion' in t.stderr,(label,t.stderr);print('Rejected behavioral mutant',label)
 return label
result=run('actual',s,True)
start=s.index('int\nkebab_keychain_key_crypt(');end=s.index('\n}\n',start)+3;body=s[start:end]
mutants=[('root-not-ready','(kfk_ready&&kdb_ready)','(kdb_ready)'),('bank-not-ready','(kfk_ready&&kdb_ready)','(kfk_ready)'),('allow-class9','cls!=7&&cls!=8&&cls!=10&&cls!=11','cls!=7&&cls!=8&&cls!=9&&cls!=10&&cls!=11'),('wrong-policy','policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE','false'),('ignore-key-class','&cls,4','&version,4'),('ignore-bag-id','memcpy(context+sizeof(domain),kdb_identifier,16);','memset(context+sizeof(domain),0,16);'),('ignore-volume-id','memcpy(context+sizeof(domain)+16,kdb_record+24,32);','memset(context+sizeof(domain)+16,0,32);'),('overwrite-on-failure','if(!error)memcpy(output,result,output_bytes);','memcpy(output,result,output_bytes);'),('allow-foreign-bag','!kfk_context()||bag||','!kfk_context()||(bag==99)||')]
for label,old,new in mutants:
 assert body.count(old)==1;(run(label,s.replace(body,body.replace(old,new)),False))
out=dict(passed=True,component_only=True,physical_test=False,root_locks_context_rng_and_CPX_teardown_explicit_models=True,actual_pinned_bank_and_AES_code=True,independent_RFC3394_and_domain_HMAC_vectors=4,behavioral_mutants_rejected=len(mutants),production_fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),current_public_header_sha256=hashlib.sha256((w/'kebab_file_keys.h').read_bytes()).hexdigest(),summary=result,classB_implemented=False,AK_AKU_passcode_implemented=False,securityd_authorization_changed=False)
(w/'test-keychain-core-result-0464.json').write_text(json.dumps(out,indent=2)+'\n')
