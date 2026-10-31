"""Fill the device ring via real registry queries, then check its first full dump."""
from pathlib import Path
import hashlib,json,re,serial,sys,time
v=sys.argv[1];r=Path('D:/LOS/HiOS/p1-run/prelink-fix');dest=r/(v+'-fwlog-device.json')
assert not dest.exists(),'This requires a fresh boot with no preceding FWLOG report'
def read_until(port,marker,seconds):
    out=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        out.extend(port.read(max(1,port.in_waiting)))
        if marker in out:
            time.sleep(.08);out.extend(port.read(port.in_waiting));return bytes(out)
    (r/(v+'-fwlog-timeout.partial.log')).write_bytes(out)
    raise RuntimeError('device reply did not complete: '+repr(marker))
traffic=bytearray()
with serial.Serial('COM7',115200,timeout=.1,write_timeout=3) as port:
    port.dtr=True;port.rts=True;time.sleep(.2);port.reset_input_buffer()
    for _ in range(25):
        port.write(b'DRV?\n');port.flush()
        traffic.extend(read_until(port,b'KEBAB DRV: end;',5))
    port.write(b'FWLOG? all\n');port.flush()
    dump=read_until(port,b'\nKEBAB FWLOG: ---- end ----',15)
    port.write(b'PON?\n');port.flush();after=read_until(port,b'SPMI-3 PASS',5)
(r/(v+'-fwlog-fill.log')).write_bytes(traffic)
(r/(v+'-fwlog-full.log')).write_bytes(dump)
(r/(v+'-fwlog-after-pon.log')).write_bytes(after)
flat=dump.replace(b'\r\n',b'\n')
header=re.search(rb'KEBAB FWLOG: ring=(\d+) written=(\d+) dump=(\d+)',flat);assert header
assert int(header[1])==65536 and int(header[2])>65536 and int(header[3])==65536
begin=flat.index(b'KEBAB FWLOG: ---- begin ----\n')+len(b'KEBAB FWLOG: ---- begin ----\n')
end=flat.rindex(b'\nKEBAB FWLOG: ---- end ----');payload=flat[begin:end]
assert b'KEBAB FWLOG:' not in payload,'dump contains its own new header'
assert len(payload)>60000,('full dump transport truncated',len(payload))
assert b'KEBAB SPMI:' in after
result={'version':v,'real_registry_queries':25,'native_ring_size':65536,'native_written':int(header[2]),
 'native_dump_requested':65536,'transport_payload_bytes_LF_normalized':len(payload),
 'transport_payload_sha256':hashlib.sha256(payload).hexdigest(),'own_header_in_payload':False,
 'normal_command_after_dump_verified':True,'scope':'Real full-ring output and no self-header contamination; transport SHA is not a kernel RAM SHA.'}
dest.write_text(json.dumps(result,indent=2)+'\n');print('PASS: real full-ring FWLOG, no self-overwrite header, subsequent PON command works',result)
