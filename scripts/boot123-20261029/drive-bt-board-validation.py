"""Finish explicitly requested fixed board qualification on the existing
capture port. First post-MKB query must already be submitted by caller.
No repeated attempt is made after an error or ambiguous completion.
"""
from pathlib import Path
import json,re,subprocess,sys,time
r=Path(__file__).resolve().parent;version,source=sys.argv[1:]
assert all(len(x)==4 and x.isdigit() for x in (version,source))
log=r/(version+'-media-boot.log');deadline=time.monotonic()+360
def current():
 assert log.stat().st_size<8<<20
 text=log.read_text()
 assert not re.search(r'panic\(cpu',text),'native panic; capture retained'
 assert 'completion timeout; probes blocked' not in text,'completion ambiguous; no retry'
 at=text.find('****** DONE MKB_INIT ****')
 if at>=0:assert all(int(e)==0 for e in re.findall(r'KEBAB BTBOARD: end error=(\d+)',text[at:])), 'actual post-MKB board query failed'
 return text
def wait(predicate):
 while time.monotonic()<deadline:
  text=current()
  if predicate(text):return text
  time.sleep(1)
 raise TimeoutError('bounded wait; capture retained')
end='KEBAB BTBOARD: end error=0 fixed board metadata only; no SMEM/rail/GPIO writes'
for count in range(1,4):
 text=wait(lambda s:s.count(end)>=count)
 print('Actual board query completed',count,flush=True)
 if count<3 and text.count(end)<count+1:
  marker=r/(version+'-btboard-request');assert not marker.exists();marker.touch()
text=current()
if text.rfind('KEBAB BTHW: fixed SE6/NORTH')<text.rfind(end):
 marker=r/(version+'-bthw-request');assert not marker.exists();marker.touch()
wait(lambda s:s.rfind('BTHW actual completion observed')>s.rfind(end))
print('Final independent GPIO/GCC snapshot completed',flush=True)
wait(lambda s:any(float(t)>=180 for t in re.findall(r'\[\s*([\d.]+)\] KEBAB WATCHDOG: clients=1 polls=\d+ beats=\d+ enabled=1 remaining=\d+',s)))
(r/(version+'-stop-capture')).touch()
provenance=r/(version+'-capture-provenance.json')
while time.monotonic()<deadline:
 if provenance.exists():
  try:json.loads(provenance.read_text());break
  except json.JSONDecodeError:pass
 time.sleep(.2)
else:raise TimeoutError('capture did not finalize')
with (r/(version+'-bt-board-analysis.txt')).open('w') as out:
 subprocess.run([sys.executable,str(r/'analyse-bt-board.py'),version,source],check=True,stdout=out,stderr=subprocess.STDOUT)
print('PASS actual SMEM identity/TCSR lifecycle + original substrate; controller/RF unqualified',flush=True)
