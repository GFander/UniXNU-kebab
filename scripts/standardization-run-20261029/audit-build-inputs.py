"""Inventory the actual Mac builder and installed dependency inputs, no Git."""
from pathlib import Path
import hashlib,json,subprocess,sys
r=Path('/Users/albertlee/kebab-build');d=r/'dxb'
def h(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cmd(args):return subprocess.run(args,text=True,capture_output=True,check=True).stdout.strip()
files={}
for p in (d/'fakeroot').rglob('*'):
    if p.is_file() and p.suffix in ('.h','.defs','.a','.o','.dylib','.plist'):
        files[str(p.relative_to(d))]={'sha256':h(p),'bytes':p.stat().st_size}
for p in [d/'build.sh',r/'start-full-build.py']+list((d/'patches').rglob('*.patch')):
    if p.is_file():files[str(p.relative_to(r))]={'sha256':h(p),'bytes':p.stat().st_size}
report={'scope':'Actual installed fakeroot headers, definitions, archives, objects and dylibs; builder/host patches. Not a complete Xcode/KDK image pin.',
 'compiler':cmd(['xcrun','clang','--version']),'xcode':cmd(['xcodebuild','-version']),
 'sdk_path':cmd(['xcrun','--sdk','macosx','--show-sdk-path']),
 'sdk_version':cmd(['xcrun','--sdk','macosx','--show-sdk-version']),
 'kdk':'/Users/albertlee/kebab-build/KDKs/KDK_13.5_22G74.kdk',
 'target':'DEVELOPMENT ARM64 KEBAB / 16KiB / XNU8796.141.3', 'files':files}
out=r/('build-inputs-'+sys.argv[1]+'.json');out.write_text(json.dumps(report,indent=2)+'\n')
print('Recorded',len(files),'explicit build dependency files in',out)
