"""Prove current strict common series then host overlay equals actual Mac inputs."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,tempfile,tarfile
ap=argparse.ArgumentParser();ap.add_argument('--archive',type=Path);args=ap.parse_args()
repo=Path('/home/albert/kebab-dev-ios16');cache=Path('/home/albert/.cache').resolve()
out=Path('/home/albert/series-repair-20261029');out.mkdir(exist_ok=True)
mac=json.loads((out/'mac-source-hashes-0221.json').read_text())['files']
common=json.loads((out/'ubuntu-source-hashes-0221.json').read_text())['files']
patches=sorted((repo/'patches/xnu-8796-kebab').glob('*.patch'));assert len(patches)==175
host=repo/'patches/hosts/macos-dxb-13.5/0001-tested-macos-dxb-preparation.patch'
root=Path(tempfile.mkdtemp(prefix='hios-final-replay-',dir=cache)).resolve()
assert root.parent==cache and root.name.startswith('hios-final-replay-')
def run(*args):return subprocess.run(args,text=True,capture_output=True,check=True).stdout.strip()
try:
    run('git','clone','--quiet','--shared','--no-checkout','/home/albert/ios-work/xnu8796',str(root))
    run('git','-C',str(root),'reset','--quiet','--hard','1b191cb')
    for p in patches+[host]:
        run('git','-C',str(root),'apply','--check','--whitespace=nowarn',str(p))
        run('git','-C',str(root),'apply','--whitespace=nowarn',str(p))
    run('python3','/home/albert/source-input-hashes.py',str(root),str(out/'fresh-replayed-macos-source-hashes-0221.json'))
    rebuilt=json.loads((out/'fresh-replayed-macos-source-hashes-0221.json').read_text())['files']
    assert rebuilt==mac,'fresh common+host replay differs from Mac'
    differences=sorted(n for n in common.keys()|mac.keys() if common.get(n)!=mac.get(n))
    assert len(differences)==10,differences
    result={'base':run('git','-C',str(root),'rev-parse','1b191cb'),
      'common_patch_count':len(patches),'host_patch_count':1,'mac_input_files':len(mac),
      'fresh_replay_equal_to_actual_mac':True,'common_vs_mac_host_paths':differences,
      'scope':'Explicit source/header/config inventory only; installed build dependencies pinned separately; not full Xcode/KDK identity.',
      'patches':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in patches+[host]}}
    (out/'fresh-replay-comparison-0221.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.archive:
        with tarfile.open(args.archive,'w:gz') as tar:
            for p in sorted(root.rglob('*')):
                relative=p.relative_to(root)
                if '.git' in relative.parts:continue
                if p.is_file() or p.is_symlink():tar.add(p,arcname='xnu/'+relative.as_posix(),recursive=False)
        print('Exported verified fresh source',args.archive,args.archive.stat().st_size,'bytes')
    print('PASS:',len(patches),'strict common + 1 host overlay;',len(mac),'actual Mac source inputs equal')
finally:
    assert root.parent==cache and root.name.startswith('hios-final-replay-')
    shutil.rmtree(root)
