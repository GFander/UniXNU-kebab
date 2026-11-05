#!/usr/bin/env python3
"""Inventory display resources from a supplied reference FDT; no device access."""
import argparse, hashlib, json, re, struct
from pathlib import Path

class InvalidFdt(ValueError): pass

def parse_fdt(data):
    def require(ok, reason):
        if not ok: raise InvalidFdt(reason)
    require(40 <= len(data) <= 64*1024*1024, 'FDT length')
    magic,total,off,strings_off,reserve,version,last,cpu,strings_len,struct_len=struct.unpack_from('>10I',data)
    require(magic==0xd00dfeed and total==len(data), 'FDT magic/total size')
    require(version==17 and last<=17, 'FDT version')
    require(off>=40 and off%4==0 and off+struct_len<=total, 'structure bounds')
    require(strings_off>=40 and strings_off+strings_len<=total and reserve>=40 and reserve<total, 'strings/reserve bounds')
    require(off+struct_len<=strings_off or strings_off+strings_len<=off, 'overlapping blocks')
    strings=data[strings_off:strings_off+strings_len];end=off+struct_len
    nodes={};stack=[];closed=False;ended=False
    def word():
        nonlocal off
        require(off+4<=end,'truncated token');v=struct.unpack_from('>I',data,off)[0];off+=4;return v
    while off<end:
        token=word()
        if token==1:
            require(not closed,'multiple roots')
            stop=data.find(b'\0',off,end);require(stop>=off,'unterminated node')
            name=data[off:stop].decode('ascii');require('/' not in name,'node separator')
            require(bool(name)==bool(stack),'root/node name')
            path=(stack[-1] if stack else '')+'/'+name if stack else '/'
            if stack and stack[-1]=='/':path='/'+name
            require(path not in nodes,'duplicate node')
            nodes[path]={};stack.append(path);off=(stop+4)&~3;require(off<=end,'node padding')
        elif token==2:
            require(bool(stack),'unmatched end node');stack.pop();closed=not stack
        elif token==3:
            require(bool(stack),'property outside node');length=word();name_off=word()
            require(name_off<len(strings),'property name bounds')
            stop=strings.find(b'\0',name_off);require(stop>name_off,'property name terminator')
            name=strings[name_off:stop].decode('ascii');require(name not in nodes[stack[-1]],'duplicate property')
            require(off+length<=end,'property bounds');nodes[stack[-1]][name]=data[off:off+length]
            off=(off+length+3)&~3;require(off<=end,'property padding')
        elif token==4: pass
        elif token==9:
            require(closed and not stack and '/' in nodes,'incomplete tree')
            require(not any(data[off:end]),'trailing structure data');ended=True;break
        else: raise InvalidFdt('unknown token')
    require(ended,'missing end token');return nodes

def cells(raw):
    if len(raw)%4:raise InvalidFdt('cell alignment')
    return list(struct.unpack('>'+str(len(raw)//4)+'I',raw))

def strings(raw):
    if not raw:return []
    if raw[-1]!=0:raise InvalidFdt('string terminator')
    return raw[:-1].decode('ascii').split('\0')

def inventory(data):
    nodes=parse_fdt(data);resources=[]
    for path,p in nodes.items():
        compat=strings(p.get('compatible',b''))
        if not any(c=='qcom,sde-kms' or c.startswith(('qcom,dsi-ctrl-','qcom,dsi-phy-','qcom,mdss_dsi_pll_')) or c=='qcom,kona-dispcc' for c in compat):continue
        parent=path.rsplit('/',1)[0] or '/';bus=nodes[parent]
        ac=cells(bus.get('#address-cells',struct.pack('>I',2)));sc=cells(bus.get('#size-cells',struct.pack('>I',1)))
        if len(ac)!=1 or len(sc)!=1 or ac[0] not in (1,2) or sc[0] not in (1,2):raise InvalidFdt('bus cell geometry')
        ac,sc=ac[0],sc[0];reg=cells(p.get('reg',b''));names=strings(p.get('reg-names',b''));stride=ac+sc
        if len(reg)%stride or len(names)!=len(reg)//stride or len(names)!=len(set(names)):raise InvalidFdt('register tuple/name count')
        ranges=[]
        for n,name in enumerate(names):
            row=reg[n*stride:(n+1)*stride];address=0;size=0
            for value in row[:ac]:address=(address<<32)|value
            for value in row[ac:]:size=(size<<32)|value
            if not size or address+size>1<<64:raise InvalidFdt('register extent')
            ranges.append(dict(name=name,bus_address=address,bytes=size))
        # Retain the full bus translation chain; do not silently call a bus
        # address a physical address or infer resource/power ownership.
        chain=[];at=parent
        while at!='/':
            chain.append(dict(node=at,ranges_present='ranges' in nodes[at],ranges_cells=cells(nodes[at].get('ranges',b''))))
            at=at.rsplit('/',1)[0] or '/'
        irq_parent=None;at=path
        while True:
            if 'interrupt-parent' in nodes[at]:
                handle=cells(nodes[at]['interrupt-parent'])
                if len(handle)!=1:raise InvalidFdt('interrupt parent')
                matches=[n for n,v in nodes.items() if v.get('phandle',v.get('linux,phandle'))==struct.pack('>I',handle[0])]
                if len(matches)!=1:raise InvalidFdt('unresolved interrupt parent')
                irq_parent=matches[0];break
            if at=='/':break
            at=at.rsplit('/',1)[0] or '/'
        resources.append(dict(node=path,compatible=compat,status=strings(p.get('status',b'okay\0')),registers=ranges,bus_translation_chain=chain,
          interrupt_cells=cells(p.get('interrupts',b'')),interrupt_parent=irq_parent,
          clock_names=strings(p.get('clock-names',b'')),clock_specifiers=cells(p.get('clocks',b'')),
          iommu_specifiers=cells(p.get('iommus',b''))))
    if not resources:raise InvalidFdt('no matching SM8250 display resources')
    # Whitelist only the display selection; never emit the complete bootargs,
    # serial numbers, UUIDs, private paths, or arbitrary property payloads.
    args=nodes.get('/chosen',{}).get('bootargs',b'').decode('ascii','replace')
    selected=re.findall(r'(?:^|\s)msm_drm\.dsi_display0=([A-Za-z0-9_,.-]+)(?::[^\s]*)?',args)
    return dict(scope='Offline reference FDT resource inventory; not actual phone identity, powered MMIO ownership, IRQ routing or native display proof',
      reference_fdt_sha256=hashlib.sha256(data).hexdigest(),reference_panel_selection=selected,
      physical_phone_panel_verified=False,mmio_access_performed=False,register_writes_performed=False,
      hardware_vsync_or_refresh_rate_qualified=False,resources=resources)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dtb',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    try:result=inventory(a.dtb.read_bytes())
    except (OSError,ValueError,UnicodeError,struct.error) as error:p.error(str(error))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x',encoding='utf-8') as out:json.dump(result,out,indent=2);out.write('\n')
    print('PASS offline resource inventory:',len(result['resources']),'display nodes; hardware unqualified')
if __name__=='__main__':main()
