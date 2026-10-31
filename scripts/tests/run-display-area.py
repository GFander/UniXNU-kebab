"""Run exact current selector29 bodies against explicit output/Float32 models."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--qualification',type=Path,required=True);ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args()
q=a.qualification;receipt=json.loads((q/'test-display-area-result.json').read_text());assert receipt['passed']
manifest=json.loads((q/'all-mac-source-0438.json').read_text())['files'];sources={}
for name,expected in receipt['production_sources'].items():
 path='iokit/Kernel/arm/'+name;raw=(a.source/path).read_bytes();assert hashlib.sha256(raw).hexdigest()==expected==manifest[path];sources[name]=raw.decode()
cpp=sources['KebabFramebuffer.cpp'];header=sources['KebabFramebufferGeometry.h']
start=cpp.index('static IOReturn\nkebab_fb_struct_out(');end=cpp.index('\n/* Call shapes',start);output=cpp[start:end]
start=cpp.index('\tcase kKebabFBGetDisplayArea: {');end=cpp.index('\tcase kKebabFBVideoPowerSavings:',start);branch=cpp[start:end]
assert re.search(r'#define KEBAB_FB_DOT_PITCH\s+458\b',cpp)
raw=(q/'display-area-production-test.cpp').read_bytes();s=raw.decode();assert hashlib.sha256(raw).hexdigest()==receipt['fixture_sha256']
assert all(body in s for body in(header,output,branch)),'Fixture must contain full current converter, case and output helper'
mutations=[('old16B-case','uint32_t area[2];','uint32_t area[4];'),('swap-order','output[0] = kebab_fb_ratio_f32(height, pitch);','output[0] = kebab_fb_ratio_f32(width, pitch);'),('truncate-rounding','if (remainder * 2 > b || (remainder * 2 == b && (significand & 1)))','if (false && (remainder * 2 > b || (remainder * 2 == b && (significand & 1))))'),('false-success-invalid','pitch > (1U << 24)) return false;','pitch > (1U << 24)) return true;'),('ignore-descriptor-error','if (args->structureOutputDescriptor->writeBytes(0, buf, len) != len)','if (args->structureOutputDescriptor->writeBytes(0, buf, len) != len && false)')]
variants=[('actual',s,True)]
for name,before,after in mutations:
 assert s.count(before)==1,(name,s.count(before));variants.append((name,s.replace(before,after),False))
a.cache.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='display-area-',dir=a.cache)as td:
 for name,text,expected in variants:
  p=Path(td)/(name+'.cpp');p.write_text(text);exe=p.with_suffix('')
  subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
  assert(result.returncode==0)==expected,(name,result.stdout,result.stderr)
  if expected:print(result.stdout.strip())
  else:assert 'Assertion'in result.stderr;print('Rejected behavioral mutation',name)
print('PASS current selector29 source,100280 numeric cases and five behavioral mutants; descriptors explicitly modeled')
