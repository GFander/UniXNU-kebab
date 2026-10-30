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
 rec.note('capturing complete owner verification/startup, maximum 2700s')
 try:
  while time.monotonic()<end:
   # One fixed read-only probe, sent through this process's existing port.
   # Consume the marker before writing; a timeout never retries silently.
   request=here/(version+'-btfw-request')
   if request.exists():
    request.unlink()
    ser.write_timeout=2
    try:
     sent=ser.write(b'BTFW?\n')
     if sent!=6:raise IOError('short BTFW command write')
     rec.note('sent requested read-only BTFW probe on existing capture port')
    except Exception as exc:rec.note('BTFW command write failed: '+str(exc))
   chunk=ser.read(65536)
   if chunk:
    pending=rec.feed(chunk,pending)
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
