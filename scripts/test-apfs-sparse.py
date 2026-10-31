#!/usr/bin/env python3
"""Check exact sparse expansion, real holes, and corruption refusals."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location('exporter', Path(__file__).with_name('export-apfs-sparse.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='apfs-sparse-') as temp:
    folder = Path(temp)
    source, target = folder/'source.raw', folder/'target.img'
    with source.open('wb') as f:
        f.write(b'A'*4096)
        f.seek(1 << 20)
        f.write(b'B'*4096)
        f.truncate(2 << 20)
    with contextlib.redirect_stdout(io.StringIO()):
        module.export(source, target)
    expanded = bytearray()
    with target.open('rb') as f:
        _, _, _, _, _, block, total, chunks, _ = struct.unpack('<IHHHHIIII', f.read(28))
        for _ in range(chunks):
            kind, _, count, size = struct.unpack('<HHII', f.read(12))
            expanded += f.read(count*block) if kind == module.RAW else f.read(4)*(count*block//4)
    assert bytes(expanded) == source.read_bytes() and len(expanded) == total*block
    assert target.stat().st_size < 65536, 'The real host hole was not exported sparsely'
    with source.open('rb') as f:
        ranges = list(module.extents(f.fileno(), source.stat().st_size))
    pristine = target.read_bytes()
    damaged = bytearray(pristine)
    damaged[40] ^= 1  # First RAW byte.
    target.write_bytes(damaged)
    try:
        module.verify(source, target, ranges)
        raise RuntimeError('Corrupt RAW payload accepted')
    except AssertionError:
        pass
    target.write_bytes(pristine[:-1])
    try:
        module.verify(source, target, ranges)
        raise RuntimeError('Truncated final FILL accepted')
    except (AssertionError, struct.error):
        pass
print('Real holes, exact expansion, RAW corruption and truncation refusals: PASS')
