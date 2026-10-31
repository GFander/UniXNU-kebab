"""Original APFS virtual-call contract vs the actual linked native provider/table."""
from pathlib import Path
import struct,hashlib,json,sys
import capstone
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';sys.path.insert(0,str(b))
from native_prelink import macho,backed_offset
version=sys.argv[1] if len(sys.argv)>1 else '0314'
pins={'0314':'aff8bb68f152113bfd4459530f4f2381ed55f48bc362f538713008d6455849dd',
 '0318':'29dfbe5bf8cf1d18b04b99f69836ea7a26226a0ae801218d3f5e809f460d7cdd',
 '0319':'9654d7417b4b250f68ab336007e915848528ba76d486647460bd0a5df0e9b7d3',
 '0320':'6ff71840c8b2bd2320a5f13d83daa5a0bc7c3cb16d016a24970daeb10666d50c',
 '0321':'cfbb6e034d21132d6de85ff80a1e2c33c6e50c8cced869fcaef54886bdd52310',
 '0322':'781e67db020dc4026db6971315e17d3d8f881d894b1ec7df0546e10444f9e52e',
 '0323':'d5911a7e9bfe4dab1852aaaf633509b8cd7e416f7e9c6b82213aaac03d952e3c',
 '0325':'93db50a675bd56878952fcf503577f05b3f87c4c53868197191054ddb5092998',
 '0326':'5746162c6c17982057686a7adad72a1af1034653d0e875ea9e9903842bb26981',
 '0327':'0564798d1e2f8781a997276105ba8bc47a3cefba0c2f527ed2ce365ede6868c8',
 '0328':'564c99726065d7893b738e0fd1aa3b098d56da974c861765ca3dc081f000e13e',
 '0329':'564c99726065d7893b738e0fd1aa3b098d56da974c861765ca3dc081f000e13e',
 '0330':'b201c7fac05b1383b9584d73ab2ebccc66005f6bd2093891976b03650fd195d6',
 '0331':'568b3243e30ea2fa12ae5d853618fb3e1e3fbe5a0ad1c640ff261742655ae703',
 '0332':'167a9a25d3b645975b6f106d3a7ee858cec6db60a307603d72355140885ca0e4',
 '0333':'27d5ff712dc1ba5bcf6eba180b75ba3881bc7dfb701e7c0d0b11b285b24e7461',
 '0334':'400501e4712674a9d0589e8bece162c9aefb7c6579941c063b488849d3bd4045',
 '0335':'a1b449434130d7713a1112009338b35a8261e5d42aca549949d9f1ba7b4b7192',
 '0336':'a1b449434130d7713a1112009338b35a8261e5d42aca549949d9f1ba7b4b7192',
 '0337':'bc235bab831fa08a91c00dc2740b2aa557bb7fed77ae98bb17720504f91953ae',
 '0338':'e5d4a43e465284e6523b674bcd2a506eb7e6d2a0f6efeef668f3cc8ac9bc5d7a',
 '0339':'556d8c72835ac088e11a2ff477b9c8dc4c129ec525e3c2b870785edfd0949716',
 '0340':'556d8c72835ac088e11a2ff477b9c8dc4c129ec525e3c2b870785edfd0949716',
 '0341':'556d8c72835ac088e11a2ff477b9c8dc4c129ec525e3c2b870785edfd0949716'}
