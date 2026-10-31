from pathlib import Path
import hashlib,json,subprocess
w=Path(__import__("sys").argv[1]).resolve();p=w/'class-b-client-production-test.cpp';s=p.read_text()
def run(label,code,expected):
 q=w/(label+'.cpp');q.write_text(code);exe=w/(label+'.bin')
 subprocess.run(['clang++','-std=c++17','-pthread','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(q),'-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
 (w/(label+'.stdout')).write_text(t.stdout);(w/(label+'.stderr')).write_text(t.stderr)
 assert (t.returncode==0)==expected,(label,t.stdout[-900:],t.stderr[-2000:])
 if not expected:assert 'Assertion' in t.stderr,(label,t.stderr)
 print(label,t.stdout.strip().splitlines()[-1]if expected else'behavioral mutant rejected')
 return t.stdout.strip().splitlines()[-1]if expected else label
summary=run('actual',s,True)
mutants=[('root-device-bypass','client->deviceAccess=(entitlement==kOSBooleanTrue);','client->deviceAccess=administrative||(entitlement==kOSBooleanTrue);'),('accept-nonboolean','client->deviceAccess=(entitlement==kOSBooleanTrue);','client->deviceAccess=(entitlement!=nullptr);'),('promote-device-admin','client->administrative=administrative;','client->administrative=administrative||(task==(void*)6);'),('skip-device-authorization','(selector==12||selector==13)&&!deviceAccess','(selector==99)&&!deviceAccess'),('accept-nonempty-secret','if(selector==12&&a->scalarInput[2])','if(false)'),('hardcode-unlocked','*state=deviceLocked?5:4;','*state=4;'),('ignore-lock-error','return (IOReturn)kebab_device_keybag_set_locked(selector==13);','(void)kebab_device_keybag_set_locked(selector==13);return kIOReturnSuccess;')]
for label,old,new in mutants:
 assert s.count(old)==1,(label,s.count(old));run(label,s.replace(old,new),False)
out=dict(passed=True,physical_test=False,complete_client=True,models=['IOKit','AMFI entitlement lookup','keychain crypto','device lock backend'],legacy_keychain_cases_retained=True,strict_separate_device_capability=True,behavioral_mutants_rejected=len(mutants),summary=summary,fixture_sha256=hashlib.sha256(p.read_bytes()).hexdigest())
(w/'test-class-b-client-result-0466.json').write_text(json.dumps(out,indent=2)+'\n')
