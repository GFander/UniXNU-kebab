"""Reproduce actual TCM framing/command tests with explicit I2C stand-ins."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--qualification',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);a=p.parse_args()
q=a.qualification;cache=a.cache.resolve();cache.mkdir(parents=True,exist_ok=True);os.environ['TMPDIR']=str(cache)
manifest=json.loads((q/'all-mac-source-0433.json').read_text())['files']
for name in ('kebab_touch_tcm.h','kebab_touch_tcm.c','kebab_tcm_wire.h','kebab_touch.c'):
 raw=(a.source/'bsd/dev'/name).read_bytes();assert hashlib.sha256(raw).hexdigest()==manifest['bsd/dev/'+name]
 if name.endswith('.h'): (cache/name).write_bytes(raw)
 if name=='kebab_touch_tcm.c':
  text=raw.decode();assert text.count('#define KEBAB_TOUCH_KERNEL 1')==1
  (cache/'decoder-host.cpp').write_text(text.replace('#define KEBAB_TOUCH_KERNEL 1','/* Host header selection only; all decoder bodies unchanged. */'))
header=(cache/'kebab_tcm_wire.h').read_text();source=(a.source/'bsd/dev/kebab_touch.c').read_text()
files={'read':'touch-wire-production-test.cpp','write':'touch-write-production-test.cpp','command':'touch-command-production-test.cpp'}
receipts={'read':'test-touch-wire-result.json','write':'test-touch-write-result.json','command':'test-touch-command-result.json'}
for name,file in files.items():
 raw=(q/file).read_bytes();receipt=json.loads((q/receipts[name]).read_text());assert receipt['passed']and hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']
 if name=='command':
  for func in ('kt_tcm_wire_read','kt_tcm_wire_write','kt_tcm_read_header','kt_tcm_command'):
   start=source.index('static int\n'+func+'(');opening=source.index('{',start);depth=1;end=opening+1
   while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
   assert source[start:end]in raw.decode()
 path=cache/(name+'.cpp');path.write_bytes(raw)
def run(tag,fixture,expected):
 binary=cache/tag
 subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cache/(fixture+'.cpp')),str(cache/'decoder-host.cpp'),'-o',str(binary)],check=True)
 result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
 assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
 if expected:print(result.stdout.strip())
 else:assert 'Assertion'in result.stderr;print('Rejected behavioral regression:',tag)
for name in files:run('actual-'+name,name,True)
variants=[('ignore-prefix','read',' || chunk[0]!=KEBAB_TCM_MARKER || chunk[1]!=0x03',''),('ignore-padding','read','return out[length]==KEBAB_TCM_PADDING?0:-1;','return 0;'),('copy-prefix','read','out[offset+i]=chunk[i+2]','out[offset+i]=chunk[i]'),('replay-command-opcode','write','offset==1?command[0]:0x01','command[0]'),('overlap-chunk','write','offset+=bytes;remaining-=bytes;','offset+=bytes-1;remaining-=bytes;'),('ignore-length','write','((uint32_t)command[1]|((uint32_t)command[2]<<8))+3!=length','false')]
for tag,fixture,old,new in variants:
 assert old in header;(cache/'kebab_tcm_wire.h').write_text(header.replace(old,new));run(tag,fixture,False)
(cache/'kebab_tcm_wire.h').write_text(header)
print('PASS original-source TCM read/write/command bodies and6behavioral mutations; host I2C and app vector are explicit stand-ins, not physical touch proof')
