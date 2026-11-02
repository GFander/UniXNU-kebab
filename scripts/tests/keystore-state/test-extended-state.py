"""Test actual AKS state and selector dispatch with explicit IOKit/backend models.
No original binary, hardware keys, phone or private Data is used.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args();here=Path(__file__).resolve().parent
s=(a.source/'iokit/Kernel/arm/kebab_aks_userclient_impl.h').read_text();start=s.index('static IOReturn\nkuc_device_state_method(');end=s.index('\n#include "kebab_aks_keychain_impl.h"',start);helper=s[start:end];start=s.index('IOReturn\nAppleKeyStoreUserClient::externalMethod(');end=s.index('\nclass AppleKeyStore final',start);dispatch=s[start:end]
prefix=(here/'extended-state-model-prefix.cpp.inc').read_text();tests=(here/'extended-state-model-cases.cpp.inc').read_text()
variants=[('actual',helper,dispatch,True),('deny-extended',helper,dispatch.replace('&&selector!=35',''),False),('skip-permission',helper,dispatch.replace('else if(!administrative&&selector!=7','else if(false&&selector!=7'),False),('ordinary-only',helper,dispatch.replace('}else if(selector==17||selector==35){','}else if(selector==17){'),False),('failed-backend-success',helper.replace('if(result!=kIOReturnSuccess)return result;','if(result!=kIOReturnSuccess)return kIOReturnSuccess;'),dispatch,False),('wrong-no-pin',helper.replace("'s','l','s',0x02,0x01,0x03","'s','l','s',0x02,0x01,0x00"),dispatch,False)]
with tempfile.TemporaryDirectory(prefix='extended-state-')as folder:
 d=Path(folder)
 for name,h,b,good in variants:
  assert good or (h,b)!=(helper,dispatch)
  cpp=d/(name+'.cpp');exe=d/name;cpp.write_text(prefix+h+b+tests)
  subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True,capture_output=True)
  q=subprocess.run([str(exe)],capture_output=True,text=True);assert(q.returncode==0)==good,(name,q.stdout,q.stderr[-1000:])
  if good:assert 'PASS 1163 'in q.stdout
print(json.dumps(dict(passed=True,actual_dispatch_and_state_helper=True,cases=1163,compiled_runtime_mutants_rejected=5,explicit_IOKit_and_backend_models=True,source_sha256=hashlib.sha256(s.encode()).hexdigest())))
