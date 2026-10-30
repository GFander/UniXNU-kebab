"""Run the public production-function snapshot; explicit IOKit/crypto mocks."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--compiler',default='clang++');a=p.parse_args()
src=a.source.read_text()
with tempfile.TemporaryDirectory(prefix='kebab-aks-notify-',dir=a.cache) as tmp:
 root=Path(tmp)
 env=dict(os.environ,TMPDIR=str(root))
 variants=[('original',src,True)]
 for name,old,new in [('query-always-zero','a->scalarOutput[0]=state?1:0','(void)state;a->scalarOutput[0]=0'),('query-always-one','a->scalarOutput[0]=state?1:0','(void)state;a->scalarOutput[0]=1'),('set-does-nothing','(void)OSCompareAndSwap(0,1,&notificationState);','(void)OSCompareAndSwap(0,0,&notificationState);')]:
  assert src.count(old)==1;variants.append((name,src.replace(old,new),False))
 for name,text,expected in variants:
  cpp=root/(name+'.cpp');exe=root/name;cpp.write_text(text)
  subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True,env=env)
  q=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
  assert (q.returncode==0)==expected,(name,q.stdout,q.stderr)
  if expected:print(q.stdout.strip())
  else:
   assert 'Assertion' in q.stderr and 'failed' in q.stderr,(name,q.stderr)
   print(name+': rejected by behavioral assertion')
