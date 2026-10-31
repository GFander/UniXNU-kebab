from pathlib import Path
import subprocess,hashlib,json
w=Path(__import__("sys").argv[1]).resolve();p=w/'class-b-lifecycle-production-test.c';s=p.read_text()
def run(label,source,expected):
 q=w/(label+'.c');q.write_text(source);exe=w/(label+'.bin')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(q),'-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);assert(t.returncode==0)==expected,(label,t.stdout,t.stderr)
 if not expected:assert 'Assertion' in t.stderr
 print(label,t.stdout.strip().splitlines()[-1]if expected else'behavioral mutant rejected');return t.stdout.strip().splitlines()[-1]if expected else label
summary=run('actual',s,True)
mutants=[('recycle-CD','if(class_known&&attributes.va_dataprotect_class!=PROTECTION_CLASS_B)','if(false)'),('ignore-fsync-failure','if(sync_error){printf','if(false){printf'),('path-only-fallback','if(kebab_lookup_apfs_live_data(&system,&data)||!rootvnode)return 0;','if(false)return 0; memset(&system,0,sizeof(system));memset(&data,0,sizeof(data));'),('skip-real-uuid','if(identified){if(!kebab_apfs_b_volume_matches(volume.f_uuid))return 0;}','if(identified){if(false)return 0;}'),('skip-data-device','if(actual!=makedev(data.major_number,data.minor_number))','if(false)'),('skip-root-device','if(actual!=makedev(system.major_number,system.minor_number))','if(false)')]
for label,old,new in mutants:
 assert s.count(old)==1;run(label,s.replace(old,new),False)
(w/'test-class-b-lifecycle-result-0466.json').write_text(json.dumps(dict(passed=True,physical_test=False,actual_close_function=True,native_VFS_and_references_modeled=True,behavioral_mutants_rejected=len(mutants),summary=summary,fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest()),indent=2)+'\n')
