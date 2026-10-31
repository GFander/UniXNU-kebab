"""Finalize the live capture only after actual original-watchdog regression."""
from pathlib import Path
import json,re,subprocess,sys,time
r=Path(__file__).resolve().parent;v=sys.argv[1];assert v in ('0425','0426','0427')
end=time.monotonic()+300;log=r/(v+'-media-boot.log')
while time.monotonic()<end:
 text=log.read_text() if log.exists() else ''
 assert not re.search(r'panic\(cpu',text),'Actual panic: retain capture for investigation'
 times=[float(t) for t in re.findall(r'\[\s*([\d.]+)\] KEBAB WATCHDOG: clients=1 polls=\d+ beats=\d+ enabled=1 remaining=\d+',text)]
 if times and max(times)>=180:break
 time.sleep(1)
else:raise TimeoutError('Original watchdog window incomplete; capture retained')
(r/(v+'-stop-capture')).touch()
while time.monotonic()<end:
 p=r/(v+'-capture-provenance.json')
 if p.exists():
  try:json.loads(p.read_text());break
  except json.JSONDecodeError:pass
 time.sleep(.2)
else:raise TimeoutError('Capture not finalized')
subprocess.run([sys.executable,str(r/'analyse-defaults-lifecycle.py'),v],check=True,stdout=subprocess.DEVNULL)
print('PASS',v,'actual roots/MKB/C,D/defaults/NVRAM/RTC/watchdog >180s; graphics unqualified',flush=True)
