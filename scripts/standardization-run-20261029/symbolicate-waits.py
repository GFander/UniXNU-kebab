from pathlib import Path
from bisect import bisect_right
import json
import re
import struct
import sys

r=Path('D:/LOS/HiOS/p1-run/prelink-fix');sys.path.insert(0,str(r))
from native_prelink import macho,backed_offset,unserialize_xml
version=sys.argv[1];log=Path(sys.argv[2]);report=json.loads((r/('prelink-'+version+'.json')).read_text())
def symbols(data,image):
    command,=[off for kind,off,length in image['commands'] if kind==2]
    symoff,count,stroff,strsize=struct.unpack_from('<4I',data,command+8)
    strings=data[stroff:stroff+strsize];result={}
    for i in range(count):
        ix,kind,section,desc,va=struct.unpack_from('<IBBHQ',data,symoff+i*16)
        if kind&0xe0 or kind&0x0e!=0x0e or not ix:continue
        end=strings.find(b'\0',ix)
        if end<0:continue
        name=strings[ix:end].decode(errors='replace')
        result.setdefault(va,[]).append(name)
    return result
native=(r/('k'+version+'.noctf')).read_bytes();ni=macho(native);ns=symbols(native,ni)
donor=(r/'kernelcache.fileset.macho').read_bytes();di=macho(donor);ds=symbols(donor,di)
segment=next(s for s in di['segments'] if s['name']=='__PRELINK_INFO')
at=backed_offset(di['segments'],segment['va'],segment['filesize'],len(donor))
metadata=unserialize_xml(donor[at:at+segment['filesize']])
for entry in metadata['_PrelinkInfoDictionary']:
    if entry.get('OSKernelResource') is True:continue
    address=entry.get('_PrelinkExecutableLoadAddr')
    if not address or not entry.get('_PrelinkExecutableSize'):continue
    at=backed_offset(di['segments'],address,32,len(donor));module=macho(donor,at)
    for address,names in symbols(donor,module).items():ds.setdefault(address,[]).extend(names)
addresses=[int(x,16) for x in re.findall(r'kframe\[\d+\]=(0x[0-9a-f]+)',log.read_text())]
results=[]
for address in addresses:
    pc=address-4
    extent=next((e for e in report['extents'] if e['va']<=pc<e['va']+e['size']),None)
    if extent:
        owner=extent['module'];target=pc-report['slide'];table=ds
        low=extent['va']-report['slide'];high=low+extent['size']
    else:
        owner='native';target=pc;table=ns
        segment=next(s for s in ni['segments'] if s['va']<=target<s['va']+s['size'])
        low=segment['va'];high=low+segment['size']
    starts=sorted(v for v in table if low<=v<high);i=bisect_right(starts,target)-1
    nearest=starts[i] if i>=0 else None
    row=dict(return_address=hex(address),callsite=hex(pc),owner=owner,
        nearest_symbol=table.get(nearest,[]),offset=hex(target-nearest) if nearest is not None else None)
    results.append(row);print(hex(address),owner,','.join(row['nearest_symbol']),row['offset'])
log.with_suffix('.symbols.json').write_text(json.dumps(dict(version=version,frames=results),indent=2)+'\n')
