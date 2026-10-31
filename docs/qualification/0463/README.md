# 0463: crypto foundation and original AKS metadata

This is a full native build and normal boot qualification of the math self-test
and bounded metadata observer. It does not implement class B file keys, original
userland wrap/unwrap, new entitlements, keychain or a usable SpringBoard.

See [scope, evidence and limitations](../../CLASS-B-CRYPTO-20261031.md),
[runtime receipt](0463-qualified-runtime.json) and [sealed log](0463-media-boot.log).
The component-only compiler receipt was taken before the separate whole native
build; its historical `active_native_unchanged=0462` is not the final device state.

The [portable verifier](../../../scripts/tests/run-class-b-crypto.py) requires a
replayed 0463 XNU source tree, Git, clang/clang++, and OpenSSL development headers.
It checks actual production code, public test-only vectors, nine behavioral
mutants, complete client fixtures, reverse patch restoration and sealed evidence.
IOKit, locks and backend contexts in host tests are explicit models. Native build
and physical device receipts are separate; rerunning a host test is not a new
device test. Original binaries and complete disassembly are not published.
