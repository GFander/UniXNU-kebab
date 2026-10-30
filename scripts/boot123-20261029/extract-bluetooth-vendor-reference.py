"""Extract only the local full-OTA vendor image for read-only Bluetooth audit.
No device access. New exclusive output only; verify each blob and final image.
Schema: AOSP update_engine/update_metadata.proto, blob6d16da40e53720078b2e4f3689dbb296eb325bee.
"""
from pathlib import Path
import bz2,collections,hashlib,json,lzma,struct,zipfile
r=Path(__file__).resolve().parent
ota=r.parents[1]/'01-rom/lineage-24.0-20261016-nightly-kebab-signed.zip'
dest=r.parent/'bluetooth-reference/lineage-24.0-20261016'
dest.mkdir(parents=True,exist_ok=True)
def fields(s):
 i=0
 def vint():
  nonlocal i
  value=shift=0
  while True:
   assert i<len(s) and shift<64
   b=s[i];i+=1;value|=(b&127)<<shift
   if b<128:return value
   shift+=7
 while i<len(s):
  tag=vint();field,typ=tag>>3,tag&7;assert field>0
  if typ==0:val=vint()
  elif typ==2:
   n=vint();assert n<=len(s)-i;val=s[i:i+n];i+=n
  elif typ in (1,5):
   n=8 if typ==1 else 4;assert n<=len(s)-i;val=s[i:i+n];i+=n
  else:raise ValueError('unsupported protobuf wire type')
  yield field,val
def one(q,k,default=None):
 vals=[v for f,v in q if f==k];assert len(vals)<=1
 return vals[0] if vals else default
with zipfile.ZipFile(ota) as z,z.open('payload.bin') as src:
 magic,major,n,sig=struct.unpack('>4sQQI',src.read(24));assert magic==b'CrAU' and major==2 and n<16<<20 and sig<1<<20
 manifest=src.read(n);assert len(manifest)==n
 top=list(fields(manifest));assert one(top,12,0)==0,'full OTA required'
 block=one(top,3,4096);assert block==4096
 partition=next(list(fields(v)) for k,v in top if k==13 and one(list(fields(v)),1)==b'vendor')
 assert one(partition,6) is None
 info=list(fields(one(partition,7)));size=one(info,1);expected=one(info,2)
 assert isinstance(size,int) and 0<size<8<<30 and len(expected)==32
 operations=[list(fields(v)) for k,v in partition if k==8];assert 0<len(operations)<100000
 output=dest/'vendor.img';ranges=[];types=collections.Counter();base=24+n+sig
 with output.open('xb+') as out:
  out.truncate(size)
  for index,op in enumerate(operations):
   typ=one(op,1);types[typ]+=1;assert typ in (0,1,6,7,8)
   assert not any(k in (4,5,9) for k,v in op),'no source-dependent operation'
   extents=[]
   for k,v in op:
    if k==6:
     e=list(fields(v));off=one(e,1)*block;length=one(e,2)*block
     assert off>=0 and length>0 and off+length<=size;extents.append((off,length));ranges.append((off,off+length))
   assert extents
   need=sum(n for o,n in extents);assert need<=64<<20
   if typ in (6,7):
    assert one(op,3,0)==0;data=bytes(need)
   else:
    off=one(op,2);length=one(op,3);digest=one(op,8)
    assert 0<length<=64<<20 and len(digest)==32 and base+off+length<=z.getinfo('payload.bin').file_size
    src.seek(base+off);blob=src.read(length);assert len(blob)==length and hashlib.sha256(blob).digest()==digest
    data=blob if typ==0 else bz2.decompress(blob) if typ==1 else lzma.decompress(blob)
    assert len(data)==need
   at=0
   for off,length in extents:out.seek(off);assert out.write(data[at:at+length])==length;at+=length
   if (index+1)%64==0:print('vendor operations',index+1,'/',len(operations),flush=True)
  out.flush();out.seek(0);actual=hashlib.file_digest(out,'sha256').digest();assert actual==expected
 ordered=sorted(ranges);assert ordered[0][0]==0 and ordered[-1][1]==size
 assert all(a[1]==b[0] for a,b in zip(ordered,ordered[1:])),'complete non-overlapping coverage required'
with ota.open('rb') as f:ota_sha=hashlib.file_digest(f,'sha256').hexdigest()
q=dict(input_ota=ota.name,input_sha256=ota_sha,partition='vendor',partition_bytes=size,partition_sha256=actual.hex(),
       manifest_sha256=hashlib.sha256(manifest).hexdigest(),operations=len(operations),operation_types=dict(types),
       every_data_blob_verified=True,final_partition_hash_verified=True,no_phone_writes=True,bluetooth_hardware_verified=False)
(dest/'vendor-extraction.json').write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
