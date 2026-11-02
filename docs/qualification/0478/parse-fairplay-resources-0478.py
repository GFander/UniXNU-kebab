"""Join bounded request-name observations; no causal inference or data values."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parent;raw=(r/'0478-media-boot.log').read_bytes();s=raw.decode();rows={};names={}
for m in re.finditer(r'KEBAB FP-RESOURCE: id=(\d+) pid=(\d+) kind=(\d+) code=(\d+) result=(-?\d+) aux=(-?\d+) copy=(-?\d+) trunc=([01]) changed=([01])\n',s):
 values=list(map(int,m.groups()));ticket,pid,kind,code,result,aux,copy,truncated,changed=values
 assert 1<=ticket<=32 and ticket not in rows and kind in (0,1,2)
 rows[ticket]=dict(ticket=ticket,pid=pid,kind=kind,code=code,result=result,aux=aux,copy_error=copy,truncated=bool(truncated),sanitized=bool(changed))
for m in re.finditer(r'KEBAB FP-NAME: id=(\d+) name=([^\n]*)\n',s):
 ticket=int(m[1]);assert ticket not in names and len(m[2])<128 and re.fullmatch(r'[A-Za-z0-9/._?:-]*',m[2]);names[ticket]=m[2]
assert rows and rows.keys()==names.keys() and len(rows)==s.count('KEBAB FP-RESOURCE:')==s.count('KEBAB FP-NAME:')
assert sorted(rows)==list(range(1,len(rows)+1))
for ticket,row in rows.items():
 row['name']=names[ticket]
 if row['kind']==0:
  assert row['code'] in (5,463) and row['result']==2
  if row['copy_error']:assert row['name']==''
 else:assert row['code']==0 and row['aux'] in (0,1)
q=dict(version='0478',actual_log_sha256=hashlib.sha256(raw).hexdigest(),capture_closed=(r/'0478-capture-provenance.json').exists(),rows=list(rows.values()),budget_reached=len(rows)==32,user_paths_post_return_best_effort=True,registry_names_from_live_MIG_request=True,file_or_property_values_observed=False,exit_cause_proven=False,migration_complete=False,system_shell_checkin_complete=False,usable_springboard=False)
if q['capture_closed']:assert json.loads((r/'0478-capture-provenance.json').read_text())['sha256']==q['actual_log_sha256']
(r/'0478-fairplay-resources-observations.json').write_text(json.dumps(q,indent=2)+'\n',newline='\n')
for row in q['rows']:print(row)
print('Observed',len(rows),'bounded names; closed',q['capture_closed'],'cause unproven')
