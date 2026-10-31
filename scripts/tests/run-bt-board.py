"""Published fixed-board parser/native tests; all hardware is a host
stand-in, never accepted as an actual project number or Bluetooth proof.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--qualification',type=Path,required=True)
p.add_argument('--cache',type=Path,default=Path.home()/'.cache/hios-bt-board')
p.add_argument('--compiler',default='clang++');a=p.parse_args()
here=Path(__file__).resolve().parent;a.cache.mkdir(parents=True,exist_ok=True);os.environ['TMPDIR']=str(a.cache.resolve())
raw=(here/'bt-board.cpp').read_bytes();q=json.loads((a.qualification/'test-bt-board-result.json').read_text())
assert q['passed'] and hashlib.sha256(raw).hexdigest()==q['fixture_sha256']
source=raw.decode();mutations=[
 ('not-acquired','if(owner==1u){io->owner->held=true;status->acquired=true;break;}','if(true){io->owner->held=true;status->acquired=true;break;}'),
 ('foreign-unlock','if(!cleanup&&owner!=1u)cleanup=EIO;','if(false)cleanup=EIO;'),
 ('skip-release','cleanup=io->lock_write(io->context,0u);','cleanup=0;'),
 ('canary','(tag&0xffffu)!=0xa5a5u','false'),
 ('partial-result','if(!error)*out=found;','if(!error||status->parse_error==0)*out=found;'),
 ('irq-restore','ml_set_interrupts_enabled(saved);','(void)saved;'),
 ('mapping-leak','ml_io_unmap(kbb_native.memory,KBB_SMEM_SIZE);',';'),
 ('table-bound','count>(4096u-32u)/48u','false'),
 ('uncached-bound','if(bytes>free_uncached-data)return EINVAL;','if(false)return EINVAL;')]
variants=[('actual',source,True)]
for tag,old,new in mutations:
 assert old in source,tag;variants.append((tag,source.replace(old,new),False))
for tag,text,expected in variants:
 f=a.cache/('board-'+tag+'.cpp');f.write_text(text);exe=f.with_suffix('')
 subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],text=True,capture_output=True)
 assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
 if expected:print(result.stdout.strip())
 else:
  assert 'Assertion' in result.stderr,(tag,result.stderr)
  print(tag+': behavioral assertion refused')
raw=(here/'bt-board-usb.cpp').read_bytes()
q=json.loads((a.qualification/'test-bt-firmware-panic-result.json').read_text())
assert q['passed'] and hashlib.sha256(raw).hexdigest()==q['fixture_sha256']
source=raw.decode()
old='if (ud_command_seen(buf, received, "BTPWR?\\n", sizeof("BTPWR?\\n") - 1))'
assert source.count(old)==1
for tag,text,expected in [('actual',source,True),('input-priority',source.replace(old,'if (false)'),False)]:
 f=a.cache/('board-usb-'+tag+'.cpp');f.write_text(text);exe=f.with_suffix('')
 subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(f),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],text=True,capture_output=True)
 assert (result.returncode==0)==expected,(tag,result.stdout,result.stderr)
 if expected:print(result.stdout.strip())
 else:
  assert 'Assertion' in result.stderr,result.stderr
  print('USB input-priority: behavioral assertion refused')
print('PASS host board and actual USB input/dispatch coverage only; RF remains unqualified.')
