from pathlib import Path
import difflib
import sys

root=Path(__file__).parent
chunks=[]
for after in sorted((root/'after').rglob('*')):
    if not after.is_file():continue
    relative=after.relative_to(root/'after').as_posix()
    before=root/'before'/relative
    old=before.read_text().splitlines(keepends=True) if before.exists() else []
    new=after.read_text().splitlines(keepends=True)
    chunks.extend(difflib.unified_diff(old,new,
        fromfile='a/'+relative if before.exists() else '/dev/null',tofile='b/'+relative,n=3))
Path(sys.argv[1]).write_text(''.join(chunks),newline='\n')
print('patch written:',sys.argv[1])
