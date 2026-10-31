#!/usr/bin/env python3
"""Compile actual board catalogues; require Surface/CPU only on intended boards."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
cache = Path.home()/'.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='driver-personalities-',dir=cache) as temp:
    cpp,binary = Path(temp)/'test.cpp',Path(temp)/'test'
    cpp.write_text('#include <stdio.h>\n'+source+'\nint main(){puts(gIOKernelConfigTables);}\n')
    for board in ('KEBAB','ARM64_BOARD_CONFIG_NEGRONI',None):
        subprocess.run(['clang++','-Wall','-Wextra','-Werror',*(['-D'+board] if board else []),
                        str(cpp),'-o',str(binary)],check=True)
        catalogue = subprocess.check_output([str(binary)],text=True)
        entries = re.findall(r'\{([^{}]*)\}',catalogue)
        surface = [e for e in entries if re.search(r"'IOClass'\s*=\s*IOCoreSurfaceRoot;",e)]
        cpu = [e for e in entries if re.search(r"'IOClass'\s*=\s*KebabCPU;",e)]
        assert len(surface)==(1 if board else 0)
        assert len(cpu)==(1 if board=='KEBAB' else 0)
        if board=='KEBAB':
            assert re.search(r"'IOResourceMatch'\s*=\s*IOBSD;",surface[0])
            assert re.search(r"'IOProviderClass'\s*=\s*IOResources;",surface[0])
            assert re.search(r"'IOProviderClass'\s*=\s*IOPlatformDevice;",cpu[0])
            assert 'AppleARMCPU' not in catalogue
print('KEBAB CPU + Surface/IOBSD matching, preserved NEGRONI and default exclusion: PASS')
