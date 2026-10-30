from pathlib import Path
import json,hashlib,struct,sys,capstone,re,subprocess
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';sys.path.insert(0,str(b))
from native_prelink import macho,backed_offset,exports,unserialize_xml
v=sys.argv[1];d=(b/'kernelcache.fileset.macho').read_bytes();assert hashlib.sha256(d).hexdigest()=='2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221'
m=macho(d);read=lambda va,n:d[backed_offset(m['segments'],va,n,len(d)):][:n]
s=next(x for x in m['segments'] if x['name']=='__PRELINK_INFO');e=next(e for e in unserialize_xml(read(s['va'],s['filesize']))['_PrelinkInfoDictionary'] if e['CFBundleIdentifier']=='com.apple.driver.AppleARMWatchdogTimer')
mod=macho(d,backed_offset(m['segments'],e['_PrelinkExecutableLoadAddr'],32,len(d)))
assert read(0xfffffff00555ed3a,11)==b'IOWatchdog\0';assert read(0xfffffff00555eced,21)==b'IOWatchdogUserClient\0'
c=capstone.Cs(capstone.CS_ARCH_ARM64,0)
checks={0xfffffff006bd5108:('mov','w3, #0xf8'),0xfffffff006bd50c8:('mov','w3, #0xe8'),0xfffffff006bd51fc:('str','x0, [x20, #0xa0]'),0xfffffff006bd5504:('ldr','x8, [x8, #0x2a0]'),0xfffffff006bd5518:('ldr','x8, [x8, #0x540]'),0xfffffff006bd54c0:('str','x8, [x20, #0xc8]'),0xfffffff006bd5dfc:('add','x2, x19, #0xc8'),0xfffffff006bd5e00:('mov','w0, #0x5a'),0xfffffff006bd5e18:('str','x8, [x19, #0xb8]'),0xfffffff006bd5ec4:('strb','wzr, [x19, #0xa8]'),0xfffffff006bd5f50:('strb','w8, [x20, #0xa8]')}
for va,wanted in checks.items():
 ins=next(c.disasm(read(va,4),va));assert (ins.mnemonic,ins.op_str)==wanted,(hex(va),ins.mnemonic,ins.op_str)
original_exports={n:va for va,ns in exports(d,m).items() for n in ns};reverse={va:n for n,va in original_exports.items()}
ptr=lambda va:struct.unpack('<Q',read(va,8))[0];table=0xfffffff006fe5f68
assert [ptr(table+i*8) for i in (168,169,170)]==[original_exports['___cxa_pure_virtual']]*3 and ptr(table+171*8)==0
actual=(b/('kernel-'+v+'.kebab')).read_bytes()
subprocess.run([sys.executable,str(b.parent/'strip_ctf.py'),str(b/('kernel-'+v+'.kebab')),str(b/('k'+v+'.noctf'))],check=True)
raw=(b/('k'+v+'.noctf')).read_bytes();image=macho(raw);native={n:va for va,ns in exports(raw,image).items() for n in ns}
sym=next(at for cmd,at,_ in image['commands'] if cmd==2)
at,count,strings,size=struct.unpack_from('<4I',raw,sym+8)
for i in range(count):
 ix,kind,_,_,va=struct.unpack_from('<IBBHQ',raw,at+i*16)
 if not(kind&0xe0) and kind&0xe:
  name=raw[strings+ix:raw.index(b'\0',strings+ix)].decode();native[name]=va
nread=lambda va,n:raw[backed_offset(image['segments'],va,n,len(raw)):][:n]
watchdog_classes=[(name,symbol) for name,symbol in [('KebabWatchdog','__ZTV13KebabWatchdog'),('KebabPlatformWatchdog','__ZTV21KebabPlatformWatchdog')] if symbol in native]
assert len(watchdog_classes)==1
watchdog_class,watchdog_vtable=watchdog_classes[0]
base=native[watchdog_vtable]+16;own={0,1,7,14,84,86,87,140,168,169,170}
for i in range(171):
 target=struct.unpack('<Q',nread(base+i*8,8))[0]
 if i in own:assert any(va==target and (str(len(watchdog_class))+watchdog_class) in name for name,va in native.items()),(i,hex(target))
 else:assert native[reverse[ptr(table+i*8)]]==target,(i,hex(target),reverse.get(ptr(table+i*8)))
slide=-0x1effcdc8000
bindings={'__ZN10IOWatchdogC2EPK11OSMetaClass':0xfffffff006bd5128,'__ZN10IOWatchdogD2Ev':0xfffffff006bd5148,'__ZN10IOWatchdog10gMetaClassE':0xfffffff007bb9928,'__ZTV10IOWatchdog':table-16,'_kebab_original_watchdog_start':0xfffffff006bd51c0}
for name,va in bindings.items():assert native[name]==va+slide,(name,hex(native[name]))
text=next(x for x in mod['segments'] if x['name']=='__TEXT_EXEC');exe=hashlib.sha256(read(text['va'],text['filesize'])).hexdigest()
q=dict(qualified=True,native_version=v,native_class=watchdog_class,native_sha256=hashlib.sha256(actual).hexdigest(),original_kernelcache_sha256=hashlib.sha256(d).hexdigest(),original_executable_sha256=exe,original_executable_bytes=text['filesize'],exact_original_instructions_checked=len(checks),virtual_slots_checked=171,bindings={n:hex(va+slide) for n,va in bindings.items()},hardware_watchdog_implemented=False,original_client_delegated=True,runtime_qualified=False)
(r/'original-watchdog-abi-qualified.json').write_text(json.dumps(q,indent=2)+'\n');(r/(v+'-watchdog-abi.json')).write_text(json.dumps(q,indent=2)+'\n');print('PASS original 20H392 watchdog, 171 native slots, 5 bindings and',len(checks),'private-layout instructions')
