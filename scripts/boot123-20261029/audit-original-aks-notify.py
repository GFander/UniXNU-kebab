"""Read-only original producer/consumer ABI evidence for AKS102/103."""
from pathlib import Path
import hashlib,json,struct,sys
import capstone
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
sys.path.insert(0,str(b));from native_prelink import macho,backed_offset
from cache_readonly import CacheReader
raw=(b/'kernelcache.fileset.macho').read_bytes();digest=hashlib.sha256(raw).hexdigest()
assert digest=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
m=macho(raw)
def read(va,n):
 o=backed_offset(m['segments'],va,n,len(raw));return raw[o:o+n]
c=capstone.Cs(capstone.CS_ARCH_ARM64,capstone.CS_MODE_LITTLE_ENDIAN)
def instructions(data,va):return [(i.mnemonic,i.op_str) for i in c.disasm(data,va)]
expected={102:[('mov','w19, #0'),('ldr','x8, [x27]'),('mov','w9, #1'),('str','w9, [x8, #0xa4]'),('b','#0xfffffff0060d9984')],
103:[('ldr','w8, [x21, #0x28]'),('cbnz','w8, #0xfffffff0060d9984'),('ldr','w8, [x21, #0x50]'),('cmp','w8, #1'),('b.ne','#0xfffffff0060d9984'),('mov','w19, #0'),('ldr','x8, [x27]'),('ldr','w8, [x8, #0xa4]'),('b','#0xfffffff0060d9f50')]}
rows=[]
for sel in (102,103):
 delta=struct.unpack('<H',read(0xfffffff0050e93b4+(sel-1)*2,2))[0]
 va=0xfffffff0060d6408+delta*4;code=read(va,len(expected[sel])*4)
 assert instructions(code,va)==expected[sel]
 rows.append(dict(selector=sel,entry=hex(va),instructions_checked=len(expected[sel]),code_sha256=hashlib.sha256(code).hexdigest()))
cache=CacheReader(r.parent/'ios-userland/cryptex-cache')
wrappers=[]
for sel,va,length in ((102,0x1ae401b0c,0x50),(103,0x1ae401b58,0xc0)):
 code=cache.read(va,length);ins=instructions(code,va)
 assert ('mov',f'w1, #0x{sel:x}') in ins
 wrappers.append(dict(selector=sel,va=hex(va),code_sha256=hashlib.sha256(code).hexdigest()))
q=dict(qualified=True,original_kernelcache_sha256=digest,original_cache_uuid=cache.main[88:104].hex(),
 native_contract='provider-owned notification-registration flag; not key availability or a fabricated device-state reply',
 provider_flag_offset=0xa4,semantics=dict(set_selector=102,read_selector=103,read_scalar_input_count=0,read_scalar_output_count=1,initial_state=0,set_state=1),
 methods=rows,original_wrappers=wrappers,hardware_key_operation_claim=False,full_aks_compatibility=False)
(r/'original-aks-notify-abi-qualified.json').write_text(json.dumps(q,indent=2)+'\n')
print('Original selectors102/103 producer/consumer and actual provider flag verified.')
