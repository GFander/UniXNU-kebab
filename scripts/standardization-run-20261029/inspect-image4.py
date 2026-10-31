from pathlib import Path
import json
import re
import struct
import sys
sys.path.insert(0,'D:/LOS/HiOS/p1-run/prelink-fix')
from native_prelink import macho,backed_offset,exports,unserialize_xml
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');v=sys.argv[1]
raw=(r/('prelink-'+v+'.bin')).read_bytes();image=macho(raw)
native=(r/('k'+v+'.noctf')).read_bytes();names=exports(native,macho(native))
report=json.loads((r/('prelink-'+v+'.json')).read_text())
donor=(r/'kernelcache.fileset.macho').read_bytes();di=macho(donor)
segment=next(s for s in di['segments'] if s['name']=='__PRELINK_INFO')
offset=backed_offset(di['segments'],segment['va'],segment['filesize'],len(donor))
metadata=unserialize_xml(donor[offset:offset+segment['filesize']])
entry=next(e for e in metadata['_PrelinkInfoDictionary'] if e['CFBundleIdentifier']=='com.apple.security.AppleImage4')
print('PERSONALITIES',entry.get('IOKitPersonalities'))
def read(address,size):
    at=backed_offset(image['segments'],address,size,len(raw));return raw[at:at+size]
for off in range(0xaa8,0xae8,8):
    target,=struct.unpack('<Q',read(0xfffffe0009ee3000+off,8));print('GOT',hex(0xfffffe0009ee3000+off),hex(target),names.get(target))
for address in [0xfffffe000805667b,0xfffffe00080566a0,0xfffffe00080566c5,0xfffffe00080566d5,0xfffffe00080566e1,0xfffffe0008058de9,0xfffffe0008058f58,0xfffffe0008059cea,0xfffffe0008059cf7,0xfffffe0008059cfd,0xfffffe0008059d2b]:
    print('STRING',hex(address),read(address,200).split(b'\0',1)[0])
entry=next(e for e in report['extents'] if e['module']=='com.apple.security.AppleImage4' and e['segment']=='__TEXT_EXEC')
targets={0xfffffe0008931f8c,0xfffffe0008931f98,0xfffffe0008931fa4,0xfffffe00089259ec,0xfffffe0008924c14}
for insn in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(read(entry['va'],entry['size']),entry['va']):
    if insn.mnemonic=='bl' and int(insn.op_str.removeprefix('#'),16) in targets:
        print('GATE CALL',hex(insn.address),insn.op_str)
for target in [0xfffffe00089259ec,0xfffffe0008924c14]:
    at=0
    while True:
        at=raw.find(struct.pack('<Q',target),at)
        if at<0:break
        owner=next(s for s in image['segments'] if s['fileoff']<=at<s['fileoff']+s['filesize'])
        print('FUNCTION POINTER',hex(target),'at',hex(owner['va']+at-owner['fileoff']))
        at+=8
for entry in report['extents']:
    if entry['module']!='com.apple.security.AppleImage4' or entry['segment']!='__TEXT':continue
    data=read(entry['va'],entry['size'])
    for match in re.finditer(rb'[ -~]{5,}\0',data):
        if any(x in match[0].lower() for x in (b'ignit',b'boot-man',b'boot-nonce')):
            print('KEY STRING',hex(entry['va']+match.start()),match[0][:-1].decode())
