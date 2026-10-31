"""Exercise the actual display-scale transformer using synthetic ADT only."""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,struct
r=Path(__file__).resolve().parent
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--transformer',type=Path,default=r/'adt-display-scale.py');ap.add_argument('--output',type=Path,default=r/'test-display-scale-adt-result-0443.json');a=ap.parse_args()
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
g=load('display_scale',a.transformer);codec_path=g.r/'adt-codec.py';codec=load('adt_codec_test',codec_path)
fixture=([('name',b'device-tree\0')],[([('name',b'chosen\0'),('synthetic-owner',bytes(range(32)))],[]),([('name',b'product\0'),('product-name',b'OnePlus 8T\0')],[]),([('name',b'arm-io\0')],[([('name',b'kebab-display\0'),('device_type',b'display-subsystem\0')],[])]),([('name',b'defaults\0'),('kebab,hid-input',b'\3\0\0\0'),('synthetic-test-policy',b'unchanged')],[])])
original=codec.encode(fixture);after=g.change(original);tree,end=codec.parse(after)
assert end==len(after)and dict(g.chosen(tree)[0])['display-scale']==struct.pack('<I',3)
assert g.change(after)==after
g.remove_scale(tree,True);assert codec.encode(tree)==original
rejected=0
def refuse(data):
 global rejected
 try:g.change(data)
 except(AssertionError,ValueError,struct.error):rejected+=1
 else:raise AssertionError('Invalid scale/ADT accepted')
for value in [struct.pack('<I',1),struct.pack('<I',2),struct.pack('<f',3),struct.pack('<Q',3)]:
 t=copy.deepcopy(fixture);g.chosen(t)[0].append(('display-scale',value));refuse(codec.encode(t))
t=copy.deepcopy(fixture);t[1].pop(0);refuse(codec.encode(t))
t=copy.deepcopy(fixture);t[1].append(copy.deepcopy(t[1][0]));refuse(codec.encode(t))
t=copy.deepcopy(fixture);g.chosen(t)[0].extend([('display-scale',struct.pack('<I',3)),('display-scale',struct.pack('<I',3))]);refuse(codec.encode(t))
refuse(original+b'\0\0\0\0');refuse(b'\0')
result=dict(passed=True,transformer_sha256=hashlib.sha256(a.transformer.read_bytes()).hexdigest(),codec_sha256=hashlib.sha256(codec_path.read_bytes()).hexdigest(),exact_old_tree_preserved=True,idempotent=True,invalid_inputs_rejected=rejected,property='/chosen/display-scale',type='little-endian32bit integer',value=3,dimensionless_ui_scale_not_panel_refresh=True,device_images_or_keys_used=False,hardware_refresh_qualified=False)
a.output.write_text(json.dumps(result,indent=2)+'\n',newline='\n')
print('PASS actual transformer: integer3, every old tree byte preserved, idempotent,9 invalid ADTs/scales rejected; no hardware refresh claim')
