"""Read-only inspection of the pinned 20H392 bluetoothd; never patches it."""
from pathlib import Path
import argparse, hashlib, json, struct, sys
import capstone

r = Path(__file__).resolve().parent
sys.path.insert(0, str(r.parent / 'p1-run/prelink-fix'))
from native_prelink import macho

p = argparse.ArgumentParser()
p.add_argument('--start', type=lambda s: int(s, 0), default=0x1000266bc)
p.add_argument('--end', type=lambda s: int(s, 0), default=0x100027554)
p.add_argument('--all', action='store_true')
a = p.parse_args()
assert 0 < a.end-a.start <= 0x5000 and not (a.start | a.end) & 3
d = (r/'original-runtime-tools/bluetoothd').read_bytes()
sha = hashlib.sha256(d).hexdigest()
assert sha == '8b7c0a0bea51544db82269d7f2775ab7996b8740927283428c4753e032df3a8f'
m = macho(d)
def read(va, n):
    s = next(s for s in m['segments'] if s['va'] <= va and va+n <= s['va']+s['filesize'])
    o = s['fileoff']+va-s['va']
    return d[o:o+n]
names, indirect, stubs = [], [], {}
for cmd, o, n in m['commands']:
    if cmd == 2:
        no, nc, so, ss = struct.unpack_from('<4I', d, o+8)
        t = d[so:so+ss]
        for j in range(nc):
            x = struct.unpack_from('<I', d, no+j*16)[0]
            names.append(t[x:t.find(b'\0', x)].decode(errors='replace'))
    if cmd == 0xb:
        io, ic = struct.unpack_from('<II', d, o+56)
        indirect = struct.unpack_from('<'+'I'*ic, d, io)
for seg in m['segments']:
    for s in seg['sections']:
        if s['name'] == '__stubs':
            first, step = struct.unpack_from('<II', d, s['command_offset']+68)
            for j in range(s['size']//step):
                z = indirect[first+j]
                if z < len(names): stubs[s['va']+step*j] = names[z]
def string(va):
    try:
        s = next(s for s in m['segments'] if s['va'] <= va < s['va']+s['filesize'])
        raw = read(va, min(1024, s['va']+s['filesize']-va)).split(b'\0')[0]
        return raw.decode() if raw and all(b in (9,10,13) or 32 <= b < 127 for b in raw) else ''
    except (StopIteration, UnicodeError): return ''
c = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
c.detail = True
pages, rows = {}, []
for i in c.disasm(read(a.start, a.end-a.start), a.start):
    note = ''
    if i.mnemonic in ('bl', 'b') and i.operands[0].type == capstone.arm64.ARM64_OP_IMM:
        note = stubs.get(i.operands[0].imm, '')
    if i.mnemonic == 'adrp': pages[i.reg_name(i.operands[0].reg)] = i.operands[1].imm
    if i.mnemonic == 'adr': note = string(i.operands[1].imm)
    if i.mnemonic == 'add' and len(i.operands) == 3 and i.operands[2].type == capstone.arm64.ARM64_OP_IMM:
        src = i.reg_name(i.operands[1].reg)
        if src in pages: note = string(pages[src]+i.operands[2].imm)
    row = dict(pc=hex(i.address), instruction=i.mnemonic+' '+i.op_str, annotation=note)
    rows.append(row)
    if a.all or note: print(row)
out = dict(original_sha256=sha, read_only=True, start=hex(a.start), end=hex(a.end), rows=rows)
(r/f'bluetooth-original-static-{a.start:x}-{a.end:x}.json').write_text(json.dumps(out, indent=2)+'\n')
