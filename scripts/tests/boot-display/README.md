# Boot display ownership and power lifecycle tests

Use Python 3 and Clang against the actual patched XNU source tree:

```
python3 test-boot-art.py --source /path/to/xnu --receipt-dir /tmp/boot-display-tests
python3 test-early.py --source /path/to/xnu --receipt-dir /tmp/boot-display-tests
python3 test-scale.py --source /path/to/xnu --receipt-dir /tmp/boot-display-tests
python3 test-power-queue.py --source /path/to/xnu --receipt-dir /tmp/boot-display-tests
python3 test-logo-match.py --source /path/to/xnu --receipt-dir /tmp/boot-display-tests
```

The runners extract and compile the actual current function bodies. Original
progress alpha tables come from the supplied Apple OSS source tree. Logo and
device-tree test fixtures are synthetic: no original proprietary image, device
capture, hardware key or user data is included. ASan/UBSan and compiled mutants
exercise bounds, pixel/padding matching, privacy rejection, console scale,
early USB checkpoints, panic guards, work-loop ordering, actual drain before
off, concurrent queues, one-time public-logo retention and key cancellation.

DT, atomic operations, locks/work-loop, time, notification, framebuffer drawing
and physical input are explicitly limited host models. Native compilation and
real device captures/key tests are separate requirements. In particular, 0505
passed its first host cases yet physically faulted: generic memcpy used paired
64-bit stores at a 4-byte aligned device framebuffer address. 0384 uses volatile
32-bit pixels. Host sanitizers never prove ARM device-memory access safety.
