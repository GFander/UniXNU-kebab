"""Prepare a private, measured fixed-file plan; never opens a device.
The only Setup mutation is three genuine20H392 PurpleBuddy keys. DataArk
checkpoint payloads remain byte-identical. No activation record is generated.
"""
from pathlib import Path
import argparse,hashlib,json,plistlib,re,struct
ENTRY=struct.Struct('<4I2Q32s32s32s6I8qQ2I16s16sQ2q')
HEADER=struct.Struct('<4I')
assert ENTRY.size==288
MAGIC=0x3150534f
KEYS={'SetupDone':True,'SetupFinishedAllSteps':True,'SetupVersion':11}
def prepare(backup,mode='apply',current=None):
 assert mode in ('apply','rollback')
 backup=Path(backup);proof=json.loads(backup.read_text());assert proof['accepted']and proof['build']=='20H392'and proof['log_drop_delta']==0
 files=proof['files'];raw={}
 for index in (0,8,13,14):
  f=files[str(index)];data=Path(f['file']).read_bytes();assert hashlib.sha256(data).hexdigest()==f['sha256']and len(data)==f['size']<=65536
  assert f['protection']==4 and f['flags']==0 and f['nlink']==1 and f['xattr_names_bytes']==0 and f['metadata_flags']&~7==0
  assert f['uid']==(0 if index==13 else 501);assert 0<=f['backup_time'][1]<1000000000;raw[index]=data
 path=files['14']['canonical_path'];assert re.fullmatch(r'/private/var/containers/Data/System/[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}/Library/internal/data_ark\.plist',path)
 assert plistlib.loads(raw[14])['-BuildVersion']=='20H392'
 assert plistlib.loads(raw[13])['-DarkProductVersion']=='16.7.16'
 old=plistlib.loads(raw[8]);assert isinstance(old,dict)
 for key in ('SetupDone','SetupFinishedAllSteps'):assert key not in old or type(old[key])is bool
 assert 'SetupVersion'not in old or type(old['SetupVersion'])is int and old['SetupVersion']>=0
 merged={**old,'SetupDone':True,'SetupFinishedAllSteps':True,'SetupVersion':max(old.get('SetupVersion',0),11)}
 assert {k:v for k,v in merged.items()if k not in KEYS}=={k:v for k,v in old.items()if k not in KEYS}
 payloads={13:raw[13],14:raw[14],0:b'',8:plistlib.dumps(merged,fmt=plistlib.FMT_BINARY,sort_keys=False)}
 current_proof=json.loads(Path(current).read_text())if current else None
 if mode=='rollback':assert current_proof and current_proof['accepted']
 packet=bytearray(HEADER.pack(MAGIC,1,1 if mode=='apply'else 2,4));records=[]
 for index in (13,14,0,8):
  f=files[str(index)];payload=payloads[index]if mode=='apply'else b'';current_id=0
  if mode=='rollback':
   now=current_proof['files'][str(index)];current_id=now['fileid'];new_size=now['size'];new_hash=bytes.fromhex(now['sha256'])
  else:new_size=len(payload);new_hash=hashlib.sha256(payload).digest()if payload else bytes(32)
  values=(index,new_size,f['acl_bytes'],0,f['fileid'],f['size'],bytes.fromhex(f['sha256']),new_hash,bytes.fromhex(f['acl_sha256']),f['uid'],f['gid'],f['mode'],f['flags'],f['protection'],0,*f['times'],current_id,f['metadata_flags'],0,bytes.fromhex(f['owner_uuid']),bytes.fromhex(f['group_uuid']),f['nlink'],*f['backup_time'])
  packet.extend(ENTRY.pack(*values));packet.extend(payload);records.append(dict(index=index,old_sha256=f['sha256'],payload_bytes=len(payload),new_sha256=new_hash.hex(),metadata_and_original_ACL_pinned=True,device_old_inode_retained=True))
 assert len(packet)<=131072
 # These are original byte-for-byte activation state checkpoints, never an
 # invented Activated/data_ark/Apple signed credential or migration seed.
 assert payloads[13]==raw[13]and payloads[14]==raw[14]
 receipt=dict(passed=True,mode=mode,build='20H392',ABI_bytes=288,bytes=len(packet),sha256=hashlib.sha256(packet).hexdigest(),backup_receipt_sha256=hashlib.sha256(backup.read_bytes()).hexdigest(),fixed_targets=[13,14,0,8],records=records,only_PurpleBuddy_keys_merged=list(KEYS),all_other_preference_values_retained=True,DataArk_checkpoint_byte_identical=True,migration_untouched=True,watchdog_untouched=True,no_activation_record_generated=True,requires_owner_manifest_refresh=True,does_not_activate_Apple_servers=True,path_private=path)
 return bytes(packet),receipt
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--backup',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--mode',choices=['apply','rollback'],default='apply');p.add_argument('--current',type=Path);a=p.parse_args();packet,receipt=prepare(a.backup,a.mode,a.current)
 with a.out.open('xb')as f:f.write(packet)
 a.out.with_suffix('.private.json').write_text(json.dumps(receipt,indent=2)+'\n');print('Prepared',a.mode,'plan:',len(packet),'bytes; three Setup keys only, original DataArk/ACL/keys retained, no device access')
