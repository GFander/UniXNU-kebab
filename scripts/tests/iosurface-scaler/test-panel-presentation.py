"""Exercise actual production presentation, with explicit IOKit/console models.
No donor binary, private pixels or device access is needed.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args();here=Path(__file__).resolve().parent
source=a.source/'iokit/Kernel/arm';s=(source/'KebabFramebuffer.cpp').read_text();start=s.index('static void\nkebab_fb_handoff_console(');end=s.index('\nextern "C" void kebab_framebuffer_report',start);body=s[start:end]
prefix=(here/'panel-model-prefix.cpp.inc').read_text();main=(here/'panel-model-cases.cpp.inc').read_text()
mutants=[('actual',body,True),('wrong-wide-step',body.replace('ix * bpe','ix * 4'),False),('drop-clipping',body.replace('*right = kebab_fb_panel_edge(x + w, pw, lw);','*right = pw;'),False),('centre-crop',body.replace('uint32_t ix = sx + kebab_fb_sample(x, fWidth, fLogicalW, dx, w, sw);','uint32_t ix = sx + x + (fLogicalW-fWidth)/2;'),False),('skip-source-extent',body.replace('(uint64_t)sx + sw <= s->width','true'),False)]
with tempfile.TemporaryDirectory(prefix='panel-present-')as directory:
 d=Path(directory)
 for name,b,good in mutants:
  assert good or b!=body,name
  cpp=d/(name+'.cpp');exe=d/name;cpp.write_text(prefix+b+main)
  subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I',str(source),str(cpp),'-o',str(exe)],check=True,capture_output=True)
  q=subprocess.run([str(exe)],capture_output=True,text=True);assert(q.returncode==0)==good,(name,q.stdout,q.stderr[-2000:])
print(json.dumps(dict(passed=True,actual_present_cases=21,compiled_runtime_mutants_rejected=4,source_sha256=hashlib.sha256(s.encode()).hexdigest(),IOKit_and_console_are_host_models=True,device_memory_alignment_not_host_proven=True)))
