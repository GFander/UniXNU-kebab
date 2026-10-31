#!/usr/bin/env python3
"""Run the actual panic reset/dispatch/halt functions with mocked hardware."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

usb = Path(sys.argv[1]).read_text()
machine = Path(sys.argv[2]).read_text()


def function(source, name):
    match = re.search(r'(?:static )?(?:void|int|uint64_t)\n' + name + r'\([^\n]*\)\n\{', source)
    assert match, name
    end = source.index('\n}\n', match.end()) + 3
    return source[match.start():end] + '\n'


names = ['ud_platform_restart', 'ud_platform_restart2', 'ud_fastboot_restart',
         'ud_run_reboot_command']
functions = '\n'.join(function(usb, name) for name in names)
# Host execution tests control flow, not the ARM memory-barrier instruction.
functions = re.sub(r'__asm__ volatile \("dsb sy" ::: "memory"\);',
                   '__asm__ volatile ("" ::: "memory");', functions)
constants = '\n'.join(re.findall(r'^#define (?:UD_REBOOT_\w+|KEBAB_PSCI_SYSTEM_RESET|'
                                  r'KEBAB_IMEM_RESTART|KEBAB_IMEM_PA|'
                                  r'KEBAB_PSCI_RET_NOT_SUPPORTED|KEBAB_FWLOG_PANIC_MAX)\s+[^\n]+',
                                  usb, re.M))
dispatcher = function(usb, 'ud_run_reboot_command')
ignored = sorted(set(re.findall(r'\b((?:kebab_|ud_)\w+)\(', dispatcher)) - set(names) -
                 {'kebab_install_sync_for_reset', 'kebab_fwlog_report'})
stubs = '\n'.join('#define ' + name + '(...) (++other_calls)' for name in ignored)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#define KEBAB 1
#define TRUE 1
#define FALSE 0
#ifndef __dead2
#define __dead2 __attribute__((noreturn))
#endif
#define kPERestartCPU 1
#define kPEHaltCPU 2
typedef int boolean_t;
static struct {
    bool restart_armed, psci_reset2_valid, psci_probe_sane;
    uintptr_t imem;
    uint32_t psci_reset2;
    char inst_path[32];
} ud;
static uint32_t memory[2048];
static bool is_panic, writes_enabled, sync_ok, ready, expect_fastboot;
static int pon_rc, reset_rc, smc_calls, pe_calls, pon_calls, sync_calls, other_calls, fwlog_calls;
static int flush_calls, delays;
static jmp_buf halt_jump;
unsigned panic_active(void) { return is_panic; }
static bool kebab_spmi_pon_writes_enabled(void) { return writes_enabled; }
static int kebab_spmi_pon_reason_write(void) { assert(is_panic); ++pon_calls; return pon_rc; }
static int kebab_spmi_pon_reason_write_normal(void) { assert(!is_panic); ++pon_calls; return pon_rc; }
static uint32_t ud_fastboot_cookie(void) { return 0x77665500; }
static void check_reset(void) {
    if (expect_fastboot) assert(memory[KEBAB_IMEM_RESTART / 4] == 0x77665500);
    else assert(memory[KEBAB_IMEM_RESTART / 4] == 0x12345678);
}
uint64_t kebab_psci_call(uint64_t fn, uint64_t a, uint64_t b) {
    assert(fn == KEBAB_PSCI_SYSTEM_RESET && a == 0 && b == 0);
    assert(is_panic && ud.restart_armed);
    check_reset(); ++smc_calls; return (uint64_t)reset_rc;
}
static uint64_t kebab_psci_system_reset2(uint32_t a, uint32_t b) {
    assert(a == 0 && b == 0); check_reset(); ++smc_calls; return (uint64_t)reset_rc;
}
static int PEHaltRestart(unsigned type) {
    if (is_panic && !ud.restart_armed) longjmp(halt_jump, type + 10);
    assert(ud.restart_armed); check_reset(); ++pe_calls; return reset_rc;
}
static void kebab_usb_log_flush(void) { ++flush_calls; }
static void delay(unsigned n) { assert(n <= 300000); ++delays; }
static bool kebab_install_sync_for_reset(const char *why) {
    assert(!is_panic && why); ++sync_calls; return sync_ok;
}
static void kebab_fwlog_report(unsigned size) { (void)size; ++fwlog_calls; }
bool kebab_usb_console_ready(void) { return ready; }
bool kebab_usb_restart_armed(void) { return ud.restart_armed; }
void kebab_usb_panic_spin(void) __dead2;
void kebab_usb_panic_spin(void) { longjmp(halt_jump, 1); }
static void reset_test(void) {
    memset(&ud, 0, sizeof(ud)); memset(memory, 0, sizeof(memory));
    ud.imem = (uintptr_t)memory;
    memory[KEBAB_IMEM_RESTART / 4] = 0x12345678;
    writes_enabled = sync_ok = ready = true; is_panic = expect_fastboot = false;
    pon_rc = 0; reset_rc = -1;
    smc_calls = pe_calls = pon_calls = sync_calls = other_calls = fwlog_calls = 0;
    flush_calls = delays = 0;
}
'''
main = r'''
int main(void) {
    for (int panic = 0; panic < 2; ++panic) for (int result = 0; result < 2; ++result) {
        reset_test(); is_panic = panic; reset_rc = result ? -1 : 0; expect_fastboot = true;
        ud_run_reboot_command(UD_REBOOT_FASTBOOT, panic);
        assert(pon_calls == 1 && smc_calls == panic && pe_calls == !panic);
        assert(sync_calls == !panic && !ud.restart_armed);
        assert(memory[KEBAB_IMEM_RESTART / 4] == 0x12345678);
    }
    for (int failure = 0; failure < 3; ++failure) {
        reset_test(); is_panic = true;
        if (failure == 0) writes_enabled = false;
        if (failure == 1) ud.imem = 0;
        if (failure == 2) pon_rc = -1;
        ud_run_reboot_command(UD_REBOOT_FASTBOOT, TRUE);
        assert(smc_calls == 0 && pe_calls == 0 && sync_calls == 0);
        assert(memory[KEBAB_IMEM_RESTART / 4] == 0x12345678);
    }
    reset_test(); is_panic = true;
    ud_run_reboot_command(UD_REBOOT_PLAIN, TRUE);
    assert(smc_calls == 1 && pe_calls == 0 && pon_calls == 0 && sync_calls == 0);
    reset_test(); is_panic = expect_fastboot = true;
    ud_run_reboot_command(UD_REBOOT_FASTBOOT2, TRUE);
    assert(smc_calls == 0 && pon_calls == 0);
    ud.psci_reset2_valid = ud.psci_probe_sane = true; ud.psci_reset2 = 0;
    ud_run_reboot_command(UD_REBOOT_FASTBOOT2, TRUE);
    assert(smc_calls == 1 && pon_calls == 1 && sync_calls == 0);
    reset_test(); sync_ok = false;
    ud_run_reboot_command(UD_REBOOT_FASTBOOT, FALSE);
    assert(sync_calls == 1 && pon_calls == 0 && smc_calls == 0 && pe_calls == 0);
    reset_test(); is_panic = true;
    for (unsigned command=4; command<=UD_REBOOT_DRV_REPORT; command++) {
        if (command==UD_REBOOT_FWLOG_REPORT || command==UD_REBOOT_FWLOG_ALL) continue;
        ud_run_reboot_command(command, TRUE);
        assert(other_calls == 0 && sync_calls == 0);
    }
    ud_run_reboot_command(UD_REBOOT_FWLOG_ALL, TRUE);
    assert(fwlog_calls == 1);
    for (int reboot = 0; reboot < 2; ++reboot) {
        reset_test(); is_panic = true;
        int stopped = setjmp(halt_jump);
        if (!stopped) halt_all_cpus(reboot);
        assert(stopped == 1); // Early panic reaches USB without a platform object.
        reset_test(); is_panic = true; ready = false;
        stopped = setjmp(halt_jump);
        if (!stopped) halt_all_cpus(reboot);
        assert(stopped == (reboot ? kPERestartCPU : kPEHaltCPU) + 10);
    }
    puts("panic FASTBOOT target, no VFS/PE, failure paths, normal sync and early halt: PASS");
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='panic-fastboot-test-', dir=cache) as directory:
    path = Path(directory) / 'test.c'
    executable = Path(directory) / 'test'
    path.write_text(constants + '\n' + prefix + stubs + '\n' + functions +
                    function(machine, 'halt_all_cpus') + main)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
