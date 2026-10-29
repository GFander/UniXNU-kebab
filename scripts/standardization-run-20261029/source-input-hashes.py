from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2])
suffixes={'.c','.cpp','.h','.s','.S','.inc','.def','.mk','.mig','.defs','.arm64','.cmd','.rules','.plist'}
files={}
for p in root.rglob('*'):
    n=p.relative_to(root)
    if '.git' in n.parts or not p.is_file():continue
    if p.suffix not in suffixes and not (p.name.startswith(('Makefile','MASTER')) or p.name=='files'):continue
    files[n.as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
out.write_text(json.dumps({'root':str(root),'scope':'Listed source/header/assembly/MIG/config/build metadata extensions only; external SDK/archives excluded.',
    'suffixes':sorted(suffixes),'files':files},sort_keys=True,indent=2)+'\n')
print('Saved',len(files),'input hashes',out)
