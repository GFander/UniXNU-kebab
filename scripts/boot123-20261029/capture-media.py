"""Capture slow full-SHA/media startup until userland ignition or a panic loop."""
from pathlib import Path
import importlib.util,sys,time,shutil,hashlib,json
here=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cap',here.parent/'p1-run/console-cap.py')
cap=importlib.util.module_from_spec(spec);spec.loader.exec_module(cap)
version=sys.argv[1];assert len(version)==4 and version.isdigit()
end=time.monotonic()+90
while time.monotonic()<end and cap.find_port() is None:time.sleep(.1)
port=cap.find_port();assert port is not None
rec=cap.Recorder(here/(version+'-media-boot.log'))
try:
 ser=cap.open_retry(port)
 end=time.monotonic()+2700
 pending=b''
 owner_deadline=None
 probe_pending=None
 probes_blocked=False
 rec.note('capturing complete owner verification/startup, maximum 2700s')
 try:
  while time.monotonic()<end:
   # Fixed commands only. Wait for a real completion before another request,
   # so separate CDC commands cannot be merged into one parser transaction.
   if probe_pending and time.monotonic()>=probe_pending[2]:
    rec.note(probe_pending[0]+' completion timeout; probes blocked, no automatic retry')
    probes_blocked=True;probe_pending=None
   if probe_pending is None and not probes_blocked:
    for suffix,command,completion in [
     ('btfw',b'BTFW?\n','KEBAB BTFW: loader error='),
     ('bthw',b'BTHW?\n','KEBAB BTHW: no GPIO/clock/power/controller writes'),
     ('btclk',b'BTCLK\n','KEBAB BTCLK: end error=')]:
     request=here/(version+'-'+suffix+'-request')
     if not request.exists():continue
     request.unlink();ser.write_timeout=2
     try:
      sent=ser.write(command)
      if sent!=len(command):raise IOError('short '+suffix+' command write')
      rec.note('sent requested fixed '+suffix.upper()+' command on existing capture port')
      probe_pending=(suffix.upper(),completion,time.monotonic()+30)
     except Exception as exc:
      rec.note(suffix.upper()+' command write failed: '+str(exc));probes_blocked=True
     break
   line_start=len(rec.lines)
   chunk=ser.read(65536)
   if chunk:
    pending=rec.feed(chunk,pending)
    if probe_pending and any(probe_pending[1] in line for line in rec.lines[line_start:]):
     rec.note(probe_pending[0]+' actual completion observed');probe_pending=None
    if any((int(version)<368 and ('leave security.mac.img4.ignition_blob' if version=='0302' else 'enter security.mac.img4.ignition_blob') in line) or 'KEBAB PANIC: USB command loop ready' in line for line in rec.lines):break
    if int(version)<358 and version not in ('0328','0329','0330','0331','0332','0333','0334','0335','0336','0337','0338','0339','0340','0341','0343','0344','0345','0346','0348','0349','0350','0351','0352','0353','0354','0355','0356','0357') and owner_deadline is None and any('owner startup flags=0x8' in line for line in rec.lines):owner_deadline=time.monotonic()+30
   if (owner_deadline is not None and time.monotonic()>=owner_deadline) or (here/(version+'-stop-capture')).exists():break
 finally:
  ser.close()
finally:
 rec.close()
 source=here/(version+'-media-boot.log')
 destination=here.parent/'p1-run/prelink-fix'/(version+'-ro-boot.log')
 shutil.copyfile(source,destination)
 (here/(version+'-capture-provenance.json')).write_text(json.dumps(dict(
  actual_capture=str(source),standard_log_alias=str(destination),
  sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n')
