"""Qualify real read-only firmware bytes; never claim radio functionality."""
from pathlib import Path
import argparse, hashlib, json, re

p = argparse.ArgumentParser()
p.add_argument('version')
p.add_argument('--source')
a = p.parse_args()
r = Path(__file__).resolve().parent
v = a.version
assert len(v) == 4 and v.isdigit()
source = a.source or v
raw = (r / (v + '-media-boot.log')).read_bytes()
digest = hashlib.sha256(raw).hexdigest()
assert json.loads((r / (v + '-capture-provenance.json')).read_text())['sha256'] == digest
s = raw.decode()
assert not re.search(r'KEBAB EARLY PANIC|panic\(cpu|KEBAB PANIC: USB command', s)
flash = json.loads((r / (v + '-flash.json')).read_text())
native = r.parent / 'p1-run/prelink-fix' / ('kernel-' + v + '.kebab')
assert hashlib.sha256(native.read_bytes()).hexdigest() == flash['kernel_sha256']
replay = json.loads((r / ('all-source-replay-' + source + '.json')).read_text())
assert replay['all_source_equal']
assert '****** DONE MKB_INIT ****' in s
probes = []
for block in s.split('KEBAB BTFW: LU4 bluetooth_b READONLY ')[1:]:
    assert block.startswith('1048576 bytes;')
    end = re.search(r'KEBAB BTFW: loader error=(\d+);', block)
    assert end and end[1] == '0', 'incomplete or failed physical probe'
    block = block[:end.end()]
    assert 'logical-bytes=67108864 resident-bytes=1048576' in block
    files = {}
    for name, size, sha in re.findall(r'KEBAB BTFW: image/(\S+) bytes=(\d+) sha256=([0-9a-f]{64}) API-repeat=PASS;', block):
        assert name not in files and 0 < int(size) <= 1048576
        files[name] = dict(bytes=int(size), sha256=sha)
    assert {'htbtfw20.tlv', 'htnv20.bin'} <= files.keys()
    assert not re.search(r'image/(?:htbtfw20\.tlv|htnv20\.bin) error=', block)
    probes.append(files)
assert len(probes) >= 2, 'two independent on-device reads required'
assert all(x == probes[0] for x in probes)
q = dict(version=v, native_sha256=flash['kernel_sha256'], source_version=source,
         actual_capture_sha256=digest, probes=len(probes), files=probes[0],
         fresh_gpt_fixed_partition_read=True, read_only_firmware_loader_qualified=True,
         public_consumer_api_repeated_read=True, original_systembag_initialized=True,
         controller_download=False, hci_transport=False, bluetooth_functional=False,
         scan_connection_data_exchange_tested=False)
(r / (v + '-btfw-qualified.json')).write_text(json.dumps(q, indent=2) + '\n')
print(json.dumps(q, indent=2))
