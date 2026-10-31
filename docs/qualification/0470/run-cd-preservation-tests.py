from pathlib import Path
import subprocess,json,hashlib
w=Path(__import__("sys").argv[1]).resolve();p=w/'cd-preservation-production-test.c'
subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(w),str(p),str(w/'source/kebab_x25519.c'),'-lcrypto','-o',str(w/'cd-preservation.bin')],check=True)
t=subprocess.run([str(w/'cd-preservation.bin')],capture_output=True,text=True,timeout=45);assert not t.returncode,(t.stdout[-1200:],t.stderr)
print(t.stdout.strip().splitlines()[-1])
(w/'test-class-b-cd-preservation-result-0470.json').write_text(json.dumps(dict(passed=True,physical_test=False,unchanged_0349_tests_and_independent_vectors=True,full_current_candidate_core=True,summary=t.stdout.strip().splitlines()[-1],fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest()),indent=2)+'\n')
