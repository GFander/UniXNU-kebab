"""Fix stdlib shadowing without suppressing LLDB source validation."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'series-repair/host-validator';name='tools/lldbmacros/core/syntax_checker.py'
p=d/'before'/name
if not p.exists():
    got=json.loads(subprocess.run(['ssh','albertlee@192.168.30.165','python3 source-reader.py /Users/albertlee/kebab-build/dxb/xnu'],input=json.dumps([name]),text=True,capture_output=True,check=True).stdout)
    p.parent.mkdir(parents=True,exist_ok=True);p.write_text(got[name]['source'],newline='\n')
before=p.read_text();needle='import os\nimport re\n';assert before.count(needle)==1
after=before.replace('#!/usr/bin/env python\n','#!/usr/bin/env python3\n',1).replace(needle,'''import os
# The adjacent core/collections.py is an LLDB package module, not stdlib.
_script_directory = os.path.dirname(os.path.realpath(__file__))
sys.path = [entry for entry in sys.path if os.path.realpath(entry) != _script_directory]
import re
''')
after=after.replace('        strdata = fh.readlines()\n','        strdata = fh.readlines()\n        fh.close()\n')
needle='''        if find_non_ascii(linedata):
            print("Error: Found a non ascii character at %s:%d" % (fname, lineno), file=sys.stderr)
            syntax_fail = True
''';assert after.count(needle)==1
after=after.replace(needle,''.join('    '+line if line.strip() else line for line in needle.splitlines(True)))
p=d/'after'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
(d/'0003-python-validator-imports.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)),newline='\n')
print('Preserved validator; fixed module shadowing, empty files and all-line ASCII checks')
