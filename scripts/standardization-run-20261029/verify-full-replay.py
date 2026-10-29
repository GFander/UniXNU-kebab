"""Check all source-tree inputs after strict common + host replay, including scripts/exports."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,tarfile,tempfile
ap=argparse.ArgumentParser();ap.add_argument('version');ap.add_argument('--archive',type=Path);ap.add_argument('--expected-common',type=int,default=175);args=ap.parse_args()
repo=Path('/home/albert/kebab-dev-ios16');cache=Path('/home/albert/.cache').resolve();out=Path('/home/albert/series-repair-20261029')
mac=json.loads((out/('all-mac-source-'+args.version+'.json')).read_text())['files']
common=sorted((repo/'patches/xnu-8796-kebab').glob('*.patch'));hosts=sorted((repo/'patches/hosts/macos-dxb-13.5').glob('*.patch'))
assert len(common)==args.expected_common and hosts
root=Path(tempfile.mkdtemp(prefix='hios-all-replay-',dir=cache)).resolve()
def run(*a):return subprocess.run(a,text=True,capture_output=True,check=True).stdout.strip()
def inventory(name):
    p=out/name;run('python3','/home/albert/all-source-hashes.py',str(root),str(p));return json.loads(p.read_text())['files']
try:
    run('git','clone','--quiet','--shared','--no-checkout','/home/albert/ios-work/xnu8796',str(root))
    run('git','-C',str(root),'reset','--quiet','--hard','1b191cb')
    for p in common:
        run('git','-C',str(root),'apply','--check','--whitespace=nowarn',str(p));run('git','-C',str(root),'apply','--whitespace=nowarn',str(p))
    baseline=inventory('all-common-source-'+args.version+'.json')
    for p in hosts:
        run('git','-C',str(root),'apply','--check','--whitespace=nowarn',str(p));run('git','-C',str(root),'apply','--whitespace=nowarn',str(p))
    replay=inventory('all-replayed-source-'+args.version+'.json')
    differences=[n for n in sorted(mac.keys()|replay.keys()) if mac.get(n)!=replay.get(n)]
    assert not differences,differences
    result={'base':run('git','-C',str(root),'rev-parse','1b191cb'),'common_patches':len(common),'host_patches':len(hosts),
      'all_source_files':len(mac),'all_source_equal':True,
      'host_paths':[n for n in sorted(baseline.keys()|replay.keys()) if baseline.get(n)!=replay.get(n)],
      'scope':'All regular source-tree files except .git, __pycache__, .pyc, .DS_Store and generated compile_commands.json; external SDK/KDK/toolchain excluded.',
      'patch_sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in common+hosts}}
    if args.archive:
        with tarfile.open(args.archive,'w:gz') as tar:
            for p in sorted(root.rglob('*')):
                rel=p.relative_to(root)
                if '.git' not in rel.parts and (p.is_file() or p.is_symlink()):tar.add(p,arcname='xnu/'+rel.as_posix(),recursive=False)
        result['archive_sha256']=hashlib.sha256(args.archive.read_bytes()).hexdigest()
    (out/('all-source-replay-'+args.version+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',len(common),'common +',len(hosts),'host patches;',len(mac),'all-source files equal')
finally:
    assert root.parent==cache and root.name.startswith('hios-all-replay-');shutil.rmtree(root)
