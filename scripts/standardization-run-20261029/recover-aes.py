"""Inspect the pinned 20H392 crypto family; no annotation from another OS."""
from pathlib import Path
import json,struct,sys
sys.path.insert(0,'D:/LOS/HiOS/p1-run/prelink-fix')
from native_prelink import macho,backed_offset,exports
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');v=sys.argv[1]
b=(r/('prelink-'+v+'.bin')).read_bytes();m=macho(b)
p=json.loads((r/('prelink-'+v+'.json')).read_text())
e=[x for x in p['extents'] if x['module']=='com.apple.iokit.IOCryptoAcceleratorFamily']
def read(va,n):
    at=backed_offset(m['segments'],va,n,len(b));return b[at:at+n]
cs=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
for x in e:print('EXTENT',x)
text=next(x for x in e if x['segment']=='__TEXT_EXEC')
const=next(x for x in e if x['segment']=='__DATA_CONST')
data=next(x for x in e if x['segment']=='__TEXT')
for word in (b'IOAESAccelerator\0',b'IOCryptoAccelerator\0',b'AES-CBC\0'):
    at=read(data['va'],data['size']).find(word)
    print('STRING',word,hex(data['va']+at))
table=[]
raw=read(const['va'],const['size'])
for at in range(0,len(raw)-16,8):
    if raw[at:at+16]!=bytes(16):continue
    n=0
    while at+16+(n+1)*8<=len(raw):
        target=struct.unpack_from('<Q',raw,at+16+n*8)[0]
        if not 0xfffffe0006000000<=target<0xfffffe0009400000:break
        n+=1
    if n>=160:
        table.append((const['va']+at,n))
        print('VTABLE',hex(const['va']+at),n)
        for slot in [0,1,7,86,111,117,*range(168,n)]:
            target=struct.unpack_from('<Q',raw,at+16+slot*8)[0]
            print('SLOT',slot,hex(target))
for ins in cs.disasm(read(text['va'],text['size']),text['va']):
    if ins.mnemonic=='mov' and ins.op_str.startswith('w3, #'):
        start=max(text['va'],ins.address-32);length=min(92,text['va']+text['size']-start)
        print('METACLASS INIT?',hex(ins.address))
        for row in cs.disasm(read(start,length),start):print(hex(row.address),row.mnemonic,row.op_str)
