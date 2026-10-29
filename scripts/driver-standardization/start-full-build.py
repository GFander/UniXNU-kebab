"""Start an isolated full rebuild without fetching or resetting source."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

version=sys.argv[1]
assert len(version)==4 and version.isdigit()
assert sys.argv[2:] in ([],['--prepared-source'])
root=Path('/Users/albertlee/kebab-build').resolve()
source=root/'dxb'
assert source.resolve()==Path('/Users/albertlee/kebab-build/dxb')
for name in ('xnu.obj','xnu.sym'):
    path=source/'build'/name
    assert path.resolve().parent==source/'build' and not path.is_symlink()
    if path.exists():shutil.rmtree(path)
env=dict(os.environ)
env.update(PATH=str(Path.home()/'bin')+':'+str(Path.home()/'Library/Python/3.9/bin')+
           ':/opt/homebrew/bin:'+env['PATH'],MACOS_VERSION='13.5',
           MACHINE_CONFIG='KEBAB',KERNEL_CONFIG='DEVELOPMENT',
           KDKROOT=str(root/'KDKs/KDK_13.5_22G74.kdk'))
if sys.argv[2:]:
    # This builder uses CODEQL only as the switch that skips its Git patch loop.
    # Source was prepared and verified on Ubuntu; no CodeQL job is performed.
    env['CODEQL']='1'
log_path=root/('build-'+version+'-full.log')
if log_path.exists():
    attempt=1
    while (root/('build-'+version+'-attempt-'+str(attempt)+'.log')).exists():
        attempt+=1
    shutil.copy2(log_path,root/('build-'+version+'-attempt-'+str(attempt)+'.log'))
with log_path.open('wb') as log:
    process=subprocess.Popen(['caffeinate','-s','bash','build.sh'],cwd=source,env=env,
                             stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
(root/('build-'+version+'.pid')).write_text(str(process.pid)+'\n')
print('Full source build started',version,'PID',process.pid,flush=True)
