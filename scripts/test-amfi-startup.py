#!/usr/bin/env python3
"""Exercise actual fallback registration without replacing an existing provider."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
function = source.split('kebab_amfi_register(void)\n', 1)[1].split('\n}\n', 1)[0] + '\n}\n'
if len(sys.argv) > 2:
    startup = Path(sys.argv[2]).read_text()
    assert startup.index('mac_policy_initmach();') < startup.index('kebab_amfi_register();')
    assert startup.index('kebab_amfi_register();') < startup.index('trust_cache_runtime_init();')
    assert startup.index('kebab_amfi_register();') < startup.index('machine_lockdown();')
    assert 'STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE, kebab_amfi_register)' not in source
prefix = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef struct { struct { unsigned version; } TrustCache; } amfi_t;
static const amfi_t kebab_amfi = {{0}}, donor = {{3}};
static const amfi_t *amfi;
static unsigned registrations;
static void amfi_interface_register(const amfi_t *provider) {
    assert(amfi == NULL);
    amfi = provider;
    ++registrations;
}
'''
main = r'''
int main(void) {
    kebab_amfi_register();
    assert(amfi == &kebab_amfi && registrations == 1);
    amfi = &donor;
    kebab_amfi_register();
    assert(amfi == &donor && registrations == 1 && amfi->TrustCache.version == 3);
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kebab-amfi-test-', dir=cache) as directory:
    harness = Path(directory) / 'amfi.c'
    executable = Path(directory) / 'amfi'
    harness.write_text(prefix + '\nstatic void\nkebab_amfi_register(void)\n' + function + main)
    subprocess.run(['clang', '-Wall', '-Wextra', str(harness), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('standalone fallback and existing AMFI provider preservation: PASS')
