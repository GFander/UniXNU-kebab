"""Compile exact native C core and complete IOKit client; verify21 behavior mutants."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;manifest=json.loads((q/'all-mac-source-0448.json').read_text())['files']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
core=json.loads((q/'test-device-state-core-result-0447.json').read_text())
client=json.loads((q/'test-aks-readonly-userclient-result-0448.json').read_text())
assert core['passed']and client['passed']
def read(name):
 p=a.source/name;assert sha(p)==manifest[name];return p.read_text()
def expand(name):
 text=read(name);out=[]
 for line in text.splitlines(True):
  if line.startswith('#include "'):
   child=line.split('"')[1]
   out.append(line if child=='kebab_file_keys.h'else expand((Path(name).parent/child).as_posix()))
  elif not line.startswith('#include'):out.append(line)
 return ''.join(out)
csource=expand('bsd/kern/kebab_file_keys.c')
for name,digest in core['source_files'].items():assert manifest[name]==digest
header=read('bsd/kern/kebab_file_keys.h');assert hashlib.sha256(header.encode()).hexdigest()==core['header_sha256']==client['status_header_sha256']
original=read('iokit/Kernel/arm/kebab_aks_userclient_impl.h');cookie=read('iokit/Kernel/arm/kebab_aks_cookie_impl.h')
assert hashlib.sha256(original.encode()).hexdigest()==client['native_userclient_sha256']
assert hashlib.sha256(cookie.encode()).hexdigest()==client['cookie_header_sha256']
cppsource='\n'.join(line for line in original.replace('#include "kebab_aks_cookie_impl.h"',cookie).splitlines()if not line.startswith('#include'))
cfixture=(q/'device-state-core-production-test.c').read_text();cppfixture=(q/'aks-readonly-userclient-production-test.cpp').read_text()
assert sha(q/'device-state-core-production-test.c')==core['fixture_sha256']and csource in cfixture
assert sha(q/'aks-readonly-userclient-production-test.cpp')==client['fixture_sha256']and cppsource in cppfixture
variants=[]
def variant(name,old,new):
 assert s.count(old)==1;variants.append((name,s.replace(old,new),False,kind))
s=cfixture;kind='c';variants.append(('actual-core',s,True,kind))
at=s.index('int\nkebab_device_classbag_get_status(');end=s.index('\n}\n',at)+3;body=s[at:end]
variant('ignore-bank-ready',body,body.replace('(kfk_ready&&kdb_ready)','(kfk_ready)'))
variant('ignore-root-ready',body,body.replace('(kfk_ready&&kdb_ready)','(kdb_ready)'))
variant('wrong-core-policy',body,body.replace('status.policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE','false'))
variant('wrong-core-version',body,body.replace('status.version!=1','false'))
variant('missing-class-c',body,body.replace('if(kdb_nonzero(kdb_class_c,sizeof(kdb_class_c)))','if(true)'))
variant('duplicate-classes',body,body.replace('!memcmp(kdb_class_c,kdb_class_d,sizeof(kdb_class_c))','false'))
variant('no-context',body,body.replace('!kfk_context()||',''))
variant('overwrite-failure',body,body.replace(' if(!error){\n  memcpy(&status.version',' memcpy(output,&status,sizeof(status));\n if(!error){\n  memcpy(&status.version'))
variant('record-alias',body,body.replace('kfk_overlap(output,sizeof(*output),kdb_record,sizeof(kdb_record))||',''))
s=cppfixture;kind='cpp';variants.append(('actual-client',s,True,kind))
start=s.index('static IOReturn\nkuc_device_state_method(');end=s.index('\n}\n',start)+3;body=s[start:end]
variant('wrong-client-policy',body,body.replace('status.policy!=KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE','false'))
variant('wrong-client-version',body,body.replace('status.version!=1','false'))
variant('wrong-classes',body,body.replace('status.available_classes!=KEBAB_DEVICE_BAG_CLASS_C_D_MASK','false'))
variant('unlocked-instead-of-factory-no-pin',body,body.replace("'s','l','s',0x02,0x01,0x03","'s','l','s',0x02,0x01,0x00"))
variant('missing-capacity',body,body.replace('capacity<bytes||','(capacity<bytes&&false)||'))
variant('ambiguous-output',body,body.replace('(a->structureOutputDescriptor&&a->structureOutput)','false'))
variant('failed-backend-success',body,body.replace('if(result!=kIOReturnSuccess)return result;','if(result!=kIOReturnSuccess)return kIOReturnSuccess;'))
variant('short-descriptor-success',body,body.replace('if(a->structureOutputDescriptor->writeBytes(0,state,bytes)!=bytes)return kIOReturnIOError;','(void)a->structureOutputDescriptor->writeBytes(0,state,bytes);'))
variant('promote-public-clients','client->administrative=administrative;','client->administrative=administrative||true;')
variant('deny-administrative-clients','client->administrative=administrative;','client->administrative=administrative&&false;')
variant('skip-operation-authorization','else if(!administrative&&selector!=7&&selector!=14&&selector!=17)','else if(!administrative&&false&&selector!=7&&selector!=14&&selector!=17)')
variant('deny-public-state','else if(!administrative&&selector!=7&&selector!=14&&selector!=17)','else if(!administrative&&selector!=7&&selector!=14)')
assert len(variants)==23
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='authenticated-device-state-',dir=a.cache)as td:
 root=Path(td);(root/'kebab_file_keys.h').write_text(header)
 for name,text,ok,kind in variants:
  p=root/(name+'.'+kind);p.write_text(text);exe=p.with_suffix('')
  command=['clang++','-std=c++17','-pthread']if kind=='cpp'else['clang','-std=c11','-Wno-unused-function','-Wno-deprecated-declarations']
  subprocess.run([*command,'-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),*(['-lcrypto']if kind=='c'else[]),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
  assert(result.returncode==0)==ok,(name,result.stdout,result.stderr)
  if ok:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected',name)
print('PASS exact current C-core/header/complete IOKit source,8452core+16572client cases,21behavioralmutants; libc/OpenSSL/context/IOKit/backend stand-ins explicit, no physical claim')
