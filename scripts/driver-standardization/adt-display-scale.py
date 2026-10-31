"""Declare the existing3x UI policy for the original MobileGestalt consumer.

This dimensionless UI scale is not panel density, refresh rate or GPU support.
The matched original consumer reads /chosen/display-scale as an integer.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,struct,sys
r=Path(__file__).resolve().parent
PROPERTY='display-scale';VALUE=struct.pack('<I',3)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def chosen(tree):
 nodes=[node for node in tree[1]if dict(node[0]).get('name')==b'chosen\0']
 assert len(nodes)==1,'Require the one existing chosen node';return nodes[0]
def remove_scale(tree,required):
 props=chosen(tree)[0];matches=[(i,value)for i,(name,value)in enumerate(props)if name==PROPERTY]
 assert len(matches)==int(required),'Unexpected display-scale property count'
 if matches:
  i,value=matches[0];assert value==VALUE,'Different scale must be audited separately';del props[i]
def change(adt):
 codec=load('display_scale_codec',r/'adt-codec.py')
 tree,end=codec.parse(adt);assert end==len(adt)and codec.encode(tree)==adt
 props=chosen(tree)[0];matches=[value for name,value in props if name==PROPERTY]
 if matches:
  assert matches==[VALUE];return adt
 props.append((PROPERTY,VALUE));result=codec.encode(tree)
 remove_scale(tree,True);assert codec.encode(tree)==adt,'Every pre-existing property and node must survive exactly'
 return result
def main():
 sys.path.insert(0,str(r.parent/'p1-run'))
 from verify_image_lib import Fat32
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('destination',type=Path);a=ap.parse_args()
 assert a.source.resolve()!=a.destination.resolve()and not a.destination.exists()
 payload=a.source.read_bytes();fs=Fat32(payload);files={name:data for name,data,_,_ in fs.walk(fs.root)}
 key='EFI/UniXNU/Payloads/kernel.macho.adt';new=change(files[key])
 pack=load('display_scale_pack',r.parent/'pack-payload.py');out=pack.Fat32(bytearray(payload));pack.replace_file(out,tuple(key.split('/')),new)
 final=Fat32(out.data);actual={name:data for name,data,_,_ in final.walk(final.root)}
 assert actual.keys()==files.keys()and actual[key]==new
 assert all(actual[name]==data for name,data in files.items()if name!=key)
 a.destination.write_bytes(out.data)
 receipt=dict(only_adt_property='/chosen/display-scale',type='little-endian32bit integer',value=3,dimensionless_ui_policy=True,all_prior_adt_bytes_preserved=True,all_other_fat_files_identical=True,panel_density_or_hardware_refresh_changed=False,gpu_or_presentation_qualified=False,before_adt_sha256=hashlib.sha256(files[key]).hexdigest(),after_adt_sha256=hashlib.sha256(new).hexdigest())
 a.destination.with_suffix('.scale.json').write_text(json.dumps(receipt,indent=2)+'\n',newline='\n')
 print('Declared existing3x UI policy for matched original /chosen/display-scale consumer; other files unchanged')
if __name__=='__main__':main()