pins.update({'0343': '37f6237844a1dc625ab1dcd457b4bb0d983d6b9f39290c424aef19ddc3cb210b', '0344': '37f6237844a1dc625ab1dcd457b4bb0d983d6b9f39290c424aef19ddc3cb210b', '0345': '37f6237844a1dc625ab1dcd457b4bb0d983d6b9f39290c424aef19ddc3cb210b', '0346': '37f6237844a1dc625ab1dcd457b4bb0d983d6b9f39290c424aef19ddc3cb210b', '0347': '37f6237844a1dc625ab1dcd457b4bb0d983d6b9f39290c424aef19ddc3cb210b'})
pins.update({v:'c350aa716e27b283d74f6d6986caf63d41901e99d640a28099778ebb8018764a' for v in ('0348','0349','0350','0351','0352','0353')})
pins['0352']='fb66a9648b56c4114df33461995a7d4d99a29717dbcffce611adb873884e865f'
pins['0353']='0f4f08c55fadc3ea0b1c525ba3bb5be6d07ada8675cc68ead140920b3eae0cca'
pins['0354']='8ab5f53160e598bb1b222b1ae746d2d46d7f2f05215abb0faf39ba91a9419475'
pins['0355']='8ab5f53160e598bb1b222b1ae746d2d46d7f2f05215abb0faf39ba91a9419475'
pins['0356']='8ab5f53160e598bb1b222b1ae746d2d46d7f2f05215abb0faf39ba91a9419475'
pins['0357']='b5eb053d951e1ecb3a271d33eee86cd38be08a600a0da600405d5cd3783dcbde'
pins['0358']='ffc1804a091298c6518e25b8476d02c85170cfa2661c90e2a4c08019eff6a726'
pins['0359']='6810448e59be4e49afd66c6f029ea83cd9b867c6faef3e944b063e8520604178'
pins['0360']='adcf6234f0fa017f81172176878afdf9c71fcddb8933b991dc9f5b6f7f51f08c'
pins['0361']='adcf6234f0fa017f81172176878afdf9c71fcddb8933b991dc9f5b6f7f51f08c'
pins['0362']='0c9b4eec41d35006e1f0426400901b20763dc4e3f936387faf86c9302fff8bdb'
pins['0363']='30a47a400e9a0612f6475a4c89be989066075c2432afa8940566f1eae7022f1f'
pins['0364']='30a47a400e9a0612f6475a4c89be989066075c2432afa8940566f1eae7022f1f'
pins['0365']='30a47a400e9a0612f6475a4c89be989066075c2432afa8940566f1eae7022f1f'
pins['0366']='b40a61b4df2b933bd66ba485efc72d80b4453fc94ecdb9cf8b424d859da03a01'
pins['0367']='ed0c2aafaf7d48603a8b2bafc9883c00fe80ccf6f32430b5a4170f73d80fa69e'
pins['0368']='183ae23f548dbf8f424bc50398a53ab847f697ae437913f41d2f5869144884ba'
pins['0369']='0345268886f3287e534517554ed1828a4e5581d80074336cf794c78fa04837db'
pins['0370']='5ccf6eece3a0243c2490032d6ca91d5617188f2e743af2c37b0f657c49b2e21b'
pins['0371']='687944fd1bb57d0a0bd6ec65d59b85d5bd2b089783d9e9b7f2a551473c997d2c'
pins['0372']='53ac9c164c0f2759b6b9b0f0525fcf83677f83f992ea7e948526b81c89f0a871'
pins['0373']='53ac9c164c0f2759b6b9b0f0525fcf83677f83f992ea7e948526b81c89f0a871'
pins['0374']='53ac9c164c0f2759b6b9b0f0525fcf83677f83f992ea7e948526b81c89f0a871'
pins['0375']='53ac9c164c0f2759b6b9b0f0525fcf83677f83f992ea7e948526b81c89f0a871'
pins['0376']='bce374874722d12f392d949aded751b12392c7ccf5938cab2e8607b3b29a16ff'
pins['0377']='bce374874722d12f392d949aded751b12392c7ccf5938cab2e8607b3b29a16ff'
pins['0378']='44d08558b1dc461ab5c2f090f99b1359b629166cd69c42b06ea126058c2c3ee1'
pins['0379']='c6feae80c87d695e3d698c2ced72a5772e347d204627911cf519d2e57da7bcef'
pins['0380']='44d08558b1dc461ab5c2f090f99b1359b629166cd69c42b06ea126058c2c3ee1'
pins['0381']='44d08558b1dc461ab5c2f090f99b1359b629166cd69c42b06ea126058c2c3ee1'
pins['0382']='44d08558b1dc461ab5c2f090f99b1359b629166cd69c42b06ea126058c2c3ee1'
pins['0383']='6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051'
pins['0384']='6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051'
pins['0385']='6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051'
pins['0386']='6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051'
pins['0387']='7c2ef66249ccf420160f09a3c6111f541014e402d73abf695d5622424388251d'
pins['0388']='25ee2e0c11059b4c8249819d036339cf1fcb287d060ac492a63176a27fdd259d'
pins['0389']='25ee2e0c11059b4c8249819d036339cf1fcb287d060ac492a63176a27fdd259d'
pins['0390']='25ee2e0c11059b4c8249819d036339cf1fcb287d060ac492a63176a27fdd259d'
pins['0391']='1518d30cffa942bf9b1d8a7df831b4789c5f53a36c3bf703e6cb54ff79bb0ce2'
pins['0392']='53fa13fb37dcb2aa299b76d9b1e59c37a4b8e18d99f95170a36f8bef3108316c'
pins['0393']='a3e170504cc422f8386c2da148e31daa41e5a2eb0e890ef049d46ed08da83e89'
pins['0394']='deb77c42915695561bf08ffe5ca76266e293ef39654684e99203c838ab154839'
pins['0395']='5873221200837a6b11ab067aacd4733816b44e200ff3768693dddcde3725fd6d'
pins['0396']='ce33dbbc233eb1f1081dc37c1f4b4f13a481e3df596960a1cf6d1e26bc65aa3c'
pins['0397']='ce33dbbc233eb1f1081dc37c1f4b4f13a481e3df596960a1cf6d1e26bc65aa3c'
pins['0398']='ce33dbbc233eb1f1081dc37c1f4b4f13a481e3df596960a1cf6d1e26bc65aa3c'
pins['0399']='ce33dbbc233eb1f1081dc37c1f4b4f13a481e3df596960a1cf6d1e26bc65aa3c'
pins['0400']='3646608fff75149fbebacd16361d36717dd10e867dcc534295cf8677e0b3d935'
pins['0401']='c91ac053582052287798782d778fbc9f368210ac9db84e720c7f343b2fe21dde'
pins['0402']='f15261dde02049a29796378de51c5503b20bc48e7c16a18cd533642001049074'
pins['0403']='f15261dde02049a29796378de51c5503b20bc48e7c16a18cd533642001049074'
pins['0404']='f15261dde02049a29796378de51c5503b20bc48e7c16a18cd533642001049074'
pins['0405']='f15261dde02049a29796378de51c5503b20bc48e7c16a18cd533642001049074'
pins['0406']='a840f478d3ed87898146adfe944d35f08fb665e9afd8a9eb121c58e3d8d90250'
pins['0407']='c8756bd4370a1d76f8ecb38058222e9a6a256def0b9993284bb955aad310c413'
pins['0408']='f5c2c9d5b999e95b91b985d538702017d5d55427a40fe9cec22f061241c961a8'
pins['0409']='aa65a83bc1576b97ba3b4e5d161b6068b2c7c0bac551d8966689fb434d99178b'
pins['0410']='ea9ed0dc9d8c19376e8a1ee5aa4f2748951c0cb71332493d0f467f1163e4d536'
pins['0411']='9aa24e39fd2405382ddbd72d290324551604e328e0695c1a0a6c1be8ceb768be'
pins['0412']='2b98799edc040e4beddd89820e6f56adc06753a277eece40f6e5052ec02c2186'
pins['0413']='635923d3f115a49d73cce65b536265909f21ee6e9c2bbf8faf64181eefd720f1'
pins['0414']='851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715'
pins['0415']='851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715'
pins['0416']='851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715'
pins['0417']='851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715'
pins['0418']='09d7ca40c3103ef833377b0b956edf43b3e73c0edd04dbbd8b2ea0a326ee4c5e'
pins['0419']='1042f0fdd6f3e16bd47ffa1760454aafa34a01376f44da60534ed2106c535291'
pins['0420']='1084d103b41d60a4fd9f6936cc7a9b1d695ab8c5c9d458801fcd1c222dc45c2e'
pins['0421']='80803c9683346a6fe99cb1a9fb94a3dadcc1b6c96e3c43d2a1ccdb697b694c6a'
pins['0422']='80803c9683346a6fe99cb1a9fb94a3dadcc1b6c96e3c43d2a1ccdb697b694c6a'
pins['0423']='bea59aaf413c87356caadad4c3cd8c2fab52c7b91a62b69fb9e4a228eff11366'
pins['0424']='bea59aaf413c87356caadad4c3cd8c2fab52c7b91a62b69fb9e4a228eff11366'
pins['0425']='9e720064c4e8e691352a11edfda0231ec05f4fdf923fc249c1ea33cb87e8bde9'
pins['0426']='9e720064c4e8e691352a11edfda0231ec05f4fdf923fc249c1ea33cb87e8bde9'
pins['0427']='b9af2033c94445303a6f75bd78bbc0b34b08b1d892f61c9297df002a08c005cb'
assert version in pins
native=(b/('kernel-'+version+'.kebab')).read_bytes();data=(b/('k'+version+'.noctf')).read_bytes()
digest=hashlib.sha256(native).hexdigest();assert digest==pins[version]
image=macho(data);symbols={}
cmd=next(x for x in image['commands'] if x[0]==2)
so,count,st,size=struct.unpack_from('<4I',data,cmd[1]+8);strings=data[st:st+size]
for i in range(count):
 name,kind,_,_,va=struct.unpack_from('<IBBHQ',data,so+16*i)
 if kind&0xe0 or kind&0x0e!=0x0e:continue
 end=strings.find(b'\0',name);assert 0<=name<end<=size
 label=strings[name:end].decode();symbols.setdefault(label,[]).append(va)
