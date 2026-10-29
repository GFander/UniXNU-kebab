#!/usr/bin/env python3
"""Check the real early USB entry's console/KDP boot-argument combinations."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
function = source.split('void\nkebab_usb_kdp_start(void)\n', 1)[1].split('\n}\n', 1)[0] + '\n}\n'
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
static struct { bool running, console_input; } ud;
static bool kdp, present, driver_ok;
static uint32_t early;
static int starts;
static bool kebab_usb_kdp_enabled(void) { return kdp; }
static bool PE_parse_boot_argn(const char *key, void *out, unsigned size) {
    assert(!strcmp(key, "kebabusb_early") && size == sizeof(early));
    if (present) memcpy(out, &early, size);
    return present;
}
static void kebab_usb_start(void) { ++starts; ud.running = driver_ok; }
'''
main = r'''
int main(void) {
    for (int mode = 0; mode < 3; ++mode) for (int debugger = 0; debugger < 2; ++debugger)
        for (int ready = 0; ready < 2; ++ready) {
            kdp = debugger; present = mode != 0; early = mode == 2; driver_ok = ready;
            starts = 0; ud.running = ud.console_input = false;
            kebab_usb_kdp_start();
            assert(starts == (debugger || mode == 2));
            assert(ud.console_input == (starts && ready && debugger));
        }
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kebab-usb-test-', dir=cache) as directory:
    harness = Path(directory) / 'usb.c'
    executable = Path(directory) / 'usb'
    harness.write_text(prefix + '\nvoid\nkebab_usb_kdp_start(void)\n' + function + main)
    subprocess.run(['clang', '-Wall', '-Wextra', str(harness), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('12 early-console/KDP/driver-state combinations: PASS')
