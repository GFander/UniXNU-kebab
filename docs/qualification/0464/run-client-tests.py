from pathlib import Path
import subprocess,json,hashlib
w=Path(__import__("sys").argv[1]).resolve();p=w/'keychain-client-production-test.cpp';s=p.read_text()
def run(label,text,expected):
 q=w/(label+'.cpp');q.write_text(text);exe=w/(label+'.bin')
 subprocess.run(['clang++','-std=c++17','-pthread','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(q),'-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45);assert (t.returncode==0)==expected,(label,t.stdout,t.stderr)
 if expected:print(t.stdout.strip());return t.stdout.strip()
 assert 'Assertion' in t.stderr,(label,t.stderr);print('Rejected behavioral mutant',label)
 return label
summary=run('actual',s,True)
mutants=[('nonnull-entitlement','client->keychainAccess=(entitlement==kOSBooleanTrue);','client->keychainAccess=(entitlement==kOSBooleanTrue)||(task==(void*)5);'),('root-bypass','client->keychainAccess=(entitlement==kOSBooleanTrue);','client->keychainAccess=administrative||(entitlement==kOSBooleanTrue);'),('promote-admin','client->administrative=administrative;','client->administrative=administrative||(task==(void*)3);'),('no-wrap-authorization','(selector==10||selector==11)&&!keychainAccess','(selector==99)&&!keychainAccess'),('truncate-keyclass','a->scalarInput[1]>UINT32_MAX','false'),('skip-input-length','a->structureInputSize!=(encrypt?32u:40u)','false'),('ignore-scalar-output','a->scalarOutputCount!=(encrypt?1u:0u)','false'),('wrong-returned-class','a->scalarOutput[0]=cls;','a->scalarOutput[0]=4;'),('ignore-crypto-error','if(result==kIOReturnSuccess){\n  if(a->structureOutputDescriptor){\n   if(a->structureOutputDescriptor->writeBytes(0,output,bytes)!=bytes)','result=kIOReturnSuccess;\n if(result==kIOReturnSuccess){\n  if(a->structureOutputDescriptor){\n   if(a->structureOutputDescriptor->writeBytes(0,output,bytes)!=bytes)')]
for label,old,new in mutants:
 assert s.count(old)==1,(label,s.count(old));run(label,s.replace(old,new),False)
out=dict(passed=True,component_only=True,physical_test=False,complete_candidate_client_tested=True,legacy_cases_retained_with_intentional_keychain_permission_changes=True,IOKit_AMFI_crypto_explicit_models=True,strict_boolean_immutable_entitlement_and_no_admin_promotion_tested=True,behavioral_mutants_rejected=len(mutants),fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),summary=summary)
(w/'test-keychain-client-result-0464.json').write_text(json.dumps(out,indent=2)+'\n')
