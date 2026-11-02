"""Test real production scaler files from an already replayed XNU tree."""
from pathlib import Path
import argparse,shutil,subprocess,sys,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args();w=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='iosurface-scaler-') as tmp:
 t=Path(tmp)
 for name in ('KebabSoftwareScaler.cpp','KebabScalerCore.h','KebabPixelFormat.h'):shutil.copyfile(a.source/'iokit/Kernel/arm'/name,t/name)
 for name in ('test-scaler.py','test-client.py','original-abi-request-fixtures.json'):shutil.copyfile(w/name,t/name)
 for name in ('test-scaler.py','test-client.py'):subprocess.run([sys.executable,str(t/name)],check=True)
 print('PASS: actual CPU core, user-client lifetime/auth and original-interface fixtures; host models, not physical evidence')
