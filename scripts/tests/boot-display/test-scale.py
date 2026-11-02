from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--receipt-dir',type=Path,required=True);options=parser.parse_args();options.receipt_dir.mkdir(parents=True,exist_ok=True);w=options.receipt_dir
from pathlib import Path
import hashlib,json,subprocess,tempfile
p=options.source/'osfmk/console/video_console.c';s=p.read_text();a=s.index('\t\t\tunsigned int scale   =');b=s.index('\n\t\t}\n\t\tnew_vinfo.v_name',a);body=s[a:b]
code='''#include <assert.h>
#define KEBAB 1
#define kPEScaleFactor1x 1
#define kPEScaleFactor2x 2
struct video {unsigned v_scale,v_width,v_height,v_depth;};
static unsigned run(unsigned input,unsigned width,unsigned height,unsigned depth){struct video v={input,width,height,depth};struct video *boot_vinfo=&v;struct video new_vinfo=v;
'''+body+'''
return new_vinfo.v_scale;}
int main(){assert(run(0,1080,2400,32)==1);assert(run(1,1080,2400,32)==1);assert(run(2,1080,2400,32)==2);assert(run(3,1080,2400,32)==3);assert(run(4,1080,2400,32)==1);assert(run(3,1125,2436,32)==1);assert(run(3,1080,2400,16)==1);assert(run(3,1080,2401,32)==1);return 0;}
'''
with tempfile.TemporaryDirectory(prefix='console-scale-') as tmp:
 t=Path(tmp);src=t/'test.c';exe=t/'test';command=['clang','-std=c11','-O1','-fsanitize=address,undefined',str(src),'-o',str(exe)]
 src.write_text(code);subprocess.run(command,check=True,capture_output=True);subprocess.run([str(exe)],check=True,capture_output=True)
 src.write_text(code.replace('new_vinfo.v_scale = 3;', 'new_vinfo.v_scale = 1;'));subprocess.run(command,check=True,capture_output=True);assert subprocess.run([str(exe)],capture_output=True).returncode!=0
(w/'host-scale-tests.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),actual_initialize_screen_scale_conversion=True,cases=8,original_fallback_compiled_runtime_rejected=True,asan_ubsan=True,physical_scale_not_yet_measured=True),indent=2)+'\n')
print('PASS8 actual PE to console scale cases; original fallback mutant rejected at runtime')
