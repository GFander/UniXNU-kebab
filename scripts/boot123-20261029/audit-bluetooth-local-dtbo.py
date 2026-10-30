"""Read the owner's OTA-matched DTBO; export only board resource metadata."""
from pathlib import Path
import argparse,hashlib,json,struct,zipfile
r=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--image',type=Path,default=r.parents[1]/'01-rom/images/dtbo.img');p.add_argument('--ota',type=Path,default=r.parents[1]/'01-rom/lineage-24.0-20261016-nightly-kebab-signed.zip');p.add_argument('--manifest-sha256');a=p.parse_args()
data=a.image.read_bytes()
# Use the already audited AOSP protobuf reader, without executing its extractor.
ns={};source=(r/'extract-bluetooth-vendor-reference.py').read_text()
exec(source[source.index('def fields('):source.index('with zipfile.ZipFile')],ns)
fields,one=ns['fields'],ns['one']
with zipfile.ZipFile(a.ota) as z,z.open('payload.bin') as src:
    magic,major,n,sig=struct.unpack('>4sQQI',src.read(24));assert magic==b'CrAU' and major==2 and n<16<<20
    manifest=src.read(n);assert len(manifest)==n
    pinned=a.manifest_sha256 or json.loads((r.parent/'bluetooth-reference/lineage-24.0-20261016/vendor-extraction.json').read_text())['manifest_sha256']
    assert hashlib.sha256(manifest).hexdigest()==pinned
    top=list(fields(manifest));part=next(list(fields(v)) for k,v in top if k==13 and one(list(fields(v)),1)==b'dtbo')
    info=list(fields(one(part,7)))
    assert len(data)==one(info,1) and hashlib.sha256(data).digest()==one(info,2)
# dt_table_header / entry: AOSP source.android.com/docs/core/architecture/dto/partitions
magic,total,header,entry_size,count,entries,page,version=struct.unpack_from('>8I',data)
assert magic==0xd7b7ab1e and header==32 and entry_size==32 and version==0
assert 0<count<=256 and header<=entries and entries+entry_size*count<=total<=len(data)
def cells(value):
    assert len(value)%4==0
    return list(struct.unpack('>%dI'%(len(value)//4),value))
def read_fdt(blob):
    magic,size,structure,strings,reserve,ver,compatible,cpu,strings_len,structure_len=struct.unpack_from('>10I',blob)
    assert magic==0xd00dfeed and 40<=size<=len(blob) and ver==17 and compatible<=17
    assert structure%4==0 and structure+structure_len<=size and strings+strings_len<=size
    table=blob[strings:strings+strings_len];pos=structure;end=structure+structure_len
    nodes=[];stack=[];paths=set();finished=False
    while pos+4<=end:
        tag=struct.unpack_from('>I',blob,pos)[0];pos+=4
        if tag==1:
            nul=blob.index(b'\0',pos,end);name=blob[pos:nul].decode();pos=(nul+4)&~3
            path=(stack[-1][0].rstrip('/') if stack else '')+'/'+name
            assert path not in paths;paths.add(path);node=(path,{})
            nodes.append(node);stack.append(node)
        elif tag==2:
            assert stack;stack.pop()
        elif tag==3:
            assert stack and pos+8<=end
            length,noff=struct.unpack_from('>II',blob,pos);pos+=8
            assert pos+length<=end and noff<len(table)
            name=table[noff:table.index(b'\0',noff)].decode();assert name not in stack[-1][1]
            stack[-1][1][name]=blob[pos:pos+length];pos=(pos+length+3)&~3
        elif tag==4:pass
        elif tag==9:
            assert not stack;finished=True;break
        else:raise ValueError('invalid FDT token')
    assert finished and nodes and nodes[0][0]=='/'
    return nodes
allowed={'compatible','status','reg','regulator-name','regulator-min-microvolt','regulator-max-microvolt','qcom,resource-name','function','pins','bias-disable','bias-pull-up','bias-pull-down','drive-strength','clocks','clock-names','interrupts-extended','pinctrl-0','pinctrl-1','pinctrl-2','pinctrl-names'}
def render(value):
    if not value:return True
    if value[-1:]==b'\0' and all(c==0 or 32<=c<127 for c in value):return value.rstrip(b'\0').decode().split('\0')
    return ['0x%x'%x for x in cells(value)] if len(value)%4==0 else value.hex()
result=[]
for i in range(count):
    size,off,ident,rev,*custom=struct.unpack_from('>8I',data,entries+i*entry_size)
    assert size>=40 and off>=entries+entry_size*count and off+size<=total
    hdr=struct.unpack_from('>10I',data,off)
    assert hdr[0]==0xd00dfeed and 40<=hdr[1]<=size and hdr[2]+hdr[9]<=hdr[1] and hdr[3]+hdr[8]<=hdr[1]
    nodes=read_fdt(data[off:off+size]);assert len(nodes)>8
    phandles={cells(props['phandle'])[0]:path for path,props in nodes if 'phandle' in props}
    fixups={}
    for path,props in nodes:
        if path=='/__fixups__':
            for label,value in props.items():
                for reference in value.rstrip(b'\0').decode().split('\0'):
                    target,prop,position=reference.rsplit(':',2)
                    fixups[(target,prop,int(position))]=label
    resources=[]
    for path,props in nodes:
        fragment='/'+path.split('/')[1] if path!='/' else '/'
        target_label=fixups.get((fragment,'target',0),'')
        if not (any(x in path or x in target_label for x in ('bt_qca6390','bluetooth','bt_en_sleep','998000','qupv3_se6','regulator-pm8150-s5','regulator-pm8150-s6','regulator-pm8150a-s8')) or any(k.startswith('qca,bt-') for k in props)):continue
        filtered={name:render(value) for name,value in props.items() if name in allowed or name.startswith('qca,') and ('supply' in name or 'voltage' in name or 'gpio' in name)}
        # Device addresses, calibration and other unrelated values are never exported.
        refs={name:phandles.get(cells(value)[0],fixups.get((path,name,0),'unresolved external phandle')) for name,value in props.items() if ('supply' in name or 'gpio' in name or name.startswith('pinctrl-')) and value and len(value)%4==0 and name not in ('pinctrl-names',)}
        resources.append(dict(path=path,target_label=target_label,properties=filtered,references=refs))
    root=next(props for path,props in nodes if path=='/')
    result.append(dict(index=i,id=ident,revision=rev,custom=custom,root={k:render(v) for k,v in root.items() if k in ('compatible','model','qcom,board-id','qcom,msm-id','qcom,pmic-id')},blob_sha256=hashlib.sha256(data[off:off+size]).hexdigest(),resources=resources))
q=dict(dtbo_sha256=hashlib.sha256(data).hexdigest(),partition_bytes=len(data),all_entries=count,
       matched_prior_pinned_ota_manifest=True,pinned_manifest_sha256=pinned,additional_ota_signature_validation=False,
       entries=result,selected_on_this_phone_entry_unknown=True,no_phone_writes=True,hardware_power_tested=False)
(r/'bluetooth-local-dtbo-audit.json').write_text(json.dumps(q,indent=2)+'\n')
print('Matched OTA DTBO:',count,'entries;',sum(len(x['resources']) for x in result),'selected board resource nodes')
for entry in result:
    print('entry',entry['index'],'id',entry['id'],'revision',entry['revision'],'root',entry['root'])
    for node in entry['resources']:
        if any(k.startswith('qca,bt-') for k in node['properties']):print(json.dumps(node,indent=2))
