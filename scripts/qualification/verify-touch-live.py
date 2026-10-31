"""Qualify captured real human touch through original HID, not GUI delivery.

Move lines are throttled diagnostic observations, not every controller packet.
No synthetic event generation or device writes occur in this verifier.
"""
from pathlib import Path
import argparse,hashlib,json,re
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--qualification',type=Path,default=Path(__file__).resolve().parent)
ap.add_argument('--source-manifest',type=Path)
ap.add_argument('--source',type=Path)
ap.add_argument('--output',type=Path)
a=ap.parse_args();q=a.qualification
sha=lambda b:hashlib.sha256(b).hexdigest()
raw=(q/'0459-touch-live.log').read_bytes();text=raw.decode();digest=sha(raw)
provenance=json.loads((q/'0459-touch-live-provenance.json').read_text());assert provenance['sha256']==digest and provenance['boot_version']=='0459'
actions=json.loads((q/'0459-touch-test-actions.json').read_text())
assert actions['stage1_user_ack']=='第一组已完成'and actions['stage2_user_ack']=='滑动已完成'
assert actions['stage3_user_ack']=='双指已完成'and actions['stage4_user_ack']=='保持已完成'
assert actions['user_returned_and_authorized_physical_sampling']and not actions['test_input_synthesized']
native=json.loads((q/'0459-flash.json').read_text())['kernel_sha256'];assert native==actions['native_sha256']=='026381abf4219889212f518227ec7dba3058019aa27c2aa33db8022c1631c8f2'
assert not any(s in text for s in ('panic(cpu','completion timeout','queue loss=','original report failed=',' (clamped)'))
events=[];reports=[];status=[];watchdog=[]
for lineno,line in enumerate(text.splitlines(),1):
 time=re.match(r'\[\s*([\d.]+)\]',line)
 if not time:continue
 seconds=float(time[1])
 m=re.search(r'KEBAB TOUCH: (down|move|up)\s+obj (\d+) raw (\d+) (\d+) panel (-?\d+) (-?\d+) logical (-?\d+) (-?\d+) z (\d+)',line)
 if m:
  phase=m[1];numbers=list(map(int,m.groups()[1:]));slot,x,y,px,py,lx,ly,z=numbers
  assert 0<=slot<10 and 0<=x<=1079 and 0<=y<=2399
  assert (px,py)==((x*1079+1079//2)//1079,(y*2399+2399//2)//2399)
  assert (lx,ly)==(px+22,py+18)and 0<=lx<1125 and 0<=ly<2436
  assert z==0 if phase=='up'else z>0
  events.append(dict(line=lineno,seconds=seconds,phase=phase,slot=slot,raw=[x,y],panel=[px,py],logical=[lx,ly],pressure=z))
 m=re.search(r'KEBAB HID: report role=2 count=(\d+) time=(\d+) keys=0 active=0x([0-9a-f]+) original-result=0',line)
 if m:reports.append(dict(line=lineno,seconds=seconds,count=int(m[1]),timestamp_ns=int(m[2]),active=int(m[3],16),original_result=0))
 m=re.search(r'KEBAB HID: role=2 ready=1 interval=(\d+) contacts=(\d+) backend-error=(\d+) produced=(\d+) dropped=(\d+) sent=(\d+) result=0x([0-9a-f]+)',line)
 if m:
  interval,contacts,error,produced,dropped,sent,result=[int(x,16)if i==6 else int(x)for i,x in enumerate(m.groups())]
  assert (interval,contacts,error,dropped,result)==(5000,10,0,0,0)and sent<=produced
  status.append(dict(line=lineno,seconds=seconds,produced=produced,sent=sent,dropped=dropped,result=result))
 m=re.search(r'KEBAB WATCHDOG: clients=1 polls=(\d+) beats=(\d+) enabled=1',line)
 if m:watchdog.append(dict(line=lineno,seconds=seconds,polls=int(m[1]),beats=int(m[2])))
assert len(status)>=4 and status[-1]['produced']==status[-1]['sent']
assert all(x['produced']<=y['produced']and x['sent']<=y['sent']for x,y in zip(status,status[1:]))
assert status[-1]['produced']>status[0]['produced']and status[-1]['seconds']>events[-1]['seconds']
assert text.count('actual observer completion')>=len(status)
assert watchdog and watchdog[-1]['seconds']>events[-1]['seconds']
active={};contacts=[];maximum=0;overlaps=[];pending=None
for e in events:
 slot=e['slot']
 if e['phase']=='down':
  assert slot not in active;active[slot]=[e]
  maximum=max(maximum,len(active))
  if len(active)==2:
   assert pending is None;pending=dict(start=e['seconds'],slots=sorted(active),first_release=None)
 else:
  assert slot in active;active[slot].append(e)
  if e['phase']=='up':
   contacts.append(active.pop(slot))
   if pending and pending['first_release']is None:
    pending.update(first_release=e['seconds'],released_first=slot,remaining=sorted(active))
   if pending and not active:
    pending.update(end=e['seconds'],released_last=slot);overlaps.append(pending);pending=None
assert not active and not pending and maximum==2 and len(overlaps)>=2
contacts.sort(key=lambda c:c[0]['seconds'])
first=[c[0]for c in contacts[:5]];assert len(first)==5
assert first[0]['raw'][0]<200 and first[0]['raw'][1]<350
assert first[1]['raw'][0]>850 and first[1]['raw'][1]<350
assert first[2]['raw'][0]<200 and first[2]['raw'][1]>2000
assert first[3]['raw'][0]>850 and first[3]['raw'][1]>2000
assert 350<first[4]['raw'][0]<750 and 900<first[4]['raw'][1]<1500
horizontal=[c for c in contacts if c[-1]['raw'][0]-c[0]['raw'][0]>800 and abs(c[-1]['raw'][1]-c[0]['raw'][1])<150 and c[-1]['seconds']-c[0]['seconds']>=2]
vertical=[c for c in contacts if c[-1]['raw'][1]-c[0]['raw'][1]>2000 and abs(c[-1]['raw'][0]-c[0]['raw'][0])<100 and c[-1]['seconds']-c[0]['seconds']>=2]
assert horizontal and vertical
assert all(len(o['remaining'])==1 and o['end']>o['first_release']and o['released_first']!=o['released_last']for o in overlaps)
dual=[p for p in reports if p['active']==3 and any(o['start']<=p['seconds']<=o['first_release']for o in overlaps)]
assert dual and any(o['first_release']-o['start']>=9.5 for o in overlaps)
assert all(x['count']<y['count']and x['timestamp_ns']<y['timestamp_ns']for x,y in zip(reports,reports[1:]))
source_pins={}
if a.source_manifest:
 manifest=json.loads(a.source_manifest.read_text())['files']
 for n in ('bsd/dev/kebab_touch.c','bsd/dev/kebab_touch_tcm.c','bsd/dev/kebab_touch_tcm.h','bsd/dev/kebab_hid_reports.h','iokit/Kernel/arm/KebabHIDInput.cpp'):
  source_pins[n]=manifest[n]
  if a.source:assert sha((a.source/n).read_bytes())==manifest[n]
out=dict(accepted=True,boot_version='0459',native_sha256=native,actual_log_sha256=digest,
 user_actions=actions,source_pins=source_pins,five_point_contacts=first,
 total_decoded_diagnostic_events=len(events),complete_physical_contact_lifecycles=len(contacts),maximum_simultaneous_contacts_observed=maximum,
 horizontal_swipe=dict(start=horizontal[0][0],end=horizontal[0][-1],printed_points=len(horizontal[0])),
 vertical_swipe=dict(start=vertical[0][0],end=vertical[0][-1],printed_points=len(vertical[0])),
 overlapping_contacts=overlaps,original_hid_dual_contact_success=dual,backend_status=status,
 new_physical_reports_during_capture=status[-1]['produced']-status[0]['produced'],original_hid_accepted_total=status[-1]['sent'],queue_drops=0,
 last_original_watchdog=watchdog[-1],no_synthetic_events=True,moves_throttled_not_full_wire_capture=True,
 input_chain_qualified='Real sensor decoded coordinates -> tracker -> native HID report -> original IOHID handleReportWithTime success',
 visible_gui_event_delivery_qualified=False,maximum10contact_capacity_qualified=False,physical_panel_120hz_qualified=False,full_goal_complete=False)
if a.output:a.output.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',newline='\n')
print('PASS real5-point/H+V swipes/two-contact identity+sequential release; original HID dual active success; newly accepted',out['new_physical_reports_during_capture'],'reports, drops0; GUI/10contacts/physical120Hz unqualified')
