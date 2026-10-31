"""Run published production UART snapshots with explicit host hardware stand-ins.
No real UART, Bluetooth controller, firmware or radio is emulated as a pass.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ap=argparse.ArgumentParser();ap.add_argument('--qualification',type=Path,required=True)
ap.add_argument('--cache',type=Path,default=Path.home()/'.cache/hios-bt-uart')
ap.add_argument('--compiler',default='clang++');a=ap.parse_args()
here=Path(__file__).resolve().parent;a.cache.mkdir(parents=True,exist_ok=True)
os.environ['TMPDIR']=str(a.cache.resolve())
for kind,filename,receipt,mutations in [
 ('byte','bt-uart.cpp','test-bt-uart-result.json',[
  ('byte-order','rx->word>>=8','rx->word<<=8'),
  ('partial-word','rx->remaining=(words==1&&(status&0x80000000u)&&valid)?valid:4','rx->remaining=4'),
  ('parent-rate','out->pll[1]!=31','(out->pll[1]!=31&&false)'),
  ('fifo-packing','0x260,0x4380e','0x260,0xe'),
  ('restore-root','0x17734+i*4,root[i]','0x17734+i*4,0'),
  ('receive-mismatch','if(stash.remaining||memcmp(tx,rx,length))','if(stash.remaining)'),
  ('stuck-stop','if(!c)c=s;','if(!c)c=0;'),
  ('watermark-restore','if(i==0||i==4||i==5)continue;','if(i==0||i==4||i==5||i==27)continue;'),
  ('pending-irq','if(!c&&status)c=EIO;','if(!c&&status)c=0;'),
 ('stop-completion','if(!(status&active)&&(irq&completed))return 0;','if(!(status&active)&&((irq&completed)||true))return 0;'),
 ('early-stop-ack','for(unsigned i=0;i<2&&!c;++i){uint32_t status;c=kbu_read(io,KBU_SE,i?0x640:0x610,&status);if(!c)c=kbu_write(io,KBU_SE,i?0x648:0x618,status);}', 'if(false)for(unsigned i=0;i<2&&!c;++i){uint32_t status;c=kbu_read(io,KBU_SE,i?0x640:0x610,&status);if(!c)c=kbu_write(io,KBU_SE,i?0x648:0x618,status);}'),
 ('repeat-initialization','if(io->owner->default_state==0){','if(true){'),
 ('uncertain-initialization','if(io->owner->default_state==2&&!c)c=EIO;','if(false&&io->owner->default_state==2&&!c)c=EIO;')]),
 ('native','bt-uart-native.cpp','test-bt-uart-native-result.json',[
  ('panic-guard','if(panic_active()||','if((panic_active()&&false)||'),
  ('pll-write','if(!write&&(off==0','if((off==0'),
  ('early-bsd','if(__atomic_load_n(&initproc,__ATOMIC_ACQUIRE)==PROC_NULL)','if((__atomic_load_n(&initproc,__ATOMIC_ACQUIRE)==PROC_NULL)&&false)'),
  ('trace-completion','kbu_trace.completed=kbu_trace.sequence;','kbu_trace.completed=0;'),
  ('repeat-strobe','if(space==KBU_SE&&off==0x20&&kbu_native.default_writes)','if((space==KBU_SE&&off==0x20&&kbu_native.default_writes)&&false)')])]:
 raw=(here/filename).read_bytes();q=json.loads((a.qualification/receipt).read_text())
 assert q['passed'] and hashlib.sha256(raw).hexdigest()==q['fixture_sha256']
 src=raw.decode()
 variants=[('actual',src,True)]
 for name,old,new in mutations:
  assert old in src
  text=src.replace(old,new)
  if kind=='byte':text=text.replace('int s=kbu_stop','[[maybe_unused]] int s=kbu_stop')
  variants.append((name,text,False))
 for name,text,passed in variants:
  p=a.cache/(kind+'-'+name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  run=subprocess.run([str(exe)],capture_output=True,text=True)
  assert (run.returncode==0)==passed,(kind,name,run.stdout,run.stderr)
  if passed:print(run.stdout.strip())
  else:
   assert 'Assertion' in run.stderr and 'failed' in run.stderr,(kind,name,run.stderr)
   print(kind+'/'+name+': behavioral assertion refused')
print('PASS host fault coverage only. Physical UART and Bluetooth/RF require separate device receipts.')
