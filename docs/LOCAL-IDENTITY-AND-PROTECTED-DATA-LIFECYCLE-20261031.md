# Original iOS identity and protected Data lifecycle

Status: 0459 passed the physical storage/security/watchdog and normal diagnostic
qualification below. Installation, migration and usable SpringBoard remain
incomplete.

The original installation coordinator was waiting in a DirectoryService-style
identity resolver that this iOS userspace does not provide. KEBAB inherited
`config_ext_resolver` from its macOS filesystem feature list. The board now uses
Apple's existing local UID/GID/GUID and POSIX group implementation, matching the
unmodified donor iOS kernel. Apple ACL evaluation, MACF, protection, auditing,
permissions, and unsupported-identity errors remain in place.

## Direct evidence and source scope

- 0454 adds the fixed original `installcoordinationd` receiver to normal `MIG?`.
  Full names, parent PID 1, actual UID, referenced task/map/thread lifetimes,
  bounded user/kernel frames, and the existing seven no-follow metadata reads
  remain enforced. This is an observer, not arbitrary task or memory access.
- Two physical receiver samples showed original cancellation/purge and then
  placeholder materialization/rename waiting in `kauth_acl_evaluate`, credential
  GUID lookup, and `kauth_resolver_submit`. This replaces the earlier inference
  from a caller's synchronous reply wait with actual receiver evidence.
- 0340 removes only `config_ext_resolver` from `config/MASTER.arm64.kebab`.
  The actual Apple credential, authorization and `kauth.h` files are unchanged.
  Native preprocessing reports resolver 0, MACF/protection/audit/CSR 1; the
  external resolver entry point is absent while native ACL and identity entry
  points remain linked.
- The actual unmodified donor ARM64 UID/GID conversion routines and their real
  `memcmp` executed 6,400 emulated cases. No identity or library return was
  stubbed. This is independent emulation evidence, not physical execution.
  The complete unchanged native Apple functions also passed 10,475 modeled
  identity/group/ACL cases and 11 compiled behavioral mutation refusals.

The primary source is Apple's
[xnu-8796.141.3 credential implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.141.3/bsd/kern/kern_credential.c).
Unknown translations retain `ENOENT`; ACL outcomes retain allow/deny/defer and
ordinary POSIX handling. Reserved well-known group GUIDs retain their original
semantics. This does not authorize every UID, group or ACL.

## Serial observations and cold-start failures

0455 preserved hardware Root/nonce, original MKB/systembag and encrypted Data
with an enabled original watchdog window above 300 seconds. Its receiver reached
the original MobileInstallation client instead of the former resolver wait.
Installation completion was not established. A second large `MIG?` response
was truncated and lacked its completion marker; later probes were correctly
blocked instead of retried automatically. A real synchronized FASTBOOT reset
still worked.

The fixed reports execute on the USB event thread. A fast multi-thread report
can fill its 16 KiB TX ring before that thread returns to USB polling. 0341
drains between bounded thread records, outside task/thread/scheduler locks,
using the existing bounded transport flush. Full frame budgets and ordinary
daemon output remain unchanged. The finite-ring host model reproduces old
loss and byte-exact delivery with a reading host. A nonreading host still does
not imply delivery; actual completion remains necessary.

0456/0457 exposed a separate cold prerequisite failure. The private file
`/private/var/.hios-cp-d-v1` still had root ownership, mode 0600, one link,
65,559 bytes and no flags, but reported protection class C (3), previously D
(4). The actor that changed the class is not yet established. It is incorrect
to label this observation alone as lost hardware keys or corrupt file contents.

0458 first verified the complete original D-pattern payload through original
APFS while the file reported class C, and checked both encrypted raw samples.
Only then did original authorization and APFS setattr restore this fixed private
fixture to D. Reopening, complete protected reading and raw samples passed.
The nonce/Root/device bank remained unchanged; VFS and UFS sync succeeded.
No file was recreated, no class key was regenerated, and no system/application
file was changed. This boot's second read was after restoration, not a cold D
read. 0344 labels those cases separately.

That boot next failed the installation-template prerequisite. The private
installation receipt also needs to survive legitimate protected Data lifecycle
changes. Its read path now accepts supported C or D protection, while preserving
exact Data mount/type, owner/group/mode/link/flags/length and all 4096 bytes of
contract hash, volume identities, completion state and reserved contents.
Receipt writes still require D. Other protection classes and incomplete or
changed contents remain errors. Normal boot does not recreate or rewrite this
receipt. The complete producer passed 8,495 host cases and nine compiled
behavioral mutation refusals. Physical receipt class and final boot result must
be taken from the sealed current capture, not assumed from these models.

0459 actually reports this receipt as class C, verifies its complete contents,
and reaches original userland. The D fixture passes a genuine cold read with
its original plaintext and original raw ciphertext samples, without a rewrite
or restoration on this boot. Original MKB/systembag, RTC/NVRAM and the enabled
watchdog are running. Three migration snapshots and one UI snapshot have real
completion markers; no automatic retry or shortened frame budget was needed.
Two migration samples now show `CLInternalPerformMigration` waiting for a
synchronous XPC reply, while the original installer receiver is in its run loop.
The reply destination and its resource failure still need direct investigation;
loaded module names alone do not establish which migrator is executing.

The sealed watchdog window reaches 321.438 seconds. Original C/D plaintext and
all four raw ciphertext sample hashes match the earlier 0343 baseline. The
three physical migration reports contain 6,159 / 14,017 / 13,700 normalized
fixed-report bytes; these are console-text counts, not USB wire-byte counts.
The overflowing 32-thread-per-target case is a host transport model, not a
claim that the current physical reports individually exceeded the ring size.
Full source replay is 323 common plus four host patches, all 5,261 source files
equal to the compiled manifest. Native SHA-256 is
`026381abf4219889212f518227ec7dba3058019aa27c2aa33db8022c1631c8f2`.

## Limits and remaining work

Original installation/DataMigration and system-shell registration still require
physical verification and further fixes. A/B, passcodes, full keychain and SEP
are not implemented; the supported state remains hardware-backed factory C/D.
The original watchdog is a software deadline monitor, not an independent
hardware watchdog. Daily developer-fast boot remains enabled; it is not a full
System/SystemOS content scan or independent Data fsck.

The user's panel capability and target are 120 Hz. Real DPU/panel timing and
vsync are not yet qualified; FrontBoard's 60 Hz report and a CPU timer do not
measure physical refresh. Genuine A650/Metal/presentation and usable GUI remain
open. True touch sampling is pending until the user returns, and Bluetooth is
deferred until physically usable SpringBoard.
