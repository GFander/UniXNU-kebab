from pathlib import Path
import json,re,hashlib,sys,collections
r=Path(__file__).resolve().parent;v=sys.argv[1];p=Path(sys.argv[2]) if len(sys.argv)>2 else r/(v+'-media-boot.log')
raw=p.read_bytes();s=raw.decode(errors='replace')
crashes=collections.Counter(re.findall(r'Process\[\d+\] crashed: ([^.\n]+(?:\.H2)?)[.] Too many corpses',s))
exits=re.findall(r'KEBAB RUNTIME EXIT: ([^\n]+)',s)
q=dict(version=v,path=str(p),sha256=hashlib.sha256(raw).hexdigest(),crash_counts=dict(crashes.most_common()),total_corpse_crash_messages=sum(crashes.values()),disk3_busy=len(re.findall(r'disk3: device is busy',s)),storage_failed_to_start=s.count('executeRequest: request failed to start!'),pagein_errors=len(re.findall(r'decmpfs_pagein_compressed: [^\n]*err(?:or)? (?:16|94)',s)),missing_hash_record=s.count('Missing hash record')+s.count('Cannot find data hash record'),runtime_first_exits=exits,queue_observations=re.findall(r'KEBAB CRYPTEX QUEUE: ([^\n]+)',s),unsupported_aks_counts=dict(collections.Counter(re.findall(r'KEBAB AKS UC: selector=(\d+)[^\n]*result=e00002c7',s))),user_brks=re.findall(r'KEBAB USER BRK: ([^\n]+)',s),panic=re.findall(r'panic\(cpu[^\n]+',s))
(r/(v+'-runtime-crashes.json')).write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
