"""Validate exact native initializer/metadata bodies with explicit VFS models.
Actual daemon, encrypted Data and durable reboot qualification is separate.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
for name in ('source','qualification','cache'):ap.add_argument('--'+name,type=Path,required=True)
a=ap.parse_args();q=a.qualification
manifest=json.loads((q/'all-mac-source-0452.json').read_text())['files']
fixtures={}
for source,receipt_name,fixture_name,function in [
 ('bsd/dev/kebab_data_template_impl.h','test-installer-library-result-0452.json','installer-library-production-test.cpp','kdi_installer_library_prepare'),
 ('bsd/kern/kebab_bootdiag.c','test-installer-directories-result-0451.json','installer-directories-production-test.cpp','kebab_installer_directories_report')]:
 text=(a.source/source).read_text();receipt=json.loads((q/receipt_name).read_text());assert receipt['passed']
 assert hashlib.sha256(text.encode()).hexdigest()==receipt['source_sha256']==manifest[source]
 start=text.index('static '+('int'if function.endswith('prepare')else'void')+'\n'+function+'(');end=text.index('\n}\n',start)+3;body=text[start:end]
 raw=(q/fixture_name).read_bytes();fixture=raw.decode()
 assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256'] and hashlib.sha256(body.encode()).hexdigest()==receipt['function_sha256'] and body in fixture
 fixtures[fixture_name]=fixture
template=(a.source/'bsd/dev/kebab_data_template_impl.h').read_text()
assert 'if(!error&&action==3)error=kdi_installer_library_prepare(data);'in template
assert template.index('if(!error&&action==3)error=kdi_installer_library_prepare(data);')<template.index('if(!error)error=koos_identity();')
# The fixed lookup retains its existing no-symlink and exact-mount contract.
assert 'nd.ni_flag|=NAMEI_NOFOLLOW_ANY;error=namei(&nd);'in template
assert 'vnode_mount(*file)!=wanted'in template
usb=json.loads((q/'test-migrator-usb-result-0446.json').read_text())
assert hashlib.sha256((a.source/'bsd/dev/kebab_usb.c').read_bytes()).hexdigest()==manifest['bsd/dev/kebab_usb.c']==usb['source_sha256']
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='installer-library-',dir=a.cache)as td:
 p=Path(td)/'fixture.cpp';fixture=fixtures['installer-library-production-test.cpp'];start=fixture.index('static int\nkdi_installer_library_prepare(');end=fixture.index('\n}\n',start)+3;s=fixture[start:end];variants=[]
 def change(name,old,new):
  assert s.count(old)==1,(name,s.count(old));variants.append((name,fixture[:start]+s.replace(old,new)+fixture[end:]))
 change('wrong-owner-gate','a.va_uid==(uid_t)-2&&','true&&')
 change('wrong-group-gate','a.va_gid==(gid_t)-2&&','true&&')
 change('ignore-flags','a.va_flags==0&&','true&&')
 change('ignore-protection-gate','a.va_dataprotect_class==0;','true;')
 change('wrong-desired-mode','VATTR_SET(&desired,va_mode,0751);','VATTR_SET(&desired,va_mode,0777);')
 change('wrong-desired-protection','VATTR_SET(&desired,va_dataprotect_class,4);','VATTR_SET(&desired,va_dataprotect_class,3);')
 change('ignore-set-failure','error=vnode_setattr(file,&desired,vfs_context_current());','(void)vnode_setattr(file,&desired,vfs_context_current());error=0;')
 change('omit-vnode-release','vnode_put(file);','if(sets==99)vnode_put(file);')
 change('ignore-flush-failure','if(!error)error=kebab_ufs_sync();','if(!error){(void)kebab_ufs_sync();error=0;}')
 variants.insert(0,('actual-library',fixture))
 variants.insert(1,('actual-metadata',fixtures['installer-directories-production-test.cpp']))
 for name,text in variants:
  q=p.with_name('installer-library-'+name+'.cpp');q.write_text(text);exe=q.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(q),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  if name.startswith('actual-'):
   assert result.returncode==0,(name,result.stdout,result.stderr);print(result.stdout.strip())
  else:
   assert result.returncode and 'Assertion'in result.stderr,(name,result.stdout,result.stderr);print('Rejected',name,flush=True)
print('PASS exact initializer and metadata production functions,43modeled cases/9behavioral mutants; unchanged USB and prior Data identity/no-symlink contract; not actual hardware proof')
