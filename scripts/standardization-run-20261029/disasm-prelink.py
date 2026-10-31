from pathlib import Path
import sys
sys.path.insert(0,'D:/LOS/HiOS/p1-run/prelink-fix')
from native_prelink import macho,backed_offset
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');version,address,size=sys.argv[1:4]
address=int(address,0);size=int(size,0);assert 0<size<=0x4000
raw=(r/('prelink-'+version+'.bin')).read_bytes();image=macho(raw)
at=backed_offset(image['segments'],address,size,len(raw))
for instruction in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(raw[at:at+size],address):
    print(f'{instruction.address:016x}: {instruction.mnemonic:8s} {instruction.op_str}')
