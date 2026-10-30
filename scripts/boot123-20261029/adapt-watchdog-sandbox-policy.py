"""Explicit owner policy: exact platform-class predicate mapping, not Apple auth.

Only 21 readonly policy-data bytes change. All original executable bytes and
the serialized policy shape, operations, privileges and other class predicates
remain exact. The genuine native class uses the original IOWatchdog/client.
"""
from pathlib import Path
import sys,json,hashlib
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';sys.path.insert(0,str(b))
from native_prelink import macho,backed_offset
ORIGINAL='AppleARMWatchdogTimer';NATIVE='KebabPlatformWatchdog'
DONOR='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
TEXT='ded8921e103c6597598c3d6b9d505748e096fb77936625d84f5dddde8ead47ca'
WINDOW='972fa9ad1ce4dec34b1b0177965dbd3b1c4af310b1447916df878d980b74c895'
VA=0xfffffff00536fa3b
sha=lambda x:hashlib.sha256(x).hexdigest()
def adapt(image,report):
 assert len(ORIGINAL)==len(NATIVE)==21
 assert report['original_sha256']==DONOR and report['candidate_sha256']==sha(image)
 assert not report.get('owner_sandbox_policy')
 assert report['original_executable_sha256']['com.apple.security.sandbox']==TEXT
 segments=macho(image)['segments'];extents=report['extents'];slide=report['slide']
 text=next(x for x in extents if x['module']=='com.apple.security.sandbox' and x['segment']=='__TEXT')
 address=VA+slide;assert text['va']<=address-128<address+256<=text['va']+text['size']
 at=backed_offset(segments,address,21,len(image));off=backed_offset(segments,text['va'],text['size'],len(image))
 assert image[off:off+text['size']].count(ORIGINAL.encode())==1
 assert image[at:at+21]==ORIGINAL.encode() and sha(image[at-128:at+256])==WINDOW
 container=next(s for s in segments if s['va']<=address<s['va']+s['filesize'])
 assert container['initprot']==1 and container['maxprot']==1
 for e in extents:
  if e['segment']=='__TEXT_EXEC':
   pos=backed_offset(segments,e['va'],e['size'],len(image))
   assert sha(image[pos:pos+e['size']])==report['original_executable_sha256'][e['module']]
 result=bytearray(image);result[at:at+21]=NATIVE.encode();result=bytes(result)
 assert len(result)==len(image) and result[:at]==image[:at] and result[at+21:]==image[at+21:]
 q=dict(report);q.update(candidate_sha256=sha(result),owner_sandbox_policy=dict(
  kind='exact owner platform watchdog class mapping',apple_authenticated_policy=False,
  source_candidate_sha256=sha(image),original_class=ORIGINAL,native_class=NATIVE,
  original_va=hex(VA),candidate_va=hex(address),bytes=21,file_offset=at,
  source_context_sha256=WINDOW,changed_readonly_policy_data_only=True,
  original_executable_bytes_unchanged=True,policy_operations_or_other_predicates_changed=False,
  original_macf_return_overridden=False,process_name_authorization=False,runtime_qualified=False))
 return result,q
if __name__=='__main__':
 src,dest=map(Path,sys.argv[1:]);assert not dest.exists() and not dest.with_suffix('.json').exists()
 image,report=adapt(src.read_bytes(),json.loads(src.with_suffix('.json').read_text()))
 dest.write_bytes(image);dest.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Owner platform-class policy adapted: 21 RO data bytes; all original code and other bytes unchanged; runtime unqualified')
