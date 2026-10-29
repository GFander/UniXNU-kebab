"""Install only a byte-verified replay, preserving the complete prior Mac tree."""
from pathlib import Path
import hashlib,json,subprocess,tarfile
r=Path('/Users/albertlee/kebab-build').resolve();archive=r/'fresh-xnu-0222.tar.gz'
stage=r/'replay-0222-install';actual=r/'dxb/xnu';backup=r/'xnu-before-clean-replay-0222'
assert actual.resolve()==r/'dxb/xnu' and actual.is_dir() and not actual.is_symlink()
assert not stage.exists() and not backup.exists()
with tarfile.open(archive,'r:gz') as tar:
    members=tar.getmembers();assert all(m.isfile() and m.name.startswith('xnu/') and '..' not in Path(m.name).parts and not Path(m.name).is_absolute() for m in members)
    assert sum(m.size for m in members)<300_000_000
    stage.mkdir();tar.extractall(stage,members=members)
subprocess.run(['python3',str(r/'source-input-hashes.py'),str(stage/'xnu'),str(r/'fresh-staged-source-hashes-0222.json')],check=True)
new=json.loads((r/'fresh-staged-source-hashes-0222.json').read_text())['files']
old=json.loads((r/'mac-source-hashes-0221.json').read_text())['files']
assert new==old,'fresh source does not match the tested Mac source inventory'
actual.rename(backup);(stage/'xnu').rename(actual);stage.rmdir()
result={'actual':str(actual),'retained_backup':str(backup),'verified_inputs':len(new),
 'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
 'source':'pristine 1b191cb + 175 strict common patches + 1 host overlay; no Git metadata or old build output'}
(r/'source-replacement-0222.json').write_text(json.dumps(result,indent=2)+'\n')
print('Installed fresh verified source;',len(new),'input hashes match; old complete tree retained at',backup)
