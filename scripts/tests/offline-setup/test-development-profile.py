from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,struct,subprocess,sys,tempfile
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);a=p.parse_args();a.receipt_dir.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('codec',a.repo/'scripts/driver-standardization/adt-codec.py');codec=importlib.util.module_from_spec(spec);spec.loader.exec_module(codec)
keys={'certificate-production-status':0,'effective-production-status-ap':0,'certificate-security-mode':1,'effective-security-mode-sep':1};checks=[]
def child(t,name):return next(n for n in t[1]if dict(n[0]).get('name')==name.encode()+b'\0')
def normalized(t):
 t=copy.deepcopy(t);c=child(t,'chosen');c[0][:]=[(k,v)for k,v in c[0]if k!='random-seed'];return codec.encode(t)
with tempfile.TemporaryDirectory()as d:
 d=Path(d);counter=0
 def generate(script,flags,ok=True):
  global counter
  counter+=1;out=d/str(counter);q=subprocess.run([sys.executable,str(script),'--dram-size','0x300000000','--output',str(out),'--product-file',str(a.repo/'configs/d22ap-product.json'),*flags],capture_output=True,text=True);assert(q.returncode==0)==ok,(q.returncode,q.stderr)
  if not ok:assert not out.exists();return
  raw=out.read_bytes();t,end=codec.parse(raw);assert end==len(raw)and codec.encode(t)==raw;return t
 current=a.repo/'scripts/make-kebab-adt.py'
 for flags in ([],['--iphone-identity'],['--ipad-identity']):
  assert normalized(generate(current,flags))==normalized(generate(a.baseline,flags));checks.append('unchanged default:'+str(flags))
 phone=generate(current,['--iphone-identity']);dev=generate(current,['--iphone-identity','--development-activation-profile','20H392']);props=dict(child(dev,'chosen')[0]);assert all(props[k]==struct.pack('<I',v)for k,v in keys.items());checks.append('four exact little-endian properties')
 assert not any(k in dict(child(phone,'chosen')[0])for k in keys);checks.append('iPhone default profile absent')
 cleaned=copy.deepcopy(dev);c=child(cleaned,'chosen');c[0][:]=[(k,v)for k,v in c[0]if k not in keys];assert normalized(cleaned)==normalized(phone);checks.append('only four chosen properties changed; entire hardware and identity retained')
 assert dict(dev[0])['model']==b'iPhone10,3\0'and dict(dev[0])['target-type']==b'D22\0';checks.append('iPhone10,3 D22 logical identity')
 for flags in (['--development-activation-profile','20H392'],['--ipad-identity','--development-activation-profile','20H392'],['--iphone-identity','--development-activation-profile','21H461']):generate(current,flags,False);checks.append('invalid profile/mode refused:'+str(flags))
result=dict(passed=True,checks=checks,generator_sha256=hashlib.sha256(current.read_bytes()).hexdigest(),baseline_sha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest(),profile='20H392',default_off_for_iPhone=True,device_deployed=False,activation_success_proven=False)
(a.receipt_dir/'host-development-profile-tests.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(checks),'generator profile checks; no device activation claim')