def symbol(name):
 values=symbols[name];assert len(set(values))==1,(name,values);return values[0]
def read(va,n):
 off=backed_offset(image['segments'],va,n,len(data));return data[off:off+n]
vtable=symbol('__ZTV13KebabKeyStore');method=symbol('__ZN13KebabKeyStore20callPlatformFunctionEPK8OSSymbolbPvS3_S3_S3_')
assert struct.unpack('<Q',read(vtable+16+0x3a8,8))[0]==method
table=symbol('__ZL12kfk_services');entries=struct.unpack('<23Q',read(table,184))
expected={0:'_kebab_file_key_unwrap',1:'_kebab_file_key_rewrap',2:'_kebab_file_key_new',
 7:'_kebab_metadata_key_unwrap',9:'_kebab_metadata_key_wrap'}
if version in ('0322', '0323', '0325', '0326', '0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427'):expected.update({7:'_kebab_metadata_key_unwrap_wrapped',9:'_kebab_metadata_key_create_wrapped'})
if version in ('0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427'):expected.update({0:'_kebab_apfs_file_key_unwrap',1:'_kebab_apfs_file_key_rewrap',2:'_kebab_apfs_file_key_new',21:'_kebab_apfs_file_key_is_available'})
for i,value in enumerate(entries):assert value==(symbol(expected[i]) if i in expected else 0),(i,hex(value))
donor=(b/'kernelcache.fileset.macho').read_bytes();donor_sha=hashlib.sha256(donor).hexdigest()
assert donor_sha=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
segments=macho(donor)['segments'];decoder=capstone.Cs(capstone.CS_ARCH_ARM64,0)
checks={0xfffffff006175ac0:('ldr','x8, [x8, #0x3a8]'),
 0xfffffff006175ac4:('add','x3, sp, #8'),0xfffffff006175ad0:('mov','w2, #1'),
 0xfffffff006175ae0:('blr','x8')}
