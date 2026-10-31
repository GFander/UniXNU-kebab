# CoreLocation receiver and database retry on 0460

The original migration caller is waiting in `CLInternalPerformMigration`.
The fixed original `locationd` process is alive but its initialization work
does not complete. Three physical snapshots at 93.943, 285.263 and 590.287
seconds observe all eleven threads of PID63, actual UID0. Its original
executable UUID is checked before selecting the referenced task; credentials
are never changed.

One original worker repeatedly reaches return site `0x100a38940`, immediately
after the call at `0x100a3893c` in the original database manager's initialization
and configuration routine. Exact original instructions compare the stored
SQLite result to 10, branch to that call, and call a helper that invokes
`sleep(5)`. SQLite documents 10 as the broad
[`SQLITE_IOERR` category](https://www.sqlite.org/rescode.html#ioerr); identifying
the failed operation requires more specific error evidence. This does not
establish corruption, denied permissions, missing class keys, a failed open
versus a failed pragma, or any particular filesystem operation.

Other receiver threads wait for LocationSupport dispatch queues, a database
manager mutex path, or `BLSXPCBacklightProxy isAlwaysOnEnabled`. The original
daemon exports `-[CLInternalService performMigrationWithReplyBlock:]` and its
launch configuration owns the locationd synchronous Mach service. A separate
observed exported request is `getTechnologiesInUseWithReplyBlock:`. No matching
IPC transaction identifier has been captured: observing the server's backlog
and the migration caller does not prove those particular transactions are
paired, or prove which thread owns each wait object.

The next step is to obtain the real database path and failing VFS operation or
extended SQLite error. No database was removed, permissions changed, key bank
regenerated, migration plugin skipped, or failed operation converted to success.
The IOERR cause remains unfixed.

## Source and qualification

0345 extends only the existing normal `MIG?` observer with fixed locationd
name, launchd parent, original executable UUID and actual UID. It retains the
32-thread/16-frame/64KiB budgets, referenced task/map/thread lifetimes, no
suspension, no printing under task/thread/scheduler locks and existing bounded
USB drains. Panic dispatch remains unchanged and refuses sleepable queries.

Full source replay:324 common+4 host patches, all5261 files equal. Native
SHA256:`25ab228e22dc4ac29cea74b887f1097ef3409fa8440549e25f4aac60dc642156`.
The complete production BSD/Mach source fixture retains prior observers and
tests the fixed receiver gates/UIDs, bounds/references, four32-thread targets,
full16-frame records and finite transport behavior. Nine compiled behavioral
mutants are refused. Host kernel/VFS/USB models are explicit; they are not
physical receiver execution.

Physical boot has three completed migration queries and one completed UI
query, without timeout or panic. The complete fixed-report console text sizes
are23718/23818/24826 bytes, each larger than the16KiB TX ring. These are
normalized console-text counts, not USB wire-byte counts. Actual command and
snapshot end markers prove these physical reports finish with bounded drains.

The original hardware Root/nonce, MKB/systembag, C/D cold reads and unchanged
raw ciphertext samples, defaults, RTC/NVRAM and original watchdog regression
pass. The qualified watchdog list reaches321.203s; longer raw observations
remain separately identifiable in the sealed log. The original watchdog is a
software deadline monitor. The native compiler still selects Apple's local
identity implementation while MACF/protection/audit remain enabled.

Source changes are diagnostic only. The real physical five-point, swipes and
two-contact input qualification remains the separately published0459 session.
Visible SpringBoard/event delivery, real A650/Metal/presentation, physical120Hz,
more than two contacts and full A/B/passcode/SEP support are not qualified.
Bluetooth remains deferred. Full goal remains active.
