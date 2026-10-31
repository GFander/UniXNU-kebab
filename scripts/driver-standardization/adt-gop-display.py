"""Describe the one existing firmware GOP screen for original IOMFB enumeration.

This is an IODeviceTree declaration only. It does not describe Apple display
hardware, add MMIO/IRQ/SMMU resources, or qualify rendering/presentation.
"""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,sys
r=Path(__file__).resolve().parent
sys.path.insert(0,str(r.parent/'p1-run'))
from verify_image_lib import Fat32

NODE=([('name',b'kebab-display\0'),('device_type',b'display-subsystem\0'),
       ('compatible',b'unixnu,kebab-gop-display\0')],[])

def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def remove_node(tree,required):
 armio,=[c for c in tree[1]if dict(c[0]).get('name')==b'arm-io\0']
 matches=[c for c in armio[1]if dict(c[0]).get('name')==b'kebab-display\0']
 assert len(matches)==int(required),'Unexpected GOP display node count'
 if matches:
  assert matches[0]==NODE,'Foreign properties/resources on fixed GOP description'
  armio[1].remove(matches[0])
 return armio

def change(adt):
 codec=load('gop_adt_codec',r.parent/'cryptex-runtime-20261029/owned-memory.py')
 tree,end=codec.parse(adt);assert end==len(adt)and codec.encode(tree)==adt
 armio,=[c for c in tree[1]if dict(c[0]).get('name')==b'arm-io\0']
 existing=[c for c in armio[1]if dict(c[0]).get('name')==b'kebab-display\0']
 displays=[c for c in armio[1]if dict(c[0]).get('device_type',b'').rstrip(b'\0')in(b'display-subsystem',b'ext-display-subsystem')]
 if existing:
  assert len(existing)==1 and existing[0]==NODE
  assert displays==existing,'Another physical display must not be double-counted'
 else:
  assert not displays,'An existing physical display must not be double-counted'
  armio[1].append(copy.deepcopy(NODE))
 result=codec.encode(tree)
 if existing:assert result==adt
 else:
  remove_node(tree,True);assert codec.encode(tree)==adt,'Every prior ADT byte must survive'
 return result

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('destination',type=Path);a=ap.parse_args()
 assert a.source.resolve()!=a.destination.resolve()and not a.destination.exists()
 payload=a.source.read_bytes();fs=Fat32(payload);files={n:d for n,d,_,_ in fs.walk(fs.root)}
 key='EFI/UniXNU/Payloads/kernel.macho.adt';new=change(files[key])
 pack=load('gop_payload_pack',r.parent/'pack-payload.py');out=pack.Fat32(bytearray(payload));pack.replace_file(out,tuple(key.split('/')),new)
 final=Fat32(out.data);actual={n:d for n,d,_,_ in final.walk(final.root)}
 assert actual.keys()==files.keys()and actual[key]==new
 assert all(actual[n]==d for n,d in files.items()if n!=key)
 a.destination.write_bytes(out.data)
 receipt=dict(only_added_node='/arm-io/kebab-display',device_type='display-subsystem',compatible='unixnu,kebab-gop-display',all_prior_adt_bytes_preserved=True,all_other_fat_files_identical=True,no_mmio_irq_or_smmu_resources_added=True,driver_or_gpu_functionality_qualified=False,before_adt_sha256=hashlib.sha256(files[key]).hexdigest(),after_adt_sha256=hashlib.sha256(new).hexdigest())
 a.destination.with_suffix('.gop.json').write_text(json.dumps(receipt,indent=2)+'\n',newline='\n')
 print('Declared one existing GOP screen; exact prior ADT and other payload files preserved')

if __name__=='__main__':main()