for va,wanted in checks.items():
 off=backed_offset(segments,va,4,len(donor));x=next(decoder.disasm(donor[off:off+4],va));assert (x.mnemonic,x.op_str)==wanted
result=dict(native_sha256=digest,stripped_ctf_sha256=hashlib.sha256(data).hexdigest(),
 donor_sha256=donor_sha,original_apfs_instructions_checked=len(checks),virtual_method_byte_offset=0x3a8,
 native_provider_vtable=hex(vtable),native_provider_method=hex(method),native_vtable_slot_equal=True,
 native_service_table=hex(table),table_bytes=184,supported_slots={hex(i*8):name for i,name in expected.items()},
 unsupported_null_slots=17 if version in ('0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427') else 18,all_table_function_pointers_equal=True,
 note='ABI slot and linked function pointers only. Original APFS runtime client, Data keybags/mount/control, class3 and FileVault provider remain unqualified.')
if version in ('0319', '0320', '0321', '0322', '0323', '0325', '0326', '0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427'):
 fv=symbol('__ZL12kfv_services');pointers=struct.unpack('<12Q',read(fv,96))
 wanted={0:'_kebab_volume_key_new',1:'_kebab_volume_key_unwrap',4:'_kebab_volume_key_length'}
 if version in ('0321', '0322', '0323', '0325', '0326', '0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427'):wanted.update({0:'_kebab_volume_key_new_wrapped',1:'_kebab_volume_key_unwrap_wrapped'})
 if version in ('0326', '0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427'):wanted.update({7:'_kebab_volume_blob_state',8:'_kebab_volume_set_protection'})
 for i,value in enumerate(pointers):assert value==(symbol(wanted[i]) if i in wanted else 0),(i,hex(value))
 result.update(filevault_service_table=hex(fv),filevault_table_bytes=88,filevault_next_entry_null=True,
  filevault_supported_slots={hex(i*8):name for i,name in wanted.items()},filevault_unsupported_null_slots=6 if version in ('0326', '0327', '0328', '0329', '0330', '0331', '0332', '0333', '0334', '0335', '0336', '0337', '0338', '0339', '0340', '0341', '0343', '0344', '0345', '0346', '0347', '0348', '0349', '0350', '0351', '0352', '0353', '0354', '0355', '0356', '0357', '0358', '0359', '0360', '0361', '0362', '0363', '0364', '0365', '0366', '0367', '0368', '0369', '0370', '0371', '0372', '0373', '0374', '0375', '0376', '0377', '0378', '0379', '0380', '0381', '0382', '0383', '0384', '0385', '0386', '0387', '0388', '0389', '0390', '0391', '0392', '0393', '0394', '0395', '0396', '0397', '0398', '0399', '0400', '0401', '0402', '0403', '0404', '0405', '0406', '0407', '0408', '0409', '0410', '0411', '0412', '0413', '0414', '0415', '0416', '0417', '0418', '0419', '0420', '0421', '0422', '0423', '0424', '0425', '0426', '0427') else 8,
  filevault_all_function_pointers_equal=True)
(r/(version+'-aks-provider-abi.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
