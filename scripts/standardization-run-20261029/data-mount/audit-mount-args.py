"""Pin the target's own mount_apfs and the consumed mount-argument fields."""
from pathlib import Path
import hashlib,json,struct,sys
r=Path(__file__).parent
sys.path.insert(0,'D:/LOS/HiOS/p1-run/prelink-fix')
from native_prelink import macho,backed_offset
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
b=(r/'mount_apfs-20H392').read_bytes()
assert len(b)==72960 and hashlib.sha256(b).hexdigest()=='ccac6fa5b10c74ac9458212044bdc4115831c7a01be04d61d04e86bdec52dc27'
m=macho(b);cs=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
def instructions(va,n):
    at=backed_offset(m['segments'],va,n,len(b))
    return [(x.mnemonic,x.op_str) for x in cs.disasm(b[at:at+n],va)]
proof={hex(a):instructions(a,n) for a,n in [(0x100001a80,8),(0x100001bd4,12),(0x100001f3c,4),(0x1000023e0,16),(0x1000025c4,28)]}
assert proof['0x100001a80'][-1]==('add','x26, x26, #0x700')
assert proof['0x100001bd4'][0]==('add','x8, x26, #0x144')
assert proof['0x100001bd4'][-1]==('str','d0, [x8]')
assert proof['0x100001f3c'][0]==('strh','w8, [sp, #0x1710]')
assert proof['0x1000023e0'][-1]==('strh','w8, [sp, #0x1710]')
assert proof['0x1000025c4'][-1]==('bl','#0x100003654')
record={'os':'iPhone10,3 16.7.16 / 20H392','file_sha256':hashlib.sha256(b).hexdigest(),
 'mount_payload_stack_base':'sp+0x1700','device_path_pointer_offset':0,'normal_mode_offset':16,'normal_mode':1,
 'anonymous_uid_offset':0x144,'anonymous_gid_offset':0x148,'anonymous_default':0xffffffff,
 'provided_bytes':0x150,'system_readonly':True,'first_test_data_readonly':True,
 'instructions':proof,'limits':['Field and call-site evidence; actual target mount is qualified separately.']}
(r/'mount-args-20H392.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS: original 20H392 mount_apfs provenance and argument field/call-site evidence')
