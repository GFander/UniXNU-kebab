"""Run actual board/backend snapshots with explicit MMIO stand-ins."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--panic',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--compiler',default='clang++');a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='kebab-bt-clock-',dir=a.cache) as tmp:
    root=Path(tmp);env=dict(os.environ,TMPDIR=str(root))
    def run(name,text,passed):
        cpp=root/(name+'.cpp');exe=root/name;cpp.write_text(text)
        subprocess.run([a.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True,env=env)
        q=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=30)
        assert (q.returncode==0)==passed,(name,q.stdout,q.stderr)
        if passed:print(q.stdout.strip())
        else:
            assert 'Assertion' in q.stderr and 'failed' in q.stderr,(name,q.stderr)
            print(name+': rejected by behavioral assertion')
    source=a.source.read_text();run('actual-backend',source,True)
    for name,old,new in [
        ('clock-vote-gate','(vote&KBH_REQUIRED_VOTES)!=KBH_REQUIRED_VOTES','((vote&KBH_REQUIRED_VOTES)!=KBH_REQUIRED_VOTES&&false)'),
        ('uart-protocol','if(((out->se[0]>>8)&0xff)!=2)','if((((out->se[0]>>8)&0xff)!=2)&&false)'),
        ('wrong-tile','physical=0x0f900000+base','physical=0x0f100000+base'),
        ('panic-gate','if(panic_active()||ml_at_interrupt_context()','if((panic_active()&&false)||ml_at_interrupt_context()'),
        ('rollback-unrelated-votes','current&~out->added','out->before'),
        ('poll-bound','i<200','i<201'),
        ('touch-fifo-word-units','depth = kebab_geni_fifo_depth_words(kt_se_rd(SE_HW_PARAM_0));','depth = kebab_geni_fifo_width_bits(kt_se_rd(SE_HW_PARAM_0));')]:
        assert source.count(old)>=1;run(name,source.replace(old,new),False)
    run('complete-usb-dispatcher',a.panic.read_text(),True)
