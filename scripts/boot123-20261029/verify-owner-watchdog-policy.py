"""Fail closed before flashing an owner-mapped normal watchdog profile."""
from pathlib import Path
import sys,json,importlib.util,hashlib
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';v=sys.argv[1]
spec=importlib.util.spec_from_file_location('policy',r/'adapt-watchdog-sandbox-policy.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
original=b/('prelink-'+v+'-unadapted.bin');target=b/('prelink-'+v+'.bin')
image,report=p.adapt(original.read_bytes(),json.loads(original.with_suffix('.json').read_text()))
assert image==target.read_bytes() and report==json.loads(target.with_suffix('.json').read_text())
abi=json.loads((r/(v+'-watchdog-abi.json')).read_text());assert abi['native_class']=='KebabPlatformWatchdog'
assert abi['native_sha256']==hashlib.sha256((b/('kernel-'+v+'.kebab')).read_bytes()).hexdigest()
assert json.loads((r/'test-sandbox-platform-policy-result.json').read_text())['passed']
test=json.loads((r/'test-platform-watchdog-result.json').read_text());assert test['passed'] and test['cases']==30 and test['mutation_refusals']==3
print('PASS owner watchdog policy: independently replayed exact21 RO mapping; native real class; original code and MACF returns preserved')
