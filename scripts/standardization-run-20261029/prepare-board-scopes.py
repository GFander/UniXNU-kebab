"""Keep board-specific pinned prelink bindings out of other ARM64 builds."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent; d=r/'board-scopes'
names=['iokit/Kernel/arm/'+n+'.cpp' for n in ('KebabBlockStorageDevice','KebabPrelinkBindings','NativeAES','OriginalAESBindings')]
if not (d/'before').exists():
    got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(names),text=True,capture_output=True,check=True).stdout)
    for name in names:
        p=d/'before'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(got[name]['source'],newline='\n')
patch=[]
for name in names:
    before=(d/'before'/name).read_text()
    after='#include <pexpert/arm64/board_config.h>\n#if defined(KEBAB)\n'+before+'\n#endif /* KEBAB */\n'
    p=d/'after'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
    assert after.split('\n',2)[2].removesuffix('\n#endif /* KEBAB */\n')==before
    patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(d/'0194-kebab-prelink-board-scopes.patch').write_text(''.join(patch),newline='\n')
print('Prepared four board guards; inner source bodies byte-identical')
