"""Read a bounded explicit list of project source files; no Git or discovery."""
from pathlib import Path
import hashlib
import json
import sys

root=Path(sys.argv[1]).resolve();paths=json.load(sys.stdin)
assert len(paths)<=100
records={}
for path in paths:
    p=(root/path).resolve();assert root in p.parents and p.is_file()
    raw=p.read_bytes();assert len(raw)<2_000_000
    records[path]=dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),
        lines=len(raw.splitlines()),source=raw.decode('utf-8'))
json.dump(records,sys.stdout)
