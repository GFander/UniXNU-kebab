"""Reproduce real-source input tests; IOKit/hardware/original parser are substitutes."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--qualification',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);a=p.parse_args()
q=a.qualification;cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True);os.environ['TMPDIR']=str(cache)
manifest=json.loads((q/('all-mac-source-0429.json' if(q/'all-mac-source-0429.json').exists()else'all-mac-source-0428.json')).read_text())['files']
names=['bsd/dev/kebab_hid_bridge.h','bsd/dev/kebab_hid_queue.h','bsd/dev/kebab_hid_reports.h','iokit/Kernel/arm/KebabHIDInput.cpp']
for n in names:assert hashlib.sha256((a.source/n).read_bytes()).hexdigest()==manifest[n],n
for n in names[:3]:shutil.copyfile(a.source/n,cache/Path(n).name)
for tag in ('core','lifecycle'):
 fixture=(q/('hid-input-'+tag+'-test.cpp')).read_bytes()
 receipt=json.loads((q/('test-hid-input-'+tag+'-result.json')).read_text())
 assert receipt['passed']and hashlib.sha256(fixture).hexdigest()==receipt['fixture_sha256']
 if tag=='lifecycle':
  production=(a.source/names[3]).read_text();production=production[production.index('static LCK_GRP_DECLARE'):]
  production=production.replace('class KebabHIDTransport :','struct KebabHIDTransport :').replace('class KebabHIDDevice :','struct KebabHIDDevice :').replace(' override','')
  assert production in fixture.decode()
 source=cache/(tag+'.cpp');source.write_bytes(fixture);exe=cache/tag
 subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-pthread','-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
mutants=[
 ('partial-start-cleanup','lifecycle.cpp','    originalStarted=true;','    originalStarted=false;'),
 ('failed-report-counted','lifecycle.cpp','if (result==kIOReturnSuccess) channels[self->role].sent++;','if (true) channels[self->role].sent++;'),
 ('stop-without-gate','lifecycle.cpp','if (loop) loop->runAction(stopAction,this);else stopAction(this,nullptr,nullptr,nullptr,nullptr);','stopAction(this,nullptr,nullptr,nullptr,nullptr);'),
 ('foreign-owner-read','kebab_hid_queue.h','owner!=q->owner || !q->count','!q->count'),
 ('missing-contact-lift','kebab_hid_reports.h','uint16_t was=prev?prev->active:0;','uint16_t was=0;'),
 ('invalid-contact-mask','kebab_hid_queue.h','if (f->keys || (f->active & ~1023u)) return 0;','if (f->keys) return 0;')]
for tag,name,old,new in mutants:
 folder=cache/tag;folder.mkdir(exist_ok=True)
 for n in ('core.cpp','lifecycle.cpp','kebab_hid_bridge.h','kebab_hid_queue.h','kebab_hid_reports.h'):shutil.copyfile(cache/n,folder/n)
 changed=folder/name;s=changed.read_text();assert old in s;changed.write_text(s.replace(old,new))
 source=folder/('lifecycle.cpp'if name.endswith('.cpp')else'core.cpp');exe=folder/'test'
 subprocess.run(['clang++','-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
 assert result.returncode!=0 and ('Assertion'in result.stderr or'assertion'in result.stderr),(tag,result.stdout,result.stderr)
 print('Rejected behavioral regression:',tag)
print('PASS published actual-source fixtures and6behavioral mutations; physical functionality not established by host tests')
