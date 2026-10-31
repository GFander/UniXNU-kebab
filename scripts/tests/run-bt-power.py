"""Published command-DB/USB tests. Memory, maps, locks, context and drivers
are explicit host stand-ins. These tests do not emulate radio success.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
p=argparse.ArgumentParser();p.add_argument('--qualification',type=Path,required=True)
p.add_argument('--cache',type=Path,default=Path.home()/'.cache/hios-bt-power')
p.add_argument('--compiler',default='clang++');a=p.parse_args()
here=Path(__file__).resolve().parent;a.cache.mkdir(parents=True,exist_ok=True)
os.environ['TMPDIR']=str(a.cache.resolve())
for name,receipt,mutations in [
 ('bt-power.cpp','test-bt-power-result.json',[
  ('duplicate','if (mask & (1u << index)) {','if (false) {'),
  ('slave-class','if (id != 4u || (address','if (false || (address'),
  ('aux-offset','uint32_t at = auxiliary + relative + byte, value;','uint32_t at = auxiliary + byte, value;'),
  ('early-bsd','if (__atomic_load_n(&initproc, __ATOMIC_ACQUIRE) == PROC_NULL)','if (false)'),
  ('alias-address','previous->address != address','false'),
  ('alias-metadata','memcmp(previous->auxiliary, resource.auxiliary, sizeof(resource.auxiliary))','0')]),
 ('bt-power-usb.cpp','test-bt-firmware-panic-result.json',[
  ('input-priority','if (ud_command_seen(buf, received, "BTPWR?\\n", sizeof("BTPWR?\\n") - 1))','if (false)')])]:
 raw=(here/name).read_bytes();q=json.loads((a.qualification/receipt).read_text())
 assert q['passed'] and hashlib.sha256(raw).hexdigest()==q['fixture_sha256']
 source=raw.decode();variants=[('actual',source,True)]
 for tag,old,new in mutations:
  assert source.count(old)==1,(name,tag)
  variants.append((tag,source.replace(old,new),False))
 for tag,text,expected in variants:
  f=a.cache/(Path(name).stem+'-'+tag+'.cpp');f.write_text(text);exe=f.with_suffix('')
  subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],text=True,capture_output=True)
  assert (result.returncode==0)==expected,(name,tag,result.stdout,result.stderr)
  if expected:print(result.stdout.strip())
  else:
   assert 'Assertion' in result.stderr,(name,tag,result.stderr)
   print(name+'/'+tag+': behavioral assertion refused')
print('PASS host read-only discovery and USB input/dispatch fault coverage; no controller or RF qualification.')
