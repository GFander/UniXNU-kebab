"""Qualify compiled SoftwareAES dispatch against unchanged 20H392 crypto code."""
from pathlib import Path
import hashlib,json,struct,sys
r=Path(__file__).parent;d=r/'software-aes';images=r.parent/'p1-run/prelink-fix'
sys.path.insert(0,str(images))
from native_prelink import macho,backed_offset,exports
v=sys.argv[1];b=(images/('prelink-'+v+'.bin')).read_bytes();m=macho(b)
p=json.loads((images/('prelink-'+v+'.json')).read_text());syms=exports(b,m)
# Inline overrides are local N_SECT symbols after ld; do not mistake the
# export-only map for a full symbol table or weaken target identity checks.
native=(images/('k'+v+'.noctf')).read_bytes();nm=macho(native)
command=next(at for cmd,at,size in nm['commands'] if cmd==2)
so,n,strings,_=struct.unpack_from('<4I',native,command+8)
for i in range(n):
    ix,kind,sec,desc,va=struct.unpack_from('<IBBHQ',native,so+16*i)
    if kind&0xe0 or kind&0xe!=0xe:continue
    name=native[strings+ix:native.index(b'\0',strings+ix)].decode()
    if name not in syms.setdefault(va,[]):syms[va].append(name)
assert p['original_sha256']=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
assert p['candidate_sha256']==hashlib.sha256(b).hexdigest()
def read(va,n):
    at=backed_offset(m['segments'],va,n,len(b));return b[at:at+n]
extent=next(x for x in p['extents'] if x['module']=='com.apple.iokit.IOCryptoAcceleratorFamily' and x['segment']=='__TEXT_EXEC')
assert hashlib.sha256(read(extent['va'],extent['size'])).hexdigest()==p['original_executable_sha256'][extent['module']]
object_path=d/('NativeAES-'+v+'.o')
if not object_path.exists():object_path=d/'NativeAES.o'
obj=object_path.read_bytes();om=macho(obj)
off=next(at for cmd,at,size in om['commands'] if cmd==2)
symoff,count,stroff,_=struct.unpack_from('<4I',obj,off+8)
names=[]
for i in range(count):
    strx,kind,sec,desc,value=struct.unpack_from('<IBBHQ',obj,symoff+16*i)
    name=obj[stroff+strx:obj.index(b'\0',stroff+strx)].decode();names.append((name,value,kind))
own=next(va for name,va,kind in names if name=='__ZTV9NativeAES')
relocations={}
for seg in om['segments']:
    for sec in seg['sections']:
        if sec['va']<=own<sec['va']+sec['size']:
            reloc,n=struct.unpack_from('<II',obj,sec['command_offset']+56)
            for i in range(n):
                at,word=struct.unpack_from('<II',obj,reloc+8*i)
                if word>>27&1 and word>>28==0 and word>>25&3==3:
                    relocations[sec['va']+at]=names[word&0xffffff][0]
bindings={k:int(x,16) for k,x in json.loads((d/'bindings-20H392.json').read_text()).items()}
overrides={0,1,7,86,111,117,168,169,170,171,172}
rows=[];inherited=0
for slot in range(181):
    name=relocations[own+16+slot*8];target=struct.unpack('<Q',read(0xfffffe0009ee09f8+16+slot*8,8))[0]
    if slot in overrides:
        assert '9NativeAES' in name,(slot,name)
        original=name.replace('9NativeAES','16IOAESAccelerator')
        assert bindings[original]==target,(slot,original)
    elif '16IOAESAccelerator' in name:
        assert bindings[name]==target,(slot,name)
    else:
        assert name in syms.get(target,[]),(slot,name,hex(target),syms.get(target))
        inherited+=1
    rows.append(dict(slot=slot,compiled_symbol=name,target=hex(target),override=slot in overrides))
assert own+16+181*8 not in relocations
assert inherited==162
linked=next((va for va,n in syms.items() if '__ZTV9NativeAES' in n),None)
if linked is not None:
    for row in rows:
        target=struct.unpack('<Q',read(linked+16+row['slot']*8,8))[0]
        if row['override']:
            assert row['compiled_symbol'] in syms.get(target,[]),(row['slot'],'linked override mismatch',hex(target),syms.get(target,[]),row['compiled_symbol'])
        else:
            assert target==int(row['target'],16),(row['slot'],'linked inherited target mismatch')
        row['linked_target']=hex(target)
def word(va):return struct.unpack('<I',read(va,4))[0]
def branch(va):
    x=word(va);assert x&0xfc000000 in (0x14000000,0x94000000)
    imm=x&0x3ffffff
    if imm&0x2000000:imm-=0x4000000
    return va+imm*4
assert branch(0xfffffe0008911078)==branch(0xfffffe000891107c)==0xfffffe0008913580
assert branch(0xfffffe0008911040)==branch(0xfffffe0008911060)==0xfffffe0008913540
# Original class registrations, key-selector reads, and public dispatch call.
assert word(0xfffffe0008911018)==0x52802403 # mov w3, 0x120
assert word(0xfffffe0008913744)==0x52801103 # mov w3, 0x88
assert word(0xfffffe0008911a44)==0xb94024e8 # ldr w8, [x7, 0x24]
assert word(0xfffffe0008911c00)==0xf942a94a # ldr x10, [x10, 0x550] -> slot170
assert read(0xfffffe000804cfc4,8)==b'AES-CBC\0'
out=dict(target='20H392 / XNU8796.141.3 ARM64 16KiB',upstream_revision='4a45326da5c2cd6fe6813d79ee43db20805628af',
 original_sha256=p['original_sha256'],candidate_sha256=p['candidate_sha256'],slide=p['slide'],
 crypto_text_sha256=p['original_executable_sha256'][extent['module']],original_executable_unchanged=True,
 original_vtable='0xfffffe0009ee09f8',slots=181,inherited_kernel_matches=inherited,methods=rows,
 original_instance_sizes={'IOAESAccelerator':0x120,'IOCryptoAccelerator':0x88},
 metaclasses={'IOAESAccelerator':'0xfffffe000ac49050','IOCryptoAccelerator':'0xfffffe000ac490d8'},
 bindings={k:hex(x) for k,x in bindings.items()},
 structures={'IV':16,'raw_key_bits_offset':0,'raw_key_bytes_offset':4,'raw_key_selector_offset':36,'raw_key_size':40},
 compiler_object_sha256=hashlib.sha256(obj).hexdigest(),
 linked_vtable=hex(linked) if linked is not None else None,
 limitations=['Static qualification; live metadata, KAT and accepted requests are recorded separately.',
 'Only software raw-key CBC is supported. Apple hardware-key selectors remain unsupported.'])
(d/('IOAESAccelerator-'+v+'.json')).write_text(json.dumps(out,indent=2)+'\n')
print('PASS: 181 compiled slots / 162 inherited matches; target constructors, destructors, class size and request evidence')
