from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,struct,subprocess,sys,tempfile

p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);p.add_argument('--measured-adt',type=Path);a=p.parse_args();repo=a.repo.resolve();w=a.receipt_dir;w.mkdir(parents=True,exist_ok=True)
def mod(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
codec=mod('codec',repo/'scripts/driver-standardization/adt-codec.py')
identity=mod('identity',repo/'scripts/driver-standardization/adt-iphone-identity.py')
checks=[]
def check(name,condition):
 assert condition,name
 checks.append(name)
def props(tree):return dict(tree[0])
def child(tree,name):return next(n for n in tree[1]if props(n).get('name')==name.encode()+b'\0')
def canonical(raw):
 tree,end=codec.parse(raw);assert end==len(raw)and codec.encode(tree)==raw;return tree
def normalized(tree):
 tree=copy.deepcopy(tree)
 chosen=child(tree,'chosen');chosen[0][:]=[(k,v)for k,v in chosen[0]if k not in('random-seed','boot-nonce')]
 return codec.encode(tree)
profile=repo/'configs/d22ap-product.json';profile_json=json.loads(profile.read_text())
with tempfile.TemporaryDirectory()as tmp:
 tmp=Path(tmp);counter=0
 def generate(script,mode=(),profile_path=profile,expect=True):
  global counter
  counter+=1;out=tmp/str(counter)
  p=subprocess.run([sys.executable,str(script),'--dram-size','0x300000000','--output',str(out),'--product-file',str(profile_path),*mode],capture_output=True,text=True)
  assert (p.returncode==0)==expect,(p.returncode,p.stderr)
  if not expect:assert not out.exists();return p.stderr
  return canonical(out.read_bytes())
 new=repo/'scripts/make-kebab-adt.py';old=tmp/'baseline-generator.py'
 baseline=subprocess.check_output(['git','-C',str(repo),'show','5f8dd62fafc3fff9f00b1c91c5f1c4c37a2c6453:scripts/make-kebab-adt.py']);assert hashlib.sha256(baseline).hexdigest()=='7c6abac46a3f6e0ca588a8bc1e80a13933bba51acdda3ca3fa4b7289152b76be';old.write_bytes(baseline)
 phone=generate(new,['--iphone-identity']);check('iPhone root model/target',props(phone)['model']==b'iPhone10,3\0'and props(phone)['target-type']==b'D22\0')
 p=props(child(phone,'product'));check('typed original product',p['product-name']==b'iPhone X\0'and p['product-description']==b'iPhone X\0'and p['artwork-device-idiom']==b'phone\0'and p['artwork-scale-factor']==struct.pack('<I',3))
 b=props(child(phone,'buttons'));check('no mechanical Home / typed full-screen',b['button-names']==b'hold\0voldown\0volup\0'and b['home-button-type']==struct.pack('<I',2))
 check('Qualcomm compatible retained',props(phone)['compatible']==b'oneplus,kebab\0qcom,sm8250\0qcom,kona\0')
 for name,flags in [('default',[]),('iPad',['--ipad-identity'])]:check(name+' unchanged except random seed',normalized(generate(new,flags))==normalized(generate(old,flags)))
 check('mutually exclusive identities', 'Choose exactly one' in generate(new,['--iphone-identity','--ipad-identity'],expect=False))
 check('missing profile fails explicitly', 'profile is missing' in generate(new,['--iphone-identity'],tmp/'absent',False))
 for name,mutate in [('duplicate',lambda x:x['props'].append(copy.deepcopy(x['props'][0]))),('reserved-name',lambda x:x['props'].append(dict(k='name',t='str',v='bad'))),('board-model-conflict',lambda x:x['props'].append(dict(k='model',t='str',v='bad'))),('string-scale',lambda x:next(p for p in x['props']if p['k']=='artwork-scale-factor').update(t='str',v='3'))]:
  bad=copy.deepcopy(profile_json);mutate(bad);path=tmp/(name+'.json');path.write_text(json.dumps(bad));generate(new,['--iphone-identity'],path,False);check(name+' rejected without ADT',True)
 path=tmp/'bad-json';path.write_text('{');generate(new,['--iphone-identity'],path,False);check('malformed JSON rejected',True)
 actual=a.measured_adt.read_bytes()if a.measured_adt else codec.encode(generate(new,[]));updated=identity.update(actual,profile)
 check('production ADT canonical roundtrip',codec.encode(canonical(updated))==updated)
 check('identity-only transform idempotent',identity.update(updated,profile)==updated)
 before=canonical(actual);after=canonical(updated)
 check('chosen byte-identical before owner remeasure',codec.encode(child(before,'chosen'))==codec.encode(child(after,'chosen')))
 def hardware(tree):
  tree=copy.deepcopy(tree);tree[0][:]=[(k,v)for k,v in tree[0]if k not in('model','target-type')];tree[1][:]=[n for n in tree[1]if props(n).get('name')not in(b'product\0',b'buttons\0')];return codec.encode(tree)
 check('all hardware nodes unchanged',hardware(before)==hardware(after))
 check('actual phone transform matches generated identity',props(after)['model']==props(phone)['model']and props(after)['target-type']==props(phone)['target-type']and props(child(after,'buttons'))==props(child(phone,'buttons')))

result=dict(passed=True,fixture_kind='provided runtime ADT'if a.measured_adt else'synthetic production-generator ADT',checks=checks,count=len(checks),generator_sha256=hashlib.sha256(new.read_bytes()).hexdigest(),actual_source_sha256=hashlib.sha256(actual).hexdigest(),result_sha256=hashlib.sha256(updated).hexdigest(),runtime_verified=False)
(w/'host-identity-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
