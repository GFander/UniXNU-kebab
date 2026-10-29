from pathlib import Path
import hashlib,json
state=json.loads(Path('/home/albert/hios-series-diagnosis.json').read_text());assert state.get('passed')
a=Path(state['scratch']);b=Path('/home/albert/s4/x')
suffixes={'.c','.cpp','.h','.s','.S','.inc','.def','.mk','.mig','.defs','.arm64'}
def inventory(root):
    return {p.relative_to(root).as_posix():p for p in root.rglob('*') if '.git' not in p.relative_to(root).parts
        and p.is_file() and (p.suffix in suffixes or p.name.startswith('Makefile') or p.name=='files')}
left=inventory(a);right=inventory(b);different=[];matched=0
for name in sorted(left.keys()|right.keys()):
    x=left[name].read_bytes() if name in left else None;y=right[name].read_bytes() if name in right else None
    if x==y:matched+=1
    else:different.append({'path':name,'replayed_sha256':hashlib.sha256(x).hexdigest() if x is not None else None,
        'runtime_sha256':hashlib.sha256(y).hexdigest() if y is not None else None})
out={'scope':'Source/header/assembly/MIG/Makefile and architecture file-list extensions only; no Git, unrelated build outputs or external SDK/archive inputs.',
 'suffixes':sorted(suffixes),'matched':matched,'different':different}
Path('/home/albert/series-repair-20261029/source-input-comparison.json').write_text(json.dumps(out,indent=2)+'\n')
print('Source inputs matched',matched,'different',len(different))
for x in different[:50]:print(x['path'])
