"""Exercise the actual fallback startup entry for both provider orders."""
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit('usage: test-prng-startup.py PATH/TO/osfmk/prng/prng_random.c')
source = Path(sys.argv[1]).read_text()
startup = source.split('static void\nnegroni_prng_startup(void)', 1)[1].split('\nSTARTUP(', 1)[0]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define KEBAB 1
#define kprintf printf
typedef void (*func)(void);
struct cckprng_funcs {func init, initgen, reseed, refresh, generate, init_with_getentropy;};
static int prng_ready, seed_calls, register_calls;
static int negroni_prng_ctx;
static void negroni_prng_init(void) {}
static void negroni_prng_initgen(void) {}
static void negroni_prng_reseed(void) {}
static void negroni_prng_refresh(void) {}
static void negroni_prng_generate(void) {}
static void negroni_prng_init_with_getentropy(void) {}
static uint64_t early_random(void) { ++seed_calls; return 1; }
static void register_and_init_prng(int *ctx, const struct cckprng_funcs *funcs) {
    assert(!prng_ready && ctx == &negroni_prng_ctx);
    assert(funcs->generate == negroni_prng_generate);
    ++register_calls;
    prng_ready = 1;
}
'''
main = r'''
int main(void) {
    /* Standalone kernel: fallback provides the first and only registration. */
    negroni_prng_startup();
    assert(prng_ready && seed_calls == 1 && register_calls == 1);
    /* Prelinked kernel: PE_init_iokit already installed the real provider. */
    seed_calls = register_calls = 0;
    negroni_prng_startup();
    assert(prng_ready && seed_calls == 0 && register_calls == 0);
    puts("standalone fallback and existing provider preservation: PASS");
}
'''
cache = Path.home() / '.cache'
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kebab-prng-test-', dir=cache) as directory:
    harness = Path(directory) / 'startup.c'
    executable = Path(directory) / 'startup'
    harness.write_text(prefix + '\nstatic void\nnegroni_prng_startup(void)' + startup + main)
    subprocess.run(['clang', '-Wall', '-Wextra', str(harness), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
