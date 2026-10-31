"""Prove the board guards preserve KEBAB bodies and preprocess empty elsewhere."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).parent
with tempfile.TemporaryDirectory() as t:
    include=Path(t);h=include/'pexpert/arm64/board_config.h';h.parent.mkdir(parents=True);h.write_text('/* Another ARM64 board; KEBAB is undefined. */\n')
    files=list((r/'after').rglob('*.cpp'));assert len(files)==4
    for p in files:
        before=(r/'before'/p.relative_to(r/'after')).read_bytes()
        after=p.read_bytes();assert after.startswith(b'#include <pexpert/arm64/board_config.h>\n#if defined(KEBAB)\n')
        assert after.split(b'\n',2)[2].removesuffix(b'\n#endif /* KEBAB */\n')==before
        out=subprocess.run(['clang','-x','c++','-E','-P','-nostdinc','-UKEBAB','-I'+str(include),str(p)],text=True,capture_output=True,check=True).stdout
        assert not out.strip(),(str(p),out)
print('PASS: four KEBAB bodies byte-identical; other-board preprocessing emits no code or pinned symbols')
