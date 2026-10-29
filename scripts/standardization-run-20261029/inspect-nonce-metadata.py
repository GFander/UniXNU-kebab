"""Read donor Image4 nonce-domain metadata, without changing code or dumping keys."""
from pathlib import Path
import hashlib,json,struct,sys
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');sys.path.insert(0,str(r))
from native_prelink import macho,backed_offset
v=sys.argv[1];raw=(r/('prelink-'+v+'.bin')).read_bytes();image=macho(raw)
report=json.loads((r/('prelink-'+v+'.json')).read_text())
def read(va,size):
    at=backed_offset(image['segments'],va,size,len(raw));return raw[at:at+size]
def cstr(va):
    try:
        b=read(va,128).split(b'\0',1)[0]
        return b.decode('ascii') if b and all(32<=x<127 for x in b) else None
    except (ValueError,AssertionError,StopIteration,UnicodeDecodeError):return None
found=[]
for e in report['extents']:
    if e['module']!='com.apple.security.AppleImage4' or e['segment']=='__TEXT_EXEC':continue
    data=read(e['va'],e['size'])
    for offset in range(0,len(data)-4,4):
        if struct.unpack_from('<I',data,offset)[0]!=2212:continue
        address=e['va']+offset;base=address-0x38
        fields=struct.unpack('<8Q',read(base,64))
        assert cstr(fields[0]) and cstr(fields[1]).startswith('com.apple.private.img4.nonce.')
        pointers={hex(base+i*8):{'pointer':hex(n),'ascii':cstr(n)} for i,n in enumerate(fields[:2])}
        found.append({'selector_address':hex(address),'candidate_domain_address':hex(base),'string_fields':pointers})
out={'version':v,'prelink_sha256':hashlib.sha256(raw).hexdigest(),
 'scope':'Offline donor constants only; selector offset 0x38 proven by original helper at 0xfffffe0008927424.',
 'matches':found}
dest=Path(__file__).parent/('nonce-metadata-'+v+'.json');dest.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
