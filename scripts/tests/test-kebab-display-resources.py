#!/usr/bin/env python3
"""Synthetic FDT bounds/resource/redaction tests; no hardware or donor input."""
import importlib.util,json,struct,sys
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'qualification/read-kebab-display-resources.py'
s=importlib.util.spec_from_file_location('display_resources',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def u(*v):return struct.pack('>'+'I'*len(v),*v)
def fixture(reg=None,names=b'mdp_phys\0',irq=1,extra=b'',duplicate=False):
    table=bytearray();body=bytearray()
    def token(n):body.extend(u(n))
    def begin(name):token(1);body.extend(name.encode()+b'\0');body.extend(bytes(-len(body)%4))
    def prop(name,value):
        at=len(table);table.extend(name.encode()+b'\0');token(3);body.extend(u(len(value),at)+value);body.extend(bytes(-len(body)%4))
    begin('');prop('#address-cells',u(1));prop('#size-cells',u(1))
    begin('chosen');prop('bootargs',b'androidboot.serialno=PRIVATE msm_drm.dsi_display0=qcom,panel:config0 token=SECRET\0');token(2)
    begin('intc');prop('phandle',u(1));prop('compatible',b'arm,gic-v3\0');token(2)
    begin('soc');prop('#address-cells',u(1));prop('#size-cells',u(1));prop('ranges',b'');prop('interrupt-parent',u(irq))
    begin('display@ae00000');prop('compatible',b'qcom,sde-kms\0');prop('reg',u(0xae00000,0x84208) if reg is None else reg);prop('reg-names',names)
    if duplicate:prop('reg-names',names)
    token(2);token(2);token(2);token(9);body.extend(extra)
    return u(0xd00dfeed,56+len(body)+len(table),56,56+len(body),40,17,16,0,len(table),len(body))+bytes(16)+body+table
checks=0
def reject(data):
    global checks
    try:m.inventory(data)
    except (ValueError,UnicodeError,struct.error):checks+=1;return
    raise AssertionError('malformed input accepted')
data=fixture();q=m.inventory(data);checks+=1
assert len(q['resources'])==1 and q['resources'][0]['registers'][0]['bus_address']==0xae00000
assert q['resources'][0]['interrupt_parent']=='/intc' and q['reference_panel_selection']==['qcom,panel']
assert 'PRIVATE' not in json.dumps(q) and 'SECRET' not in json.dumps(q)
assert not q['mmio_access_performed'] and not q['physical_phone_panel_verified'];checks+=4
for n in range(len(data)):reject(data[:n])
for kw in [dict(reg=b'\0'),dict(reg=u(1,2,3)),dict(reg=u(1,0)),dict(names=b'a\0b\0'),dict(names=b'a'),dict(irq=99),dict(duplicate=True),dict(extra=u(1))]:reject(fixture(**kw))
for index,value in [(0,0),(1,len(data)+1),(2,1),(3,len(data)+1),(4,0),(5,16),(8,len(data)+1),(9,len(data)+1)]:
    broken=bytearray(data);struct.pack_into('>I',broken,4*index,value);reject(bytes(broken))
print('PASS',checks,'synthetic FDT bounds, cells, duplicate properties, parent resolution and privacy checks; no hardware access')
