"""Replay bounded observer/parser tests; all kernel and reset hardware is mocked."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--qualification', type=Path, required=True)
ap.add_argument('--cache', type=Path, required=True)
ap.add_argument('--source', type=Path, required=True, help='The exact replayed XNU tree, including host patches')
args = ap.parse_args()
q = args.qualification.resolve()
manifest = json.loads((q / 'all-mac-source-0435.json').read_text())['files']
source_paths = ('bsd/kern/kebab_bootdiag.c', 'osfmk/kern/kebab_bootdiag.c', 'bsd/dev/kebab_usb.c')
bodies = {}
for name in source_paths:
 data = (args.source / name).read_bytes()
 assert hashlib.sha256(data).hexdigest() == manifest[name], ('source mismatch', name)
 bodies[name] = data.decode()
strip = lambda text: re.sub(r'^#include [^\n]+\n', '', text, flags=re.M)
observer_body = strip(bodies[source_paths[1]]) + '\n' + strip(bodies[source_paths[0]]).replace('unsigned *count = arg;', 'unsigned *count = (unsigned *)arg;').replace('struct kebab_backboard_snapshot *snapshot = argument;', 'struct kebab_backboard_snapshot *snapshot = (struct kebab_backboard_snapshot *)argument;')
usb = bodies[source_paths[2]]
def function(name):
 at = usb.index('\n' + name + '(')
 start = usb.rfind('\nstatic ', 0, at) + 1
 end = usb.index('\n}\n', at) + 3
 return usb[start:end]
args.cache.mkdir(parents=True, exist_ok=True)
suite = [
 ('observer', 'backboard-observer-production-test.cpp', 'test-backboard-observer-result.json', [
  ('wrong-parent', 'proc_ppid(process) == 1', 'true'),
  ('wrong-name', '!strcmp(name, "backboardd")', 'true'),
  ('leak-task', 'task_deallocate(snapshot.task);', ';'),
  ('ignore-context', 'if (ml_at_interrupt_context() || get_preemption_level() != 0) return;', 'if(false) return;')]),
 ('parser', 'backboard-usb-production-test.c', 'test-backboard-usb-result.json', []),
 ('dispatch', 'backboard-dispatch-production-test.cpp', 'test-backboard-dispatch-result.json', [
  ('skip-panic-gate', 'if (panicking) {', 'if (panicking && false) {'),
  ('wrong-callback', '\t\tkebab_backboardd_report();', '\t\tkebab_pid1_report();')]),
]
with tempfile.TemporaryDirectory(prefix='backboard-tests-', dir=args.cache) as td:
 root = Path(td)
 for name, filename, receipt, mutations in suite:
  data = (q / filename).read_bytes()
  expected = json.loads((q / receipt).read_text())
  assert expected['passed'] and hashlib.sha256(data).hexdigest() == expected['fixture_sha256']
  source = data.decode()
  if name == 'observer':
   assert observer_body in source, 'observer fixture must contain both complete current source bodies'
  else:
   functions = ('ud_command_seen', 'ud_reboot_command') + (('ud_run_reboot_command',) if name == 'dispatch' else ())
   assert all(function(n) in source for n in functions), 'fixture must contain the complete current parser/dispatcher'
   assert all(line in source for line in re.findall(r'^#define UD_REBOOT_\w+\s+[^\n]+', usb, re.M))
  variants = [('actual', source, True)]
  for mutant, before, after in mutations:
   assert source.count(before) == 1, (name, mutant, 'mutation must match exactly once')
   variants.append((mutant, source.replace(before, after), False))
  for variant, text, passes in variants:
   path = root / (name + '-' + variant + Path(filename).suffix)
   path.write_text(text)
   exe = path.with_suffix('')
   cpp = path.suffix == '.cpp'
   command = ['clang++' if cpp else 'clang'] + (['-std=c++17'] if cpp else [])
   command += ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
               '-fsanitize=address,undefined', str(path), '-o', str(exe)]
   subprocess.run(command, check=True)
   result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
   assert (result.returncode == 0) == passes, (name, variant, result.stdout, result.stderr)
   if passes:
    print(result.stdout.strip())
   else:
    assert 'Assertion' in result.stderr, (name, variant, result.stderr)
    print('Rejected behavioral mutation:', name, variant)
print('PASS three source-derived suites and six behavioral mutants; hardware and kernel layouts explicitly mocked')
