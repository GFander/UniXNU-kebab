"""Read-only original dyld cache mappings and Mach-O export tries."""
from pathlib import Path
import struct
class CacheReader:
 def __init__(self,root):
  self.root=Path(root);self.main=(self.root/'dyld_shared_cache_arm64').read_bytes()
  assert self.main[88:104].hex()=='8f970452c9e33e88b114feb1864bc9e0'
  off,count=struct.unpack_from('<QQ',self.main,136);self.images=[];self.maps=[];self._exports={}
  for i in range(count):
   uid,base,size,path=struct.unpack_from('<16sQII',self.main,off+i*32)
   self.images.append((base,size,self.main[path:self.main.index(0,path)].decode()))
  for path in self.root.glob('dyld_shared_cache_arm64*'):
   if path.suffix=='.symbols':continue
   with path.open('rb') as f:
    h=f.read(24)
    if not h.startswith(b'dyld_v'):continue
    mo,mc=struct.unpack_from('<II',h,16);assert mc<64
    f.seek(mo);table=f.read(mc*32)
    for i in range(mc):
     addr,size,foff,maxp,initp=struct.unpack_from('<QQQII',table,i*32);self.maps.append((addr,size,foff,path))
 def read(self,va,n):
  entries=[(off+va-a,p) for a,size,off,p in self.maps if a<=va and va+n<=a+size]
  assert len(entries)==1,(hex(va),n)
  off,path=entries[0]
  with path.open('rb') as f:f.seek(off);data=f.read(n)
  assert len(data)==n;return data
 def image(self,va):return next((im for im in self.images if im[0]<=va<im[0]+im[1]),None)
 def exports(self,base):
  if base in self._exports:return self._exports[base]
  header=self.read(base,32);magic,cpu,subtype,kind,ncmds,sizeofcmds=struct.unpack_from('<6I',header)
  assert magic==0xfeedfacf and cpu==0x100000c and sizeofcmds<65536
  data=self.read(base+32,sizeofcmds);pos=0;link=None;export=None
  for _ in range(ncmds):
   cmd,size=struct.unpack_from('<II',data,pos);assert size>=8 and pos+size<=len(data)
   if cmd==0x19 and data[pos+8:pos+24].rstrip(b'\0')==b'__LINKEDIT':
    va,extent,foff,flen=struct.unpack_from('<4Q',data,pos+24);link=(va,extent,foff,flen)
   if cmd==0x80000033:export=struct.unpack_from('<II',data,pos+8)
   if cmd in (0x22,0x80000022) and export is None:export=struct.unpack_from('<II',data,pos+40)
   pos+=size
  assert pos==sizeofcmds
  if not export or not export[1]:self._exports[base]=[];return []
  assert link;off,size=export;va,extent,foff,flen=link;assert foff<=off and off+size<=foff+flen
  trie=self.read(va+off-foff,size)
  def uleb(at):
   value=shift=0
   while True:
    assert at<len(trie) and shift<64;b=trie[at];at+=1;value|=(b&127)<<shift
    if not b&128:return value,at
    shift+=7
  rows=[];stack=[(0,'')];nodes=0
  while stack:
   at,prefix=stack.pop();nodes+=1;assert nodes<1000000 and len(prefix)<4096
   term,cur=uleb(at);end=cur+term;assert end<len(trie)
   if term:
    flags,valueat=uleb(cur)
    if not flags&8:
     value,_=uleb(valueat);rows.append((value if flags&3==2 else base+value,prefix))
   count=trie[end];cur=end+1
   for _ in range(count):
    nameend=trie.index(0,cur);edge=trie[cur:nameend].decode();child,cur=uleb(nameend+1)
    assert child<len(trie);stack.append((child,prefix+edge))
  self._exports[base]=sorted(rows);return self._exports[base]
