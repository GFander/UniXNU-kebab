from pathlib import Path
import hashlib,json,subprocess,sys
w=Path(sys.argv[1]).resolve();source=(w/'class-a-backend-production-test.c').read_text()
def run(label,body,success):
 p=w/(label+'.c');p.write_text(body);exe=w/(label+'.bin')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w),str(p),str(w/'source/kebab_x25519.c'),'-lcrypto','-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
 (w/(label+'.stdout')).write_text(t.stdout);(w/(label+'.stderr')).write_text(t.stderr)
 assert (t.returncode==0)==success,(label,t.stdout[-1800:],t.stderr[-2500:])
 if not success:assert 'Assertion' in t.stderr,(label,t.stderr)
 print(label,t.stdout.strip().splitlines()[-1] if success else 'compiled behavioral mutant rejected',flush=True)
 return t.stdout.strip().splitlines()[-1] if success else label
summary=run('actual',source,True)
mutants=[
 ('wrong-A-purpose','UniXNU.kebab.complete-protection.class-key.v1','UniXNU.kebab.complete-protection.class-key.v2'),
 ('wrong-HA-purpose','UniXNU.kebab.complete-protection.cpx.v1','UniXNU.kebab.complete-protection.cpx.v2'),
 ('retain-A-on-lock','if(locked){kba_erase_locked();','if(locked){'),
 ('retain-A-on-revoke','kba_erase_locked();kbs_ready=false;','kbs_ready=false;'),
 ('retain-A-unlock-failure','if(!locked){kba_erase_locked();','if(!locked){'),
 ('wrong-A-format','if(cls==1)memcpy(out,"KF04",4);','if(cls==1)memcpy(out,"KF02",4);'),
 ('ignore-A-identity','if(!error&&!kfn_identity(plain,credential,cls))','if(false)'),
 ('deny-unlocked-HA','if(!kbs_unlocked||!kba_ready||!kdb_nonzero(kba_key,32))','if(true)'),
 ('ignore-A-RNG','if(cls==1&&(!kdb_nonzero(material,32)||!kdb_nonzero(material+32,16)))','if(false)'),
 ('wrong-A-target','kfn_plain(next,cred,target,plain+40);','kfn_plain(next,cred,3,plain+40);'),
]
for label,old,new in mutants:
 assert source.count(old)==1,(label,source.count(old));run(label,source.replace(old,new),False)
preserved=run('preserved-B',(w/'class-b-preservation-production-test.c').read_text(),True)
(w/'test-class-a-result-0470.json').write_text(json.dumps(dict(passed=True,physical_test=False,actual_production_core=True,independent_A_KF04_HA01_vectors=True,compiled_behavioral_mutants=len(mutants),summary=summary,preserved_B=preserved,models=['hardware Root restoration','VFS lock broadcast','mutex ownership','RNG'],original_APFS_cached_access_denial_unqualified=True,fixture_sha256={n:hashlib.sha256((w/n).read_bytes()).hexdigest() for n in ['class-a-backend-production-test.c','class-b-preservation-production-test.c']}),indent=2)+'\n')
