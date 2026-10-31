# Data restore and original phase 2: physically qualified

2026-10-30; final physical diagnostics 0348–0351. No GPU, display or input development.

The current requested milestone is complete: original Data contents are restored into the existing native encrypted Data volume; original `/sbin/mount` completes phase 2; original launchd continues through `init-with-data-volume`, `MSUEarlyBootTask`, FIPS and reaches `keybag`.

## Upstream reference applied

Pinned UniXNU-sheng `08cde20edc3254ed6f7df8d0f5384fb887786e8f` and UniXNU core `f0721e9745c45ad2600cd69c74730f1cca55d56a`. The core's `prepare-owner-data-fixture.py` restores initial var; `zzy-staged-system-apps.patch` performs a one-time staged-app copy through the covered original vnode, opens compressed sources before reading, fsyncs each file and avoids recursive APFS walks. Persistent Data omits `ephemeral-storage`, as our existing ADT already does.

Adaptation preserves our original signed APFS-created encrypted Data and hardware-root-backed factory C/D bank. No host-formatted seed, setup-completion preferences, fake user keybags, calibration blobs or keybagd bypass were imported. See [comparison](boot123-20261029/UPSTREAM-DATA-COMPARISON-0348.md) for pinned source links and differences.

## Physical evidence

- 0348: 9,624 fixed entries, including 9,614 original var entries and 10 donor-derived empty Hardware layout directories; 511,467,595 regular logical bytes. Original UID/GID/mode/symlinks and logical file hashes checked. Source compression wrappers authenticated and normalized to uncompressed destination data. Class D is selected at native file creation; each file fsynced and atomically renamed. Final filesystem/UFS sync both 0. Completion marker written after success.
- 0349: complete reboot and cold read of every installed template entry, all storage read-only; all checks pass. This is a reset/cold-mount test, not a separate physical power-cut test.
- 0350: exact completion marker and Hardware layout check, Data updated to RW preserving MNT_CPROTECT, System/Preboot RO, original launchd handoff. Phase 2 completes and four later original boot tasks are observed. FactoryData/Pearl ENOENT is gone; both original bind mounts are subsequently observed during shutdown. Original Data unmount completes.
- 0351: original signed `fsck_apfs -n -l /dev/disk1s2`, exit 0, crypto-io enabled, Data appears OK; no repair and no unexpected warning. Only the known APFS 2236-vs-2142 container-version/overallocation messages remain. Immutable OS/SystemOS full-content evidence remains 0341; these daily boots use fast mode.
- Across these runs, old C/D proof plaintext and four raw ciphertext stripes match 0343; Preboot nonce/Root/device-bank records are preserved.

Original iOS16 mount selects Data by boot container and role `0x40`. Although APFS reports null volume groups, original phase 2 succeeds. A macOS-style volume group was not imposed and no immutable System metadata was changed.

## Build and limits

Formal patch 0269; 3 exact KDK TUs compile; clean full build. 248 common + 4 host patches, all 5,230 source files equal under strict replay. Actual boot policy executes 94 mutation cases under ASAN/UBSAN. Native SHA256 `c350aa716e27b283d74f6d6986caf63d41901e99d640a28099778ebb8018764a`, 18,785,064 bytes. Original prelink text/trust/ABI and image readback gates pass; every flash has Sending/Writing OKAY.

This qualifies factory no-passcode C/D storage, not Apple SEP, passcode A/B protection, system keybag availability or full iOS boot. The next original blocker is:

```
MKB_INIT: FATAL KEYBAG ERROR: failed to load system bag
REBOOTING INTO RECOVERY MODE.
```

Original keybagd requests recovery; launchd later exits. Do not bypass this or call the APFS FS/FV key services a complete AppleKeyStore user-client. Investigate the actual original MobileKeyBag/AppleKeyStore system-bag path and upstream contracts next. The current init-exit callback mislabels tool12 as `DATA FORMAT`; this is a diagnostic-only known issue, and no formatter ran in 0350.

Daily startup does not restore or verify all 511 MB again. Full checks are explicit restore/cold qualification profiles; normal development startup checks the completion identity and small Hardware layout.

Machine-readable receipts: `boot123-20261029/0348-data-template-restore.json`, `0349-data-template-cold.json`, `0350-data-phase2-accepted.json`, `0351-full-data-fsck.json`. Raw logs and capture provenance accompany each. Current scope is complete; SpringBoard is not reached.
