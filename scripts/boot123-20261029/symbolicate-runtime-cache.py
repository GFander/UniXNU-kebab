from pathlib import Path
import mmap,struct,json,sys,re,bisect
r=Path(__file__).resolve().parent;root=r.parent/'ios-userland/cryptex-cache';v=sys.argv[1]
from cache_readonly import CacheReader
reader=CacheReader(root)
main=(root/'dyld_shared_cache_arm64').read_bytes();region=struct.unpack_from('<Q',main,224)[0]
offset,count=struct.unpack_from('<QQ',main,136);images=[]
for i in range(count):
 uid,base,size,path=struct.unpack_from('<16sQII',main,offset+i*32);end=main.index(0,path);images.append((base,size,main[path:end].decode()))
symbolsFile=root/'dyld_shared_cache_arm64.symbols'
with symbolsFile.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as symbols:
 assert symbols[88:104]==main[400:416]
 lo=struct.unpack_from('<Q',symbols,72)[0];no,nc,so,ss,eo,ec=struct.unpack_from('<6I',symbols,lo)
 entries={struct.unpack_from('<Q',symbols,lo+eo+i*16)[0]:struct.unpack_from('<II',symbols,lo+eo+i*16+8) for i in range(ec)}
 cached={}
 def lookup(raw_pc,return_address=False):
  pc=raw_pc-4 if return_address and raw_pc>=4 else raw_pc
  image=next((im for im in images if im[0]<=pc<im[0]+im[1]),None);out=dict(pc=hex(raw_pc),lookup_pc=hex(pc),return_address_adjusted=bool(return_address and raw_pc>=4),image=image[2] if image else None)
  if not image:return out
  base=image[0];out['image_offset']=hex(pc-base)
  if base not in cached:
   rows=[];start,n=entries.get(base-region,(0,0));assert start+n<=nc
   for i in range(start,start+n):
    string,kind,section,_,value=struct.unpack_from('<IBBHQ',symbols,lo+no+i*16)
    if not kind&0xe0 and kind&0xe==0xe and section and base<=value<base+image[1] and string<ss:
     pos=lo+so+string;rows.append((value,symbols[pos:symbols.find(b'\0',pos)].decode(errors='replace'),'local'))
   rows.extend((value,name,'export') for value,name in reader.exports(base) if base<=value<base+image[1])
   cached[base]=sorted(rows)
  rows=cached[base];index=bisect.bisect_right(rows,(pc,'\uffff','\uffff'))-1
  if index>=0:out.update(symbol=rows[index][1],symbol_source=rows[index][2],offset=hex(pc-rows[index][0]),nearest_symbol_only=pc!=rows[index][0])
  return out
 text=(r/(v+'-media-boot.log')).read_text();assert 'kebab_dsc_slide=0x0' in text
 pcs=re.findall(r'KEBAB RUNTIME PC: name=(\S+) pid=(\d+) depth=(\d+) pc=(0x[0-9a-f]+)',text)
 out=[dict(name=n,pid=int(pid),depth=int(depth),**lookup(int(pc,16),int(depth)>0)) for n,pid,depth,pc in pcs]
 if not pcs:out=[lookup(int(pc,16)) for pc in sys.argv[2:]]
 (r/(v+'-runtime-symbolicated.json')).write_text(json.dumps(out,indent=2)+'\n')
 for row in out:print(row)
