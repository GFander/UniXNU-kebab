from pathlib import Path
import difflib,hashlib,json
repo=Path('/home/albert/kebab-dev-ios16');runtime=Path('/home/albert/s4/x')
state=json.loads(Path('/home/albert/hios-series-diagnosis.json').read_text());assert state.get('passed')
scratch=Path(state['scratch']);paths=set()
for patch in sorted((repo/'patches/xnu-8796-kebab').glob('*.patch')):
    previous=None
    for line in patch.read_text(errors='surrogateescape').splitlines():
        if line.startswith('--- '):previous=line[4:].split('\t')[0]
        if line.startswith('+++ '):
            n=line[4:].split('\t')[0];n=previous if n=='/dev/null' else n
            assert n.startswith(('a/','b/'));paths.add(n[2:])
matched=[];different=[]
archive=Path('/home/albert/series-repair-20261029');archive.mkdir(exist_ok=True)
for name in sorted(paths):
    a=scratch/name;b=runtime/name
    left=a.read_bytes() if a.is_file() else None;right=b.read_bytes() if b.is_file() else None
    if left==right:matched.append(name);continue
    different.append({'path':name,'replayed_sha256':hashlib.sha256(left).hexdigest() if left is not None else None,
        'runtime_sha256':hashlib.sha256(right).hexdigest() if right is not None else None})
    text=''.join(difflib.unified_diff((left or b'').decode(errors='surrogateescape').splitlines(True),(right or b'').decode(errors='surrogateescape').splitlines(True),fromfile='replayed/'+name,tofile='runtime/'+name))
    f=archive/'differences'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text,errors='surrogateescape')
record={'patch_count':len(state['applied']),'paths':len(paths),'matched':matched,'different':different,
    'scope':'All paths named by the full ordered patch series; not unrelated/generated checkout files.'}
(archive/'runtime-comparison.json').write_text(json.dumps(record,indent=2)+'\n')
print('Full-series paths',len(paths),'matched',len(matched),'different',len(different))
for x in different:print(x['path'])
