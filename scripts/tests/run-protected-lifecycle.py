"""Replay complete private-file and installation-marker lifecycle tests.
VFS/APFS/raw reads are modeled; sealed physical receipts are separate.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__)
for n in ('source','qualification','cache'):ap.add_argument('--'+n,type=Path,required=True)
a=ap.parse_args();q=a.qualification
sha=lambda b:hashlib.sha256(b).hexdigest()
m=json.loads((q/'all-mac-source-0459.json').read_text())['files']
fixtures={}
for source,receipt_name,fixture_name in [
 ('bsd/kern/kebab_data_files_impl.h','test-fixture-lifecycle-result-0459.json','fixture-lifecycle-production-test.c'),
 ('bsd/dev/kebab_data_template_impl.h','test-template-marker-result-0459.json','template-marker-production-test.c')]:
 text=(a.source/source).read_text();receipt=json.loads((q/receipt_name).read_text());assert receipt['passed']
 assert sha(text.encode())==receipt['source_sha256']==m[source]
 raw=(q/fixture_name).read_bytes();fixture=raw.decode();assert sha(raw)==receipt['fixture_sha256']
 if 'production_prefix_sha256'in receipt:
  prefix=re.sub(r'^#include [^\n]+\n','',text[:text.index('static int\nkdp_preserve(')],flags=re.M)
  assert sha(prefix.encode())==receipt['production_prefix_sha256']and prefix in fixture
 else:
  for name,h in receipt['function_sha256'].items():
   pos=text.index('\n'+name+'(')+1;start=text.rfind('\n',0,pos-1)+1;end=text.index('\n}\n',pos)+3;body=text[start:end]
   assert sha(body.encode())==h and body.replace('kdi_marker(','kdi_marker_after(')in fixture
 fixtures[fixture_name]=fixture
assert sha((a.source/'bsd/dev/kebab_data_template_contract.h').read_bytes())==m['bsd/dev/kebab_data_template_contract.h']

def fixture_variants(fixture):
 variants=[]
 def change(name,old,new):
  assert fixture.count(old)==1,(name,fixture.count(old));variants.append((name,fixture.replace(old,new)))
 change('ignore-profile','error==EAUTH&&tool==12&&!create','error==EAUTH&&tool!=99&&!create')
 change('ignore-fixed-path','!strcmp(path,"/private/var/.hios-cp-d-v1")&&','true&&')
 change('ignore-mode','(attr.va_mode&07777)!=0600','false')
 change('ignore-original-payload','if(!error&&memcmp(w->plain,w->back,KDP_BYTES)){','if(false&&memcmp(w->plain,w->back,KDP_BYTES)){')
 change('ignore-plaintext-on-disk','if(!error&&!memcmp(w->cipher,w->plain+logical,4096))error=EAUTH;','if(false&&!memcmp(w->cipher,w->plain+logical,4096))error=EAUTH;')
 start=fixture.index('static int\nkdp_restore_fixture_d(');end=fixture.index('\n}\n',start)+3;body=fixture[start:end]
 for name,old,new in [
  ('ignore-authorization','if(!error)error=vnode_authorize(file,NULLVP,KAUTH_VNODE_ACCESS|KAUTH_VNODE_WRITE_DATA,context);','if(!error){(void)vnode_authorize(file,NULLVP,KAUTH_VNODE_ACCESS|KAUTH_VNODE_WRITE_DATA,context);error=0;}'),
  ('ignore-setattr-error','error=VNOP_SETATTR(file,&attr,context);','(void)VNOP_SETATTR(file,&attr,context);error=0;'),
  ('ignore-flush-error','if(!error)error=synced;','(void)synced;')]:
  assert body.count(old)==1;variants.append((name,fixture[:start]+body.replace(old,new)+fixture[end:]))
 change('omit-complete-second-proof','observed_class=cls;goto read_file;','observed_class=cls;')
 return variants


def marker_variants(fixture):
 variants=[]
 start=fixture.index('static int\nkdi_marker_after(');end=fixture.index('\n}\n',start)+3;body=fixture[start:end]
 def change(name,old,new):
  assert body.count(old)==1,(name,body.count(old));variants.append((name,fixture[:start]+body.replace(old,new)+fixture[end:]))
 change('accept-unsupported-B','write||a.va_dataprotect_class!=3','write||a.va_dataprotect_class!=2')
 change('allow-C-marker-write','write||a.va_dataprotect_class!=3','false||a.va_dataprotect_class!=3')
 change('ignore-owner','||a.va_uid||','||false||')
 change('ignore-group','||a.va_gid||','||false||')
 change('ignore-mode','(a.va_mode&07777)!=0600','false')
 change('check-only-contract-prefix','memcmp(expected,w->marker,4096)','memcmp(expected,w->marker,64)')
 change('ignore-short-read','if(!error&&residual)error=EIO;if(error)goto done;','if(error)goto done;')
 change('accept-incomplete-as-finished','*finished=bit;','*finished=true;')
 change('follow-symlink','O_NOFOLLOW|FREAD|(write?FWRITE:0)','FREAD|(write?FWRITE:0)')
 return variants


a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='protected-lifecycle-',dir=a.cache)as td:
 for filename,label in [('fixture-lifecycle-production-test.c','fixture'),('template-marker-production-test.c','marker')]:
  fixture=fixtures[filename];variants=fixture_variants(fixture)if label=='fixture'else marker_variants(fixture)
  for index,(name,text)in enumerate([('actual-'+label,fixture)]+variants):
   p=Path(td)/(label+'-'+name+'.c');p.write_text(text);exe=p.with_suffix('')
   args=['clang','-D_DEFAULT_SOURCE','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-deprecated-declarations','-Wno-unused-label','-fsanitize=address,undefined',str(p)]
   subprocess.run(args+(['-lcrypto']if label=='fixture'else[])+['-o',str(exe)],check=True)
   result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
   if not index:assert result.returncode==0,(name,result.stdout,result.stderr);print(result.stdout.strip())
   else:assert result.returncode and 'Assertion'in result.stderr,(name,result.stdout,result.stderr);print('Rejected',name,flush=True)
print('PASS complete protected private-file/marker production, original error propagation and18behavioral mutants; no hardware crypto claim from host models')
