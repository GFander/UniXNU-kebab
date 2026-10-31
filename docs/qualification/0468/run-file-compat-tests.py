from pathlib import Path
import subprocess,json,hashlib
w=Path(__import__("sys").argv[1]).resolve();p=w/'file-compat-production-test.c'
subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w),str(p),str(w/'source/kebab_x25519.c'),'-lcrypto','-o',str(w/'file-compat.bin')],check=True)
t=subprocess.run([str(w/'file-compat.bin')],capture_output=True,text=True,timeout=45);assert not t.returncode,(t.stdout[-1600:],t.stderr)
print(t.stdout.strip().splitlines()[-1])
(w/'test-class-b-file-compat-result-0468.json').write_text(json.dumps(dict(passed=True,physical_test=False,qualified_CD_KF02_HT01_metadata3_target_identity_vectors=True,legacy_B_refusal_changed_from_Unsupported_to_NotReady_without_B_bootstrap=True,full_candidate_core=True,fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),summary=t.stdout.strip().splitlines()[-1]),indent=2)+'\n')
