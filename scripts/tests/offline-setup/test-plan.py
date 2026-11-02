from pathlib import Path
import copy,hashlib,importlib.util,json,plistlib,struct,tempfile,unittest
spec=importlib.util.spec_from_file_location('plan',Path(__file__).with_name('make-offline-plan.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class PlanTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.w=Path(self.tmp.name);self.backup=self.w/'proof.json';self.current=self.w/'current.json';files={}
  contents={0:{'test-cache':True},8:{'setupMigratorVersion':12,'preserved':['test',7]},13:{'-DarkProductVersion':'16.7.16'},14:{'-BuildVersion':'20H392','-ActivationState':'Unactivated'}}
  for i,d in contents.items():
   raw=plistlib.dumps(d,fmt=plistlib.FMT_BINARY);file=self.w/(str(i)+'.plist');file.write_bytes(raw);files[str(i)]=dict(file=str(file),sha256=hashlib.sha256(raw).hexdigest(),size=len(raw),protection=4,flags=0,nlink=1,xattr_names_bytes=0,metadata_flags=7,uid=0 if i==13 else 501,gid=4294967294 if i==14 else 501,backup_time=[0,0],canonical_path='/private/var/containers/Data/System/00000000-0000-0000-0000-000000000001/Library/internal/data_ark.plist'if i==14 else'/test',acl_bytes=32 if i==14 else 0,acl_sha256=hashlib.sha256(b'test-acl'if i==14 else b'').hexdigest(),fileid=100+i,mode=0o100600,times=[1,0,2,0,3,0,4,0],owner_uuid='00'*16,group_uuid='00'*16)
  self.proof=dict(accepted=True,build='20H392',log_drop_delta=0,files=files);self.save();self.now=copy.deepcopy(self.proof)
  for f in self.now['files'].values():f['fileid']+=1000
  self.current.write_text(json.dumps(self.now))
 def save(self):self.backup.write_text(json.dumps(self.proof))
 def reject(self,change):
  change();self.save()
  with self.assertRaises((AssertionError,ValueError,KeyError,struct.error)):m.prepare(self.backup)
 def entries(self,data):
  result={};offset=16
  for _ in range(struct.unpack_from('<I',data,12)[0]):
   e=m.ENTRY.unpack_from(data,offset);length=e[1]if struct.unpack_from('<I',data,8)[0]==1 else 0;result[e[0]]=(e,data[offset+288:offset+288+length]);offset+=288+length
  self.assertEqual(offset,len(data));return result
 def test_apply_three_keys_only(self):
  data,q=m.prepare(self.backup);entries=self.entries(data);self.assertEqual(list(entries),[13,14,0,8]);pref=plistlib.loads(entries[8][1]);self.assertEqual(pref,dict(setupMigratorVersion=12,preserved=['test',7],SetupDone=True,SetupFinishedAllSteps=True,SetupVersion=11));self.assertEqual(entries[0][1],b'');self.assertEqual(entries[14][1],Path(self.proof['files']['14']['file']).read_bytes());self.assertEqual(q['ABI_bytes'],288)
 def test_existing_newer_version_retained(self):
  f=self.proof['files']['8'];raw=plistlib.dumps(dict(SetupVersion=17,SetupDone=False,SetupFinishedAllSteps=False),fmt=plistlib.FMT_BINARY);Path(f['file']).write_bytes(raw);f.update(sha256=hashlib.sha256(raw).hexdigest(),size=len(raw));self.save();self.assertEqual(plistlib.loads(self.entries(m.prepare(self.backup)[0])[8][1])['SetupVersion'],17)
 def test_rollback_pins_fresh_current_inode_hash(self):
  data,q=m.prepare(self.backup,'rollback',self.current);self.assertEqual(len(data),1168);self.assertEqual(struct.unpack_from('<I',data,8)[0],2)
  for i,(e,payload)in self.entries(data).items():self.assertEqual(e[6].hex(),self.proof['files'][str(i)]['sha256']);self.assertEqual(e[7].hex(),self.now['files'][str(i)]['sha256']);self.assertEqual(e[23],self.now['files'][str(i)]['fileid']);self.assertEqual(payload,b'')
 def test_rollback_requires_current(self):
  with self.assertRaises(AssertionError):m.prepare(self.backup,'rollback')
 def test_bad_mode(self):
  with self.assertRaises(AssertionError):m.prepare(self.backup,'arbitrary')
 def test_rejected_proof(self):self.reject(lambda:self.proof.update(accepted=False))
 def test_wrong_build(self):self.reject(lambda:self.proof.update(build='21H461'))
 def test_log_loss(self):self.reject(lambda:self.proof.update(log_drop_delta=1))
 def test_wrong_class(self):self.reject(lambda:self.proof['files']['8'].update(protection=3))
 def test_flags(self):self.reject(lambda:self.proof['files']['8'].update(flags=1))
 def test_hardlink(self):self.reject(lambda:self.proof['files']['8'].update(nlink=2))
 def test_xattr(self):self.reject(lambda:self.proof['files']['8'].update(xattr_names_bytes=4))
 def test_unknown_metadata(self):self.reject(lambda:self.proof['files']['8'].update(metadata_flags=8))
 def test_wrong_owner(self):self.reject(lambda:self.proof['files']['14'].update(uid=0))
 def test_hash_corruption(self):self.reject(lambda:self.proof['files']['8'].update(sha256='00'*32))
 def test_size_corruption(self):self.reject(lambda:self.proof['files']['8'].update(size=65537))
 def test_bad_time(self):self.reject(lambda:self.proof['files']['8'].update(backup_time=[0,1000000000]))
 def test_path_traversal(self):self.reject(lambda:self.proof['files']['14'].update(canonical_path='/private/var/containers/Data/System/../Library/internal/data_ark.plist'))
 def test_wrong_product(self):
  f=self.proof['files']['13'];raw=plistlib.dumps({'-DarkProductVersion':'16.7.17'});Path(f['file']).write_bytes(raw);f.update(sha256=hashlib.sha256(raw).hexdigest(),size=len(raw));self.reject(lambda:None)
 def test_bad_setup_type(self):
  f=self.proof['files']['8'];raw=plistlib.dumps({'SetupDone':'true'});Path(f['file']).write_bytes(raw);f.update(sha256=hashlib.sha256(raw).hexdigest(),size=len(raw));self.reject(lambda:None)
if __name__=='__main__':unittest.main()
