"""Replay public board/host patches and compare every prepared XNU source file.

Requires a local clone of the pinned Apple OSS XNU repository and git. No SDK,
IPSW, device data, hardware key, native binary or network access is needed.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,shutil
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,required=True);p.add_argument('--xnu-source',type=Path,required=True)
p.add_argument('--manifest',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True)
p.add_argument('--cache',type=Path,default=Path(tempfile.gettempdir()))
p.add_argument('--expected-common',type=int,default=268)
a=p.parse_args();repo=a.repo.resolve();cache=a.cache.resolve();base='1b191cb58250d0705d8a51287127505aa4bc0789'
expected=json.loads(a.manifest.read_text())['files']
common=sorted((repo/'patches/xnu-8796-kebab').glob('*.patch'))
hosts=sorted((repo/'patches/hosts/macos-dxb-13.5').glob('*.patch'))
assert len(common)==a.expected_common and len(hosts)==4
def run(*args):return subprocess.run(args,text=True,capture_output=True,check=True).stdout.strip()
def inventory(root):
 result={}
 for path in sorted(root.rglob('*')):
  rel=path.relative_to(root)
  if '.git' in rel.parts or '__pycache__' in rel.parts or path.name in ('.DS_Store','compile_commands.json') or path.suffix=='.pyc':continue
  if path.is_file():result[rel.as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
 return result
root=Path(tempfile.mkdtemp(prefix='kebab-qualified-replay-',dir=cache)).resolve()
try:
 run('git','clone','--quiet','--shared','--no-checkout',str(a.xnu_source.resolve()),str(root))
 run('git','-C',str(root),'reset','--quiet','--hard',base)
 for patch in common+hosts:
  run('git','-C',str(root),'apply','--check','--whitespace=nowarn',str(patch))
  run('git','-C',str(root),'apply','--whitespace=nowarn',str(patch))
 actual=inventory(root)
 differences=[n for n in sorted(expected.keys()|actual.keys()) if expected.get(n)!=actual.get(n)]
 assert not differences,differences
 result=dict(base=base,common_patches=len(common),host_patches=len(hosts),all_source_files=len(actual),all_source_equal=True,
  source_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),
  patch_sha256={str(x.relative_to(repo)):hashlib.sha256(x.read_bytes()).hexdigest() for x in common+hosts},
  scope='Prepared source only; external SDK/KDK/toolchain, binaries and runtime behavior are separate qualifications.')
 a.receipt.parent.mkdir(parents=True,exist_ok=True);a.receipt.write_text(json.dumps(result,indent=2)+'\n')
 print('PASS public replay:',len(common),'common +',len(hosts),'host;',len(actual),'source files exactly match compiled source manifest')
finally:
 assert root.parent==cache and root.name.startswith('kebab-qualified-replay-')
 shutil.rmtree(root)
