"""Check complete production observers and native Apple identity/ACL functions.
Kernel/task/VFS and host USB models are explicit; device qualification separate.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
for n in ('source','qualification','cache'):ap.add_argument('--'+n,type=Path,required=True)
a=ap.parse_args();q=a.qualification
sha=lambda b:hashlib.sha256(b).hexdigest()
manifest=json.loads((q/'all-mac-source-0459.json').read_text())['files']
observer=(q/'observer-drain-production-test.cpp').read_text()
receipt=json.loads((q/'test-observer-drain-result-0456.json').read_text());assert receipt['passed']
assert sha(observer.encode())==receipt['fixture_sha256']
for name in ('osfmk/kern/kebab_bootdiag.c','bsd/kern/kebab_bootdiag.c'):
 text=(a.source/name).read_text();assert sha(text.encode())==manifest[name]
 text=re.sub(r'^#include [^\n]+\n','',text,flags=re.M)
 for old,new in [('unsigned *count = arg;','unsigned *count = (unsigned *)arg;'),('struct kebab_backboard_snapshot *snapshot = argument;','struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;'),('struct kebab_ui_snapshot *snapshot = argument;','struct kebab_ui_snapshot *snapshot = (struct kebab_ui_snapshot *)argument;'),('struct kebab_migrator_snapshot *snapshot = argument;','struct kebab_migrator_snapshot *snapshot = (struct kebab_migrator_snapshot *)argument;')]:
  text=text.replace(old,new)
 assert text in observer,name
 if name.startswith('osfmk'):assert sha((a.source/name).read_bytes())==receipt['production_sha256']
identity=(q/'native-ios-identity-production-test.c').read_text()
ireceipt=json.loads((q/'test-native-ios-identity-result-0455.json').read_text());assert ireceipt['passed']
assert sha(identity.encode())==ireceipt['fixture_sha256']
sources={n:(a.source/n).read_text()for n in ireceipt['source_sha256']}
for n,s in sources.items():assert sha(s.encode())==manifest[n]==ireceipt['source_sha256'][n]
for n,h in ireceipt['production_function_sha256'].items():
 s=sources['bsd/kern/kern_authorization.c'if n=='kauth_acl_evaluate'else'bsd/kern/kern_credential.c']
 pos=s.index('\n'+n+'(')+1;start=s.rfind('\n',0,pos-1)+1;end=s.index('\n}\n',pos)+3
 body=s[start:end];assert sha(body.encode())==h and body in identity,n
config=a.source/'config/MASTER.arm64.kebab'
assert sha(config.read_bytes())==manifest['config/MASTER.arm64.kebab']
assert 'config_ext_resolver'not in config.read_text()
usb=a.source/'bsd/dev/kebab_usb.c';assert sha(usb.read_bytes())==manifest['bsd/dev/kebab_usb.c']
variants=[]
def change(name,old,new):
 assert observer.count(old)==1,(name,observer.count(old));variants.append((name,observer.replace(old,new)))
change('omit-thread-drain','thread_deallocate(thread);\n\t\tkebab_observer_drain();','thread_deallocate(thread);')
change('omit-initial-drain','task_unlock(task);\n\tkebab_observer_drain();','task_unlock(task);')
change('omit-final-drain','not user PCs\\n", label);\n\tkebab_observer_drain();','not user PCs\\n", label);')
change('drain-under-thread-lock','\t\tthread_unlock(thread);\n\t\tsplx(priority);','\t\tkebab_observer_drain();\n\t\tthread_unlock(thread);\n\t\tsplx(priority);')
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='identity-observer-',dir=a.cache)as td:
 for name,text,is_c,positive in [('actual-observer',observer,False,True),('actual-identity',identity,True,True)]+[(n,s,False,False)for n,s in variants]:
  p=Path(td)/(name+('.c'if is_c else'.cpp'));p.write_text(text);exe=p.with_suffix('')
  args=['clang'if is_c else'clang++','-std=c11'if is_c else'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
  args+=['-Wno-constant-conversion']if is_c else['-Wno-unused-function','-Wno-missing-field-initializers']
  subprocess.run(args+[str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
  if positive:assert result.returncode==0,(name,result.stdout,result.stderr);print(result.stdout.strip())
  else:assert result.returncode and 'Assertion'in result.stderr,(name,result.stdout,result.stderr);print('Rejected',name)
print('PASS exact complete source fixtures, finite-ring old-loss reproduction/byte-exact reading-host delivery, strict native Apple identity/ACL; no hardware delivery claim for a nonreading host')
