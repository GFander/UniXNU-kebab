"""Reproduce shadowed imports, then prove positive and negative validation outcomes."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).parent
with tempfile.TemporaryDirectory() as t:
    p=Path(t);core=p/'core';core.mkdir()
    (core/'collections.py').write_text('raise RuntimeError("wrong collections import")\n')
    checker=core/'syntax_checker.py'
    checker.write_text((r/'host-validator/before/tools/lldbmacros/core/syntax_checker.py').read_text())
    good=p/'good.py';good.write_text('assert False # compile only; never execute this source\n')
    bad=subprocess.run([sys.executable,'-S',str(checker),str(good)],text=True,capture_output=True)
    assert bad.returncode and ('wrong collections import' in bad.stderr or 'relative import' in bad.stderr)
    checker.write_text((Path(sys.argv[1]) if len(sys.argv)>1 else r/'host-validator/after/tools/lldbmacros/core/syntax_checker.py').read_text())
    cases=[('good.py','assert False\n',0),('empty.py','',0),('tab.py','\tpass\n',1),
      ('syntax.py','if :\n',1),('nonascii-first.py','# café\npass\n',1),('nonascii-last.py','pass\n# café\n',1),
      ('skip.txt','not a python source',0)]
    for name,text,rc in cases:
        target=p/name;target.write_text(text,encoding='utf-8')
        result=subprocess.run([sys.executable,'-S',str(checker),str(target)],text=True,capture_output=True)
        assert result.returncode==rc,(name,result.stdout,result.stderr)
    result=subprocess.run([sys.executable,'-S',str(checker),str(p/'missing.py')],capture_output=True)
    assert result.returncode==1
print('PASS: shadowing reproduced; 8 validator cases, including real syntax/tab/non-ASCII rejection and empty files')
