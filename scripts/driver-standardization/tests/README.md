# Driver standardization tests

These tests compile and execute production function bodies or the production
contract header. Hardware, allocation and workloop stand-ins permit failure and
concurrency injection; they do not qualify the donor private userland ABI.

On Ubuntu, set `TMPDIR` to a writable directory with room for a C++ sanitizer
binary. Each test takes the actual patched source/header as its single argument:

```sh
export TMPDIR="$HOME/.cache"
python3 test-driver-personalities.py /home/albert/s4/x/iokit/KernelConfigTables.cpp
python3 test-user-client-contract.py /home/albert/s4/x/iokit/Kernel/arm/KebabUserClientContract.h
python3 test-framebuffer-lifecycle.py /home/albert/s4/x/iokit/Kernel/arm/KebabFramebuffer.cpp
python3 test-surface-ownership.py /home/albert/s4/x/iokit/Kernel/arm/KebabSurface.cpp
python3 test-blockstorage-contracts.py /home/albert/s4/x/iokit/Kernel/arm/KebabBlockStorageDevice.cpp
python3 test-blockstorage-publication.py /home/albert/s4/x/iokit/Kernel/arm/KebabBlockStorageDevice.cpp
```

Actual modified translation units were additionally compiled with the recorded
KDK commands, followed by full XNU rebuilds. Boot0192 enabled the default-off
`kebab_driver_test=1` once: real Surface/AppleCLCD factories, output validation
and close, and real UFS NXSB/completion/short/bounds/read-only tests passed.
Boot0193 uses the same native kernel with this diagnostic disabled; root UUID,
launchd/dyld hashes, ordinary FASTBOOT and panic FASTBOOT have device evidence.

`../audit-storage-abi.py` accepts `--prelink`, `--bindings`, `--header`,
`--module-path` (containing native_prelink.py), and `--output`. It pins this
project's iPhone10,3/20H392 storage executable, 10 absolute bindings and 22
compiled bridge slots. It deliberately refuses another donor. Static ABI
checks do not substitute for device tests or class-size qualification.
