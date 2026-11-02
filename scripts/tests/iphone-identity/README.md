# Measured iPhone software identity tests

Run Python 3, Git and Clang against this repository and the exact replayed XNU:

```
python3 test-generator.py --repo /path/to/UniXNU-kebab --receipt-dir /tmp/identity-tests
python3 test-platform.py --source /path/to/xnu --receipt-dir /tmp/identity-tests
python3 test-runtime-observers.py --source /path/to/xnu --receipt-dir /tmp/identity-tests
```

The generator runner reads the pinned public baseline from Git without network
access. It executes the actual current generator, checks explicit profile
failures and mutually exclusive modes, preserves default/iPad behavior, verifies
typed D22/phone/3x/HomeButtonType2 traits, codec roundtrips, idempotence, and every
unrelated hardware/chosen property. Its default fixture is generated and
synthetic. Optionally pass `--measured-adt /private/path/runtime.adt` to test a
locally licensed real payload; neither its bytes nor file path enter receipts.

The native-body runners compile actual current platform and diagnostic helpers
with ASan/UBSan, model OSData/user-map/atomic operations, and require regression
mutants to compile and fail at runtime. They do not invoke MobileGestalt, modify
user processes, establish physical operation or replace real boot/UI/gesture
acceptance. An uninitialized or changing original getter must remain unknown.
No proprietary binary, original image, wire capture, device secret or Data
content is distributed with these tests.
