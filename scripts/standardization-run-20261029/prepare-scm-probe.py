"""Create a default-off fixed capability survey patch; no arbitrary SMC API."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'scm-probe'
names=['bsd/kern/bsd_init.c','bsd/conf/files']
if not (d/'before').exists():
    got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(names),text=True,capture_output=True,check=True).stdout)
    for n in names:
        p=d/'before'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(got[n]['source'],newline='\n')
changes={n:(d/'before'/n).read_text() for n in names}
needle='\t\tkebab_keys_publish();\n\t}\n'
assert changes[names[0]].count(needle)==1
changes[names[0]]=changes[names[0]].replace(needle,needle+'\t/* Default-off, fixed register-only secure firmware capability queries. */\n\t{\n\t\textern void kebab_scm_probe(void);\n\t\tkebab_scm_probe();\n\t}\n')
needle='bsd/kern/kebab_apfs_probe.c\t\t\tstandard\n'
assert changes[names[1]].count(needle)==1
changes[names[1]]=changes[names[1]].replace(needle,needle+'bsd/kern/kebab_scm_probe.c\t\t\tstandard\n')
changes['bsd/kern/kebab_scm_probe.c']=(d/'kebab_scm_probe.c').read_text()
patch=[]
for n,after in changes.items():
    before=(d/'before'/n).read_text() if (d/'before'/n).exists() else ''
    p=d/'after'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
    patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+n if before else '/dev/null',tofile='b/'+n))
(d/'0195-kebab-scm-capability-probe.patch').write_text(''.join(patch),newline='\n')
print('Prepared default-off capability probe')
