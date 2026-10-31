from pathlib import Path
import subprocess,json,hashlib
w=Path(__import__("sys").argv[1]).resolve()
p=w/'class-b-backend-production-test.c';s=p.read_text()
def run(label,code,expected):
 q=w/(label+'.c');q.write_text(code);exe=w/(label+'.bin')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w),str(q),str(w/'source/kebab_x25519.c'),'-lcrypto','-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
 (w/(label+'.stdout')).write_text(result.stdout);(w/(label+'.stderr')).write_text(result.stderr)
 assert (result.returncode==0)==expected,(label,result.stdout[-1400:],result.stderr[-2200:])
 if not expected:assert 'Assertion' in result.stderr,(label,result.stderr)
 print(label,result.stdout.strip().splitlines()[-1] if expected else 'behavioral mutant rejected')
 return result.stdout.strip().splitlines()[-1] if expected else label
summary=run('actual',s,True)
mutants=[
 ('ignore-private-lock','if(private_required&&!kbs_unlocked)','if(false)'),
 ('keep-private-on-lock','if(locked){kfk_wipe(kbs_private,32);kbs_unlocked=false;','if(locked){kbs_unlocked=false;'),
 ('unlock-via-bootstrap','kbs_ready&&kbs_factory_locked()?0:(int)kIOReturnNotReady','kbs_restore_locked()'),
 ('drop-envelope-identity','if(!error&&!kbn_identity(plain,cred))','if(false)'),
 ('overwrite-failed-unwrap','if(!error){memcpy(raw->key,plain+40,32);','if(true){memcpy(raw->key,plain+40,32);'),
 ('allow-locked-keychain','if(!error&&(cls==6||cls==9)&&(!kbs_ready||!kbs_bootstrap_done||!kbs_unlocked||!kbs_factory_locked()))','if(false)'),
 ('retain-private-after-broadcast-fail','if(!locked){kfk_wipe(kbs_private,32);kbs_unlocked=false;++kbs_generation;}','if(false){kfk_wipe(kbs_private,32);kbs_unlocked=false;++kbs_generation;}'),
 ('wrong-target-class','wrapped->key_len=128;wrapped->dp_class=2;','wrapped->key_len=128;wrapped->dp_class=3;'),
 ('wrong-keychain-purpose','memcpy(context+sizeof(domain)+56,&cls,4);','memset(context+sizeof(domain)+56,0,4);')]
for label,old,new in mutants:
 assert s.count(old)==1,(label,s.count(old));run(label,s.replace(old,new),False)
out=dict(passed=True,physical_test=False,production_code_tested=True,independent_B_vector=True,behavioral_mutants_rejected=len(mutants),explicit_models=['hardware Root restoration','VFS lock broadcast','mutex ownership','RNG','native CPX teardown not executed'],summary=summary,fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest())
(w/'test-class-b-backend-result-0466.json').write_text(json.dumps(out,indent=2)+'\n')
