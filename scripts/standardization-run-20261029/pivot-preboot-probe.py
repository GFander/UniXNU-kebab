"""Qualify plain APFS storage on Preboot without relaxing iOS Data protection."""
from pathlib import Path
import difflib,shutil
r=Path(__file__).parent;old=r/'data-mount';d=r/'apfs-probe';transition=r/'apfs-probe-transition'
before={p.relative_to(old/'after').as_posix():p.read_text() for p in (old/'after').rglob('*') if p.is_file()}
after=dict(before)
for key in ['bsd/kern/kebab_data.c','bsd/kern/kebab_data.h']:
    text=after.pop(key)
    text=text.replace('kebab_data','kebab_apfs_probe').replace('kebab_lookup_apfs_data','kebab_lookup_apfs_preboot')
    text=text.replace('kebab_apfs_probe_mount','kebab_apfs_probe')
    text=text.replace('/private/var','/private/preboot').replace('request.path, 13','request.path, 17')
    text=text.replace('4C56203C-CFE8-4DFF-BF8E-BB48D852C936','EF45F98E-9F6C-43DB-B7C6-F418CA77429B')
    text=text.replace('KEBAB DATA:','KEBAB APFS PROBE:').replace('APFS Data','APFS Preboot').replace('Data qualification','Preboot qualification')
    after[key.replace('kebab_data','kebab_apfs_probe')]=text
for key,text in list(after.items()):
    text=text.replace('kebab_data.c','kebab_apfs_probe.c').replace('kebab_data.h','kebab_apfs_probe.h')
    text=text.replace('kebab_data_prepare','kebab_apfs_probe_prepare').replace('kebab_data_error','kebab_apfs_probe_error')
    text=text.replace('kebab_lookup_apfs_data','kebab_lookup_apfs_preboot')
    text=text.replace('4C56203C-CFE8-4DFF-BF8E-BB48D852C936','EF45F98E-9F6C-43DB-B7C6-F418CA77429B')
    text=text.replace('kebab Data mount test failed','kebab APFS probe failed')
    after[key]=text
for name,sources in [('before',before),('after',after)]:
    for key,text in sources.items():
        p=transition/name/key;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,newline='\n')
for p in (old/'before').rglob('*'):
    if p.is_file():
        dest=d/'before'/p.relative_to(old/'before');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
for key,text in after.items():
    p=d/'after'/key;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,newline='\n')
chunks=[]
for key in sorted(set(before)|set(after)):
    chunks.extend(difflib.unified_diff(before.get(key,'').splitlines(keepends=True),after.get(key,'').splitlines(keepends=True),
        fromfile='a/'+key if key in before else '/dev/null',tofile='b/'+key if key in after else '/dev/null',n=3))
(transition/'switch-to-preboot.patch').write_text(''.join(chunks),newline='\n')
print('Prepared Preboot-only probe; Data guard and volume contents stay unchanged')
