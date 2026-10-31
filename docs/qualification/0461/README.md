# 0461: original locationd I/O evidence

Normal hardware keys, original MKB/systembag, old encrypted C/D, defaults/RTC/NVRAM
and enabled original watchdog exceed300s. Two MIG and one UI command complete.
This is observation qualification, not a database/crypto/GUI fix.

See [scope and actual failure](../../LOCATION-DATABASE-IO-20261031.md),
[next real classB requirements](../../CLASS-B-BACKEND-NEXT-20261031.md),
[0461-qualified-runtime.json](0461-qualified-runtime.json) and sealed log/provenance.
Original syscall error/retval, authorization and data buffers remain unchanged.
The public [production verifier](../../../scripts/tests/run-location-io.py)
uses a replayed0462source tree plus this and adjacent0461/0460receipts.
It compiles the full production observer and9behavioral mutants with explicit
kernel/VFS models; it does not produce new device evidence.
