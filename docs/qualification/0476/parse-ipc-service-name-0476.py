"""Parse bounded paired metadata/owner observations without inventing RPCs."""
from pathlib import Path
import hashlib,json,re,sys
r=Path(__file__).resolve().parent;v='0476';raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode()
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

state_pattern=r'KEBAB (\S+) IPC-STATE: tid=(\d+) hop=(\d+) obj=([0-9a-f]+) kind=(\d+) pid=(-?\d+) recv=([0-9a-f]+) dst=([0-9a-f]+) f=([0-9a-f]+) link=(\d+) kt=(\d+) q=(\d+)/(\d+)\n'
owners=list(re.finditer(owner,s));assert len(owners)==len(rows)
state_count=0
for i,(m,row) in enumerate(zip(owners,rows)):
 end=owners[i+1].start() if i+1<len(owners) else len(s);path=[]
 for z in re.finditer(state_pattern,s[m.end():end]):
  assert z[1]==row['label'] and int(z[2])==row['tid']
  node=dict(hop=int(z[3]),object=int(z[4],16),kind=int(z[5]),receiver_pid=int(z[6]),receiver_name=int(z[7],16),destination=int(z[8],16),flags=int(z[9],16),sync_link_state=int(z[10]),kobject_type=int(z[11]),queued=int(z[12]),queue_limit=int(z[13]))
  assert node['hop']==len(path) and 0<=node['kind']<=5 and node['object']>0
  assert node['object'] not in [p['object'] for p in path]
  if path:assert path[-1]['kind']==4 and path[-1]['destination']==node['object']
  if node['kind']==4:assert node['destination']>0
  else:assert node['destination']==0
  path.append(node)
 assert len(path)<=8
 if row['lookup'] and row['status']==0:
  assert path and path[0]['object']==row['object'] and path[0]['receiver_pid']==row['receiver_pid']
  if row['receiver_pid']>0:assert path[0]['receiver_name']==row['receiver_name']
 else:assert not path
 row['destination_path']=path;row['atomic_destination_path_proven']=False;state_count+=len(path)
assert state_count==s.count(' IPC-STATE:'),'Malformed/truncated state records'

service_pattern=r'KEBAB (\S+) IPC-SERVICE: tid=(\d+) hop=(\d+) name=(\S+) info=([01]) dom=(\d+) ld=([0-9a-f]+) flags=([0-9a-f]+) trunc=([01]) changed=([01])\n'
service_count=0
for i,(m,row) in enumerate(zip(owners,rows)):
 end=owners[i+1].start() if i+1<len(owners) else len(s);path=row['destination_path']
 for z in re.finditer(service_pattern,s[m.end():end]):
  assert z[1]==row['label'] and int(z[2])==row['tid']
  hop=int(z[3]);assert hop<len(path) and path[hop]['kind']!=0 and path[hop]['flags']&2 and 'service' not in path[hop]
  name=z[4];assert len(name)<=63 and all(0x21<=ord(c)<=0x7e for c in name)
  node=dict(name=name,info_available=bool(int(z[5])),domain=int(z[6]),launchd_name=int(z[7],16),label_flags=int(z[8],16),truncated=bool(int(z[9])),changed=bool(int(z[10])))
  if not node['info_available']:assert name=='-' and not node['domain'] and not node['truncated'] and not node['changed']
  path[hop]['service']=node;service_count+=1
assert service_count==s.count(' IPC-SERVICE:'),'Malformed/truncated service records'
q['original_service_label_records']=service_count
q['observed_service_names']=sorted({p['service']['name'] for row in rows for p in row['destination_path'] if 'service' in p})

q.update(port_state_records=state_count,in_transit_paths=sum(any(p['kind']==4 for p in row['destination_path']) for row in rows),unowned_head_kinds=sorted({row['destination_path'][0]['kind'] for row in rows if row['receiver_pid']<0 and row['destination_path']}),atomic_destination_path_proven=False)
(r/(v+'-IPC-observations.json')).write_text(json.dumps(q,indent=2)+'\n',newline='\n')
print('Observed',len(rows),'complete metadata/owner pairs;',len(sync),'native sync contexts; capture closed',q['capture_closed'])
for row in positive:print(row['label'],'tid',row['tid'],'source',row['source_pid'],'role',row['role'],'port',hex(row['remote']),'receiver',row['receiver_pid'],'id',row['message_id'] if row['original_request_message_id_known'] else 'unknown (receive-only sync)')

print('Port states',state_count,'transit paths',q['in_transit_paths'],'unowned kinds',q['unowned_head_kinds'])
for row in rows:
 if row['destination_path']:print(row['label'],row['source_pid'],row['tid'],'path',[(p['kind'],p['receiver_pid'],hex(p['object']),hex(p['destination']),p['queued'],p['queue_limit']) for p in row['destination_path']])

print('Original service-label records',service_count,'names',q['observed_service_names'])
for row in rows:
 for node in row['destination_path']:
  if 'service' in node:print(row['label'],row['source_pid'],row['tid'],'hop',node['hop'],'receiver',node['receiver_pid'],'queue',node['queued'],node['queue_limit'],'service',node['service'])
