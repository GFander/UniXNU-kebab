from pathlib import Path
import json,re,sys
sys.path.insert(0,'D:/LOS/HiOS/p1-run/prelink-fix')
from native_prelink import macho,backed_offset
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');v=sys.argv[1]
b=(r/('prelink-'+v+'.bin')).read_bytes();m=macho(b)
p=json.loads((r/('prelink-'+v+'.json')).read_text())
for e in p['extents']:
    if e['module']!='com.apple.security.AppleImage4' or e['segment']!='__TEXT':continue
    at=backed_offset(m['segments'],e['va'],e['size'],len(b));data=b[at:at+e['size']]
    for x in re.finditer(rb'[ -~]{4,}\0',data):
        if any(k in x[0].lower() for k in [b'nonce',b'key',b'boot',b'dev',b'policy',b'csr',b'secur',b'osenviron']):
            print(hex(e['va']+x.start()),x[0][:-1].decode())
