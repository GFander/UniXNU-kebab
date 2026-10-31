"""Bind the next graphics review to the actual latest shipped inputs."""
from pathlib import Path
import hashlib,importlib.util,json,re,sys
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix'
manifest=json.loads((r/'all-mac-source-0423.json').read_text())['files']
snap=r/'gpu-audit-0424';actual={}
for p in snap.rglob('*'):
 if p.is_file():
  name=p.relative_to(snap).as_posix();h=hashlib.sha256(p.read_bytes()).hexdigest();assert h==manifest[name],name;actual[name]=h
assert len(actual)==8
old=json.loads((r/'guide-current-inputs-audit.json').read_text())
components=json.loads((b/'prelink-0424.json').read_text())
spec=importlib.util.spec_from_file_location('owned',r.parent/'cryptex-runtime-20261029/owned-memory.py');owned=importlib.util.module_from_spec(spec);spec.loader.exec_module(owned)
fs=owned.Fat32((b/'payload-0424-ro.img').read_bytes());files={n:x for n,x,_,_ in fs.walk(fs.root)}
adt=files['EFI/UniXNU/Payloads/kernel.macho.adt'];tree,end=owned.parse(adt);assert end==len(adt)
nodes=[]
def walk(t,path=''):
 props=dict(t[0]);name=props.get('name',b'?').split(b'\0')[0].decode(errors='replace');path=path+'/'+name
 if any(x in (path+' '+','.join(props)).lower() for x in ('display','framebuffer','mdss','dsi','gpu','smmu','geni','touch')):nodes.append(dict(path=path,properties=list(props)))
 for child in t[1]:walk(child,path)
walk(tree)
native=hashlib.sha256((b/'kernel-0423.kebab').read_bytes()).hexdigest()
assert native=='bea59aaf413c87356caadad4c3cd8c2fab52c7b91a62b69fb9e4a228eff11366'
runtime=(r/'0424-media-boot.log').read_text();process=(r/'0424-post-archive-process.log').read_text()
assert 'KEBAB FB: MobileGestaltHelper sel 8' in runtime and 'KEBAB FB: MobileGestaltHelper sel 28' in runtime
assert 'name=SpringBoard' in runtime and 'pc=0x19b968ca8' in runtime
assert re.search(r'KEBAB PROC: pid=57 .*state=2 .*name=backboardd',process)
abi=json.loads((r/'0424-display-original-abi.json').read_text())
assert abi['cache_uuid']=='8f970452c9e33e88b114feb1864bc9e0'
assert any(x['text']=='failed to find the main CADisplay' for x in abi['assertion_strings'])
out=dict(qualified_build='0424',native_sha256=native,shipped_prelink_sha256=hashlib.sha256(files['EFI/UniXNU/Payloads/kernel.macho']).hexdigest(),guide_sha256=hashlib.sha256((r.parent/'DIRECTION-GPU-DISPLAY-INPUT.md').read_bytes()).hexdigest(),source_hashes_equal_actual_native_source=actual,current_components=components['components'],current_codeless=components['codeless_components'],original_hid_metadata_reference='guide-current-inputs-audit.json',original_donor_sha256=components['original_sha256'],hid_original_metadata_unchanged=components['original_sha256']=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221',hid_added_code=[x for x in old['hid_added_code_components'] if x not in components['components']],hid_added_codeless=old['hid_added_codeless_components'],adt_nodes=nodes,adt_sha256=hashlib.sha256(adt).hexdigest(),runtime_assertion='FBSDisplayMonitor: failed to find the main CADisplay',framebuffer_really_opened_by='MobileGestaltHelper selectors8,28',backboardd_actual_process_observed=True,runtime_log_sha256=hashlib.sha256((r/'0424-media-boot.log').read_bytes()).hexdigest(),process_log_sha256=hashlib.sha256((r/'0424-post-archive-process.log').read_bytes()).hexdigest(),bluetooth_deferred=True,graphics_implemented_or_qualified=False)
(r/'guide-current-inputs-audit-0424.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:v for k,v in out.items() if k not in ('adt_nodes','source_hashes_equal_actual_native_source')},indent=2));print('ADT nodes',nodes)
