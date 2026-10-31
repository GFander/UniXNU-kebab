# Original keybagd bootstrap qualified, 0354–0356

2026-10-30. The unchanged original iOS16.7.16/20H392 keybagd completes `MKB_INIT` on hardware in 0354 and again after complete reset in 0355. Original launchd proceeds to `usermanagerd` and later tasks. GPU, display and input development has not started.

## Implemented path

Native `/IOResources/AppleKeyStore` and `AppleKeyStoreUserClient` now provide the audited original user-client ABI: handshake 0, sealed-bank serialization 3, factory lock-state query 7, system-bag handle query 14, authenticated Data mapping 65, and cookie 68. Root privilege, argument counts, descriptor sizes, supported handle/context, exact volume identity and backend readiness are checked. Unknown operations return errors. Filesystem FS/FV resources continue through the existing qualified provider.

Handle 0 represents the actual existing authenticated factory C/D bank, not an empty offline bag. The sealed 4096-byte record is retained only after its hardware-root-backed authentication succeeds; failed authentication and Root cancellation wipe this cached copy. Export never returns raw class keys.

The 34-byte backend-specific cookie authenticates the actual bank ID, container/System identities and factory context under a separate HMAC purpose derived from the restored hardware Root. The original keybagd writes and reads this opaque cookie using original `APFSVolumePayloadSet/Get`. Mapping verifies its MAC and exact Data UUID, then calls the real existing factory C/D activation path. Root/bank keys are not regenerated.

The missing `/private/var/keybags/systembag.kb` is initialized through native VFS with the audited original no-effaceable-storage plist envelope: `KeyBagKeys` CFData containing the real sealed backend record, and `KeyBagVersion` CFString `1`. The payload remains our provider-specific encrypted bank format; it is not represented as an Apple SEP blob. The 4,182-byte file is class D, root-owned 0600, exclusively staged, read back, fsynced, renamed and synchronized to UFS. Existing records must match the authenticated backend; they are never silently replaced. Original keybagd/CoreFoundation then reads this envelope itself.

The original binaries, original trust-cache bundle and read-only System/Preboot are preserved. No `-apfs_shared_datavolume`, ephemeral-storage flag, keybagd patch or skip-init path is used. Normal development boots authenticate this small existing record; they do not restore or verify all 511 MB of Data.

## Physical evidence

- 0352: original connection/handshake and system-bag query succeed; the former load-system-bag failure moves to missing cookie/map operations.
- 0353: original cookie retrieval, APFS payload write and authenticated volume mapping succeed. Original output reports `System Keybag loaded`; the next failure is the missing disk plist.
- 0354: real disk-envelope initialization passes; the original program reads the persisted cookie without regenerating it, maps Data, and prints `DONE MKB_INIT`. Launchd continues into user management and later boot tasks.
- 0355: existing disk envelope authenticates byte-for-byte against the same real bank after complete reset; no recreation or cookie rewrite occurs. Original `DONE MKB_INIT` and later tasks appear again. This is reset/cold-mount validation, not a separate physical power-cut test.
- All runs preserve the old C/D proof plaintext and four raw ciphertext stripes from 0343, plus the authenticated Preboot nonce/Root/device-bank records.
- 0356: original signed `fsck_apfs -n -l /dev/disk1s2` returns 0, crypto-io is enabled and the volume appears OK. It performs no repairs. **The strict warning gate remains failed** because of one new calendar warning; see below.

The actual observed user-client calls qualify 0/7/14/65/68. Selector 3 marshaling is compile/host-tested, and its real sealed-bank export core is exercised by native disk initialization; an original userland selector-3 call was not observed in these boots.

## Clock warning and remaining work

The fsck warning says Data last modification is 75.794121562 seconds after epoch, while the next boot's current clock is 52.10505 seconds. Previous 0355 panic calendar is 75.89579 seconds, only 0.101668438 seconds after that write. Both boots still use a 1970 calendar. This supports an uninitialized/reset boot-calendar diagnosis, rather than a new tree/encryption failure.

Raw warnings, the strict failure and both timestamp receipts are preserved. No timestamp was rewritten and the warning was not silently allowlisted. A persistent/current wall-clock initialization path remains open.

The later repeatable boot blocker is `watchdogd` exiting (namespace 2, subcode 5), followed by panic; its causal diagnosis remains open. Post-init clients also request unsupported selectors 17, 102 and 103. Full AppleKeyStore/keychain compatibility, additional bag/users/passcodes, Apple SEP and A/B class protection are not qualified. Current policy remains factory no-passcode C/D. SpringBoard has not been qualified.

## Source and validation

Upstream reference: [AppleKeyStoreState, pinned 42ff83e](https://github.com/UniXNU/AppleKeyStoreState/blob/42ff83e08b2265fe741d4adaf084bebe003452ba/NativeKeyStoreEvents.cpp). It explicitly supplies offline state-only behavior; its selector conventions informed this implementation. The no-system-keybag route in [UniXNU's probe](https://github.com/UniXNU/UniXNU/blob/f0721e9745c45ad2600cd69c74730f1cca55d56a/scripts/probe-keybag-bypass.py) was inspected and not adopted.

Original keybagd SHA256: `747271703b7aff98420ed23788a362716025506d8d0ba8c703fb78e5b3ec1b9b`. Local original disassembly pins the actual selected iOS16 ABI, including AKS count/length/padded field packets, which are not DER.

Formal patches 0270–0272. Exact KDK translation-unit compiles and clean full builds pass. Final strict source replay: 251 common + 4 host patches, all 5,234 source files equal. Native SHA256 `8ab5f53160e598bb1b222b1ae746d2d46d7f2f05215abb0faf39ba91a9419475`, 18,824,440 bytes. Original prelink text/trust/storage/provider ABI and FAT/ADT readback checks pass. Flashing has Sending/Writing OKAY each time.

8,480 actual sealed-bank/reference/tamper/AES-fault/revocation/cookie cases pass under ASAN/UBSAN, including an independent HMAC reference. The isolated bank test mocks volume-registry invalidation. 328 actual selector/access/marshaling/refusal cases pass with explicit IOKit/export/cookie/activation stand-ins; these tests do not themselves prove hardware. Apple `plutil` accepts the original envelope shape. Hardware observations are recorded separately above.

Receipts and raw-log provenance are in `boot123-20261029/`: `0352-systembag-protocol-observed.json`, `0353-systembag-cookie-observed.json`, `0354-systembag-qualified.json`, `0355-systembag-qualified.json`, `0356-full-data-fsck.json`, `0356-boot-calendar-warning-analysis.json`, and `SYSTEMBAG-ACCEPTED-0356.json`.
