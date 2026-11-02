"""Parse bounded paired metadata/owner observations without inventing RPCs."""
from pathlib import Path
import hashlib,json,re,sys
r=Path(__file__).resolve().parent;v='0474';raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode()
meta=r'KEBAB (\S+) IPC: tid=(\d+) opt=([0-9a-f]+) b=([0-9a-f]+) sz=(\d+) v=([01]) r=([0-9a-f]+) l=([0-9a-f]+) q=([0-9a-f]+) id=(-?\d+) lookup=([01])\n'
owner=r'KEBAB (\S+) IPC-OWNER: tid=(\d+) role=(send|sync|none) name=([0-9a-f]+) kr=([0-9a-f]+) pid=(-?\d+) recv=([0-9a-f]+) object=([0-9a-f]+)\n'
header=r'KEBAB (MIG|MIG-[WIL]|UI-SB|UI-PB|BBOARD): [^\n]*\bpid=(\d+)[^\n]*\n'
events=[]
for kind,pattern in [('meta',meta),('owner',owner),('header',header)]:
 for m in re.finditer(pattern,s):events.append((m.start(),kind,m))
sources={};pending={};rows=[]
for _,kind,m in sorted(events,key=lambda item:item[0]):
 if kind=='header':sources[m[1]]=int(m[2]);continue
 key=(m[1],int(m[2]))
 if kind=='meta':
  assert key not in pending,('Missing prior owner line',key)
  assert key[0] in sources,('No actual fixed task PID',key)
  pending[key]=dict(label=key[0],tid=key[1],source_pid=sources[key[0]],options=int(m[3],16),bits=int(m[4],16),send_size_or_count=int(m[5]),vector=bool(int(m[6])),remote=int(m[7],16),local=int(m[8],16),receive=int(m[9],16),message_id=int(m[10]),lookup=bool(int(m[11])))
 else:
  assert key in pending,('Owner lacks metadata',key)
  row=pending.pop(key);row.update(role=m[3],name=int(m[4],16),status=int(m[5],16),receiver_pid=int(m[6]),receiver_name=int(m[7],16),object=int(m[8],16))
  assert row['name']==row['remote']
  expected='send' if row['options']&1 else ('sync' if row['options']&2 and row['options']&0x4000 else 'none')
  assert row['role']==expected
  assert row['lookup']==(expected!='none' and row['remote'] not in (0,0xffffffff))
  row['original_request_message_id_known']=expected=='send'
  row['atomic_request_reply_pairing_proven']=False;rows.append(row)
assert not pending,('Incomplete metadata/owner pair',list(pending))
assert len(rows)==s.count(' IPC:')==s.count(' IPC-OWNER:'),'Truncated or malformed diagnostic line'
positive=[row for row in rows if row['lookup'] and row['status']==0 and row['receiver_pid']>0]
sync=[row for row in positive if row['role']=='sync']
q=dict(version=v,actual_log_sha256=hashlib.sha256(raw).hexdigest(),capture_closed=(r/(v+'-capture-provenance.json')).exists(),rows=rows,successful_receivers=len(positive),successful_sync_contexts=len(sync),positive_sync_labels=sorted({row['label'] for row in sync}),original_message_payload_read=False,atomic_transaction_pairing_proven=False,dependency_cycle_proven=False,migration_complete=False,system_shell_checkin_complete=False)
(r/(v+'-IPC-observations.json')).write_text(json.dumps(q,indent=2)+'\n',newline='\n')
print('Observed',len(rows),'complete metadata/owner pairs;',len(sync),'native sync contexts; capture closed',q['capture_closed'])
for row in positive:print(row['label'],'tid',row['tid'],'source',row['source_pid'],'role',row['role'],'port',hex(row['remote']),'receiver',row['receiver_pid'],'id',row['message_id'] if row['original_request_message_id_known'] else 'unknown (receive-only sync)')
