"""Run actual loader/FAT and complete USB-dispatch snapshots with UFS stubs."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--loader',type=Path,required=True);p.add_argument('--panic',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--compiler',default='clang++');a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='kebab-btfw-',dir=a.cache) as tmp:
 root=Path(tmp);env=dict(os.environ,TMPDIR=str(root))
 def run(name,source,success=True):
  cpp=root/(name+'.cpp');exe=root/name;cpp.write_text(source)
  subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(cpp),'-lz','-o',str(exe)],check=True,env=env)
  q=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=30)
  assert (q.returncode==0)==success,(name,q.stdout,q.stderr)
  if success:print(q.stdout.strip())
  else:
   assert 'Assertion' in q.stderr and 'failed' in q.stderr,(name,q.stderr)
   print(name+': rejected by behavioral assertion')
 source=a.loader.read_text();run('actual-loader',source)
 for name,old,new in [('wrong-partition','static const char name[]="bluetooth_b"','static const char name[]="keymaster_b"'),('skip-header-crc','ufs_inspect_crc32(b,hsize)!=hcrc','ufs_inspect_crc32(b,hsize)!=hcrc&&false'),('wrong-lun-restore','su.lun=saved_lun;su.device_blocks=saved_capacity','su.lun=4;su.device_blocks=saved_capacity'),('skip-lfn-order','e[0]!=0x41||e[11]!=0x0f','false||e[11]!=0x0f'),('skip-lfn-checksum','long_match&&checksum==kbf_alias_checksum(e)','long_match&&(checksum==kbf_alias_checksum(e)||true)')]:
  assert source.count(old)==1;run(name,source.replace(old,new),False)
 run('panic-gate',a.panic.read_text())
