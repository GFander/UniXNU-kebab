"""Finish an explicitly requested RO power qualification using the existing
capture's serialized command markers. Never opens another COM port or retries
a failed hardware operation. The first post-MKB query is submitted by caller.
"""
from pathlib import Path
import json,re,subprocess,sys,time
r=Path(__file__).resolve().parent;version,source=sys.argv[1:]
assert all(len(x)==4 and x.isdigit() for x in (version,source))
log=r/(version+'-media-boot.log');deadline=time.monotonic()+360
def current():
 assert log.stat().st_size<8<<20,'unexpected capture size'
 s=log.read_text()
 assert not re.search(r'panic\(cpu',s),'native panic; preserve capture for investigation'
 assert 'completion timeout; probes blocked' not in s,'probe timeout; no retry'
 at=s.find('****** DONE MKB_INIT ****')
 if at>=0:
  bad=re.findall(r'KEBAB BTPWR: end error=(\d+)',s[at:])
  assert all(int(x)==0 for x in bad),('post-MKB hardware query failed',bad)
 return s
def wait(predicate):
 while time.monotonic()<deadline:
  text=current()
  if predicate(text):return text
  time.sleep(1)
 raise TimeoutError('bounded qualification wait; capture retained')
def completions(text):return text.count('end error=0 read-only; board binding and rail ownership not established')
s=wait(lambda s:completions(s)>=1)
print('First actual post-MKB query completed',flush=True)
if completions(s)<2:
 marker=r/(version+'-btpwr-request');assert not marker.exists();marker.touch()
s=wait(lambda s:completions(s)>=2)
print('Two actual resource queries completed',flush=True)
last=s.rfind('end error=0 read-only; board binding and rail ownership not established')
if s.rfind('KEBAB BTHW: fixed SE6/NORTH')<last:
 marker=r/(version+'-bthw-request');assert not marker.exists();marker.touch()
s=wait(lambda s:s.rfind('BTHW actual completion observed')>s.rfind('end error=0 read-only; board binding and rail ownership not established'))
print('Final independent hardware snapshot completed',flush=True)
wait(lambda s:any(float(t)>=180 for t in re.findall(r'\[\s*([\d.]+)\] KEBAB WATCHDOG: clients=1 polls=\d+ beats=\d+ enabled=1 remaining=\d+',s)))
(r/(version+'-stop-capture')).touch()
provenance=r/(version+'-capture-provenance.json')
while time.monotonic()<deadline:
 if provenance.exists():
  try:json.loads(provenance.read_text());break
  except json.JSONDecodeError:pass
 time.sleep(.2)
else:raise TimeoutError('capture did not finalize')
with (r/(version+'-bt-power-analysis.txt')).open('w') as out:
 subprocess.run([sys.executable,str(r/'analyse-bt-power.py'),version,source],check=True,stdout=out,stderr=subprocess.STDOUT)
print('PASS actual RO discovery + original substrate; controller/RF still unqualified',flush=True)
