"""Check all final native subclass slots against exact compiler relocations/donor."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';sys.path.insert(0,str(b))
from native_prelink import macho,exports,backed_offset
v=sys.argv[1];assert len(v)==4 and v.isdigit()
obj=(r/(v+'-KebabHIDInput.o')).read_bytes();om=macho(obj)
cmd,=[at for c,at,_ in om['commands']if c==2]
so,n,st,sz=struct.unpack_from('<4I',obj,cmd+8);symbols=[]
for i in range(n):
 ix,kind,sect,desc,value=struct.unpack_from('<IBBHQ',obj,so+16*i)
 end=obj.index(0,st+ix);symbols.append(dict(name=obj[st+ix:end].decode(),kind=kind,section=sect,value=value))
vt,=[s for s in symbols if s['name']=='__ZTV14KebabHIDDevice']
sections=[s for seg in om['segments']for s in seg['sections']];section=sections[vt['section']-1]
reloc,nreloc=struct.unpack_from('<II',obj,section['command_offset']+56);refs={}
for i in range(nreloc):
 at,info=struct.unpack_from('<II',obj,reloc+8*i)
 relative=at-(vt['value']+16-section['va'])
 if 0<=relative<207*8:
  assert not relative%8 and (info>>27)&1 and info>>28==0 and (info>>25)&3==3
  refs[relative//8]=symbols[info&0xffffff]['name']
assert set(refs)==set(range(207))
original=json.loads((r/'original-hid-device-abi.json').read_text());assert original['all_compiled_virtual_slots_checked']==207
originalrows={row['slot']:row for row in original['rows']};assert set(originalrows)==set(range(207))
unstripped=(b/('kernel-'+v+'.kebab')).read_bytes()
subprocess.run([sys.executable,str(b.parent/'strip_ctf.py'),str(b/('kernel-'+v+'.kebab')),str(b/('k'+v+'.noctf'))],check=True)
native=(b/('k'+v+'.noctf')).read_bytes();nm=macho(native)
names={name:address for address,ns in exports(native,nm).items()for name in ns}
sym=next(at for cmd,at,_ in nm['commands']if cmd==2)
at,count,strings,size=struct.unpack_from('<4I',native,sym+8)
for i in range(count):
 ix,kind,_,_,va=struct.unpack_from('<IBBHQ',native,at+i*16)
 if not(kind&0xe0)and kind&0xe:
  name=native[strings+ix:native.index(b'\0',strings+ix)].decode();names[name]=va
table=names['__ZTV14KebabHIDDevice'];offset=backed_offset(nm['segments'],table+16,207*8,len(native))
pointers=struct.unpack_from('<207Q',native,offset);rows=[];slide=-0x1effcdc8000
own={0,1,7,14,86,87,171,172,173,180,182,198}
own.add(next(row['slot']for row in original['rows']if row['symbol']=='__ZNK9IOService11getWorkLoopEv'))
for index,symbol in refs.items():
 originalrow=originalrows[index]
 if '14KebabHIDDevice' in symbol:
  assert index in own,(index,symbol)
 else:
  assert index not in own,(index,symbol)
  assert symbol==originalrow['symbol'],(index,symbol,originalrow)
 if '11IOHIDDevice' in symbol:
  expected=int(original['bindings'][symbol],16)+slide
  assert names[symbol]==expected,(symbol,hex(names[symbol]),hex(expected))
 else:expected=names[symbol]
 assert pointers[index]==expected,(index,symbol,hex(pointers[index]),hex(expected))
 rows.append(dict(slot=index,symbol=symbol,target=hex(expected)))
assert set(i for i,s in refs.items()if '14KebabHIDDevice'in s)==own
q=dict(version=v,native_sha256=hashlib.sha256(unstripped).hexdigest(),object_sha256=hashlib.sha256(obj).hexdigest(),all_final_virtual_slots_checked=207,original_base_bytes=208,rows=rows,physical_input_or_event_consumption_qualified=False)
(r/('native-hid-abi-'+v+'.json')).write_text(json.dumps(q,indent=2)+'\n',newline='\n')
print('PASS final native subclass207slots,13explicit overrides, inherited donor bindings and common native symbols')
