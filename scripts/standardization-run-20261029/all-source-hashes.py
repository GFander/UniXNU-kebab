"""Inventory all regular source-tree files, including scripts and extensionless inputs."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();files={}
excluded=[]
for p in root.rglob('*'):
    rel=p.relative_to(root)
    if '.git' in rel.parts or '__pycache__' in rel.parts or not p.is_file():continue
    if p.name in {'compile_commands.json','.DS_Store'} or p.suffix=='.pyc':
        excluded.append(rel.as_posix());continue
    files[rel.as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
Path(sys.argv[2]).write_text(json.dumps({'root':str(root),'scope':'All regular files under the source tree except .git, __pycache__, .pyc, .DS_Store and generated compile_commands.json; symlink targets read as files.',
 'excluded_generated':excluded,'files':files},sort_keys=True,indent=2)+'\n')
print('Saved',len(files),'all-source file hashes; excluded',len(excluded),'generated files')
