#!/usr/bin/env python3
"""Compile each board's actual catalogue string and inspect CPU matching."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cpu-personality-', dir=cache) as folder:
    path = Path(folder) / 'catalogue.cpp'
    binary = Path(folder) / 'catalogue'
    path.write_text('#include <stdio.h>\n' + source +
                    '\nint main() { puts(gIOKernelConfigTables); }\n')
    for board in ('KEBAB', 'ARM64_BOARD_CONFIG_NEGRONI', None):
        defines = ['-D' + board] if board else []
        subprocess.run(['clang++', '-Wall', '-Wextra', '-Werror', *defines,
                        str(path), '-o', str(binary)], check=True)
        catalogue = subprocess.check_output([str(binary)], text=True)
        entries = [entry for entry in re.findall(r'\{([^{}]*)\}', catalogue)
                   if re.search(r"'IOClass'\s*=\s*KebabCPU;", entry)]
        if board == 'KEBAB':
            assert len(entries) == 1, 'KEBAB must publish exactly one native CPU personality'
            entry = entries[0]
            assert re.search(r"'IOProviderClass'\s*=\s*IOPlatformDevice;", entry)
            assert re.search(r"'IONameMatch'\s*=\s*cpu;", entry)
            assert int(re.search(r"'IOProbeScore'\s*=\s*(\d+):32;", entry)[1]) > 100
        else:
            assert not entries, 'KebabCPU must be scoped to its board'
print('compiled KEBAB/NEGRONI/default catalogues: CPU provider, match, score, scope PASS')
