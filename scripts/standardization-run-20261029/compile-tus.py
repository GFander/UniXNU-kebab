"""Compile existing/new C or C++ units against the exact recorded kernel ABI."""
from pathlib import Path
import json
import subprocess
import sys

r=Path('/Users/albertlee/kebab-build')
objects=r/'dxb/build/xnu.obj/DEVELOPMENT_ARM64_KEBAB'
out=r/'driver-standardization';out.mkdir(exist_ok=True)
for arg in sys.argv[1:]:
    name,_,template=arg.partition(':');template=template or name
    records=list(objects.rglob(template+'.o.json'))+list(objects.rglob(template+'.cpo.json'))
    assert len(records)==1,(template,records)
    record=json.loads(records[0].read_text().strip().rstrip(','))
    extension='.c' if records[0].name.endswith('.o.json') else '.cpp'
    args=record['arguments'];command=[];i=0
    while i<len(args):
        value=args[i]
        if value in ('-MF','-MJ'):
            i+=2;continue
        if value in ('-MD','-MP'):
            i+=1;continue
        if value=='-o':
            command.extend(['-o',str(out/(name+'.o'))]);i+=2;continue
        command.append(value.replace(template+extension,name+extension));i+=1
    run=subprocess.run(command,cwd=record['directory'],text=True,capture_output=True)
    (out/(name+'.log')).write_text(run.stdout+run.stderr)
    print(name,'PASS' if run.returncode==0 else 'FAIL',flush=True)
    if run.returncode:
        print(run.stdout+run.stderr,flush=True);raise SystemExit(run.returncode)
