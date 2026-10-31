"""Exercise the actual ADT transformer without device images or key material."""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json
r=Path(__file__).resolve().parent
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--transformer',type=Path,default=r/'adt-gop-display.py');ap.add_argument('--output',type=Path,default=r/'test-gop-display-adt-result.json');a=ap.parse_args()
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
g=load('gop_display',a.transformer);codec_path=g.r.parent/'cryptex-runtime-20261029/owned-memory.py';codec=g.load('gop_test_codec',codec_path)
fixture=([('name',b'device-tree\0')],[([('name',b'arm-io\0'),('device_type',b'arm-io\0')],[([('name',b'timer\0'),('reg',bytes(range(16)))],[])]),([('name',b'defaults\0'),('kebab,hid-input',b'\3\0\0\0'),('preserved-test-data',b'not-key-material')],[]),([('name',b'chosen\0'),('test-owner',bytes(range(32)))],[])])
original=codec.encode(fixture);after=g.change(original);tree,end=codec.parse(after)
assert end==len(after)and len(tree[1][0][1])==2
assert tree[1][0][1][-1]==g.NODE
assert g.change(after)==after
g.remove_node(tree,True);assert codec.encode(tree)==original
rejected=0
def refuse(tree):
 global rejected
 try:g.change(codec.encode(tree))
 except (AssertionError,ValueError):rejected+=1
 else:raise AssertionError('Invalid physical display description accepted')
t=copy.deepcopy(fixture);t[1][0][1].append(([('name',b'foreign\0'),('device_type',b'display-subsystem\0')],[]));refuse(t)
t=copy.deepcopy(fixture);t[1][0][1].append(([('name',b'foreign\0'),('device_type',b'ext-display-subsystem\0')],[]));refuse(t)
t,_=codec.parse(after);t[1][0][1][-1][0].append(('reg',b'foreign-address'));refuse(t)
t,_=codec.parse(after);t[1][0][1].append(copy.deepcopy(g.NODE));refuse(t)
t,_=codec.parse(after);t[1][0][1].append(([('name',b'foreign\0'),('device_type',b'display-subsystem\0')],[]));refuse(t)
t=copy.deepcopy(fixture);t[1][:]=t[1][1:];refuse(t)
result=dict(passed=True,source_sha256=hashlib.sha256(a.transformer.read_bytes()).hexdigest(),codec_sha256=hashlib.sha256(codec_path.read_bytes()).hexdigest(),exact_old_tree_preserved=True,idempotent=True,invalid_descriptions_rejected=rejected,device_images_or_keys_used=False,real_hardware_qualified=False)
a.output.write_text(json.dumps(result,indent=2)+'\n',newline='\n')
print('PASS actual transformer: prior ADT bytes preserved, idempotency and6 invalid display descriptions rejected; no hardware claim')
