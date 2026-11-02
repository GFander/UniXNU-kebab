"""Parse a bounded original-process ring; report real failures, never suppress."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parent;raw=(r/'0477-media-boot.log').read_bytes();s=raw.decode();provenance=r/'0477-capture-provenance.json'
begin=list(re.finditer(r'KEBAB FP-BEGIN: pid=(\d+) total=(\d+) kept=(\d+) first=(\d+) deny-seen=([01])\n',s));end=list(re.finditer(r'KEBAB FP-END: pid=(\d+) signal=(\d+) status=(\d+); original exit preserved\n',s));assert len(begin)==len(end)==1
b,e=begin[0],end[0];assert b.end()<e.start() and b[1]==e[1];pid,total,kept,first,seen=map(int,b.groups());assert kept==min(32,total) and first==total-kept+1
pattern=r'KEBAB FP-(SC|DENY): n=(\d+) pid=(\d+) tid=(\d+) call=(\d+) error=(-?\d+) pc=([0-9a-f]+) lr=([0-9a-f]+) req=(-?\d+) flags=([0-9a-f]+) dbg=([01])\n'
rows=[];denies=[]
for z in re.finditer(pattern,s[b.end():e.start()]):
 row=dict(sequence=int(z[2]),pid=int(z[3]),tid=int(z[4]),syscall=int(z[5]),error=int(z[6]),pc=int(z[7],16),lr=int(z[8],16),request=int(z[9]),flags=int(z[10],16),debugger=bool(int(z[11])))
 assert row['pid']==pid and row['tid']>0 and row['syscall']<=65535 and row['flags']&~3==0
 assert row['syscall']==26 or row['request']==-1
 (rows if z[1]=='SC' else denies).append(row)
assert len(rows)==kept and len(denies)==seen and len(rows)==s.count('KEBAB FP-SC:') and len(denies)==s.count('KEBAB FP-DENY:')
assert [row['sequence'] for row in rows]==list(range(first,total+1))
if denies:
 deny=denies[0];assert deny['syscall']==26 and deny['request']==31 and 0<deny['sequence']<=total
 if deny['sequence']>=first:assert deny==rows[deny['sequence']-first]
names={}
# This is the pinned production syscall table, not Linux errno/syscall names.
source=r/'fairplay-syscalls-0477/syscalls.master'
if source.exists():
 for line in source.read_text().splitlines():
  z=re.match(r'\s*(\d+)\s+\S+\s+\S+\s+\{\s*\S+\s+([A-Za-z0-9_]+)\s*\(',line)
  # Conditional fallback entries must not overwrite a named implementation.
  # This annotates the source table, not a proof of the compiled branch.
  if z and (int(z[1]) not in names or z[2] not in ('enosys','nosys')):names[int(z[1])]=z[2]
for row in rows+denies:row['syscall_name']=names.get(row['syscall'],'unknown')
identity=json.loads((r/'fairplay-syscalls-0477/original-identity.json').read_text())
q=dict(version='0477',actual_log_sha256=hashlib.sha256(raw).hexdigest(),capture_closed=provenance.exists(),selected_pid=pid,identity_filter=identity,total_completed_BSD_calls=total,retained_calls=kept,first_retained_sequence=first,first_deny_attach=denies[0] if denies else None,rows=rows,exit_signal=int(e[2]),exit_status=int(e[3]),argument_or_result_buffers_logged=False,syscall_return_values_changed=False,original_ptrace_security_checks_preserved=True,exit_cause_proven=False,migration_complete=False,system_shell_checkin_complete=False,usable_springboard=False)
if provenance.exists():assert json.loads(provenance.read_text())['sha256']==q['actual_log_sha256']
(r/'0477-fairplay-syscalls-observations.json').write_text(json.dumps(q,indent=2)+'\n',newline='\n')
print('Original FairPlay PID',pid,'total',total,'kept',kept,'first deny',q['first_deny_attach'],'exit',q['exit_signal'],q['exit_status'],'closed',q['capture_closed'])
for row in rows:print(row)
