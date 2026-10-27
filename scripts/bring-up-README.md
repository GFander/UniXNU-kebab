# P1 bring-up orchestrator

`scripts/bring-up.py` runs the P1 sequence end to end with one invocation:

```sh
python scripts/bring-up.py --from vol
```

It stops at the first observation it did not expect.  On failure it prints the
checkpoint, what was expected, what was seen, the last 30 console lines and the
two log paths; the same report is written to `failure.txt` in the run directory.

The board was stranded when this was written, so the sequence has **not** been
run against hardware.  The offline checks are the fake board in
`scripts/test-bring-up.py` and `--dry-run`.

## Artifacts

The arguments of each phase are compiled into the payload's TOML
(`args = "..."` at image offset `0x800000`), not into the Android boot header.
Three images are therefore required; the final boot is not the install image.

| Default path | What it is |
| --- | --- |
| `work/p1/boot-vol.img` | kernel with `kebab_vol=1 kebab_noinit=1 kebab_install=1` |
| `work/p1/boot-inst.img` | kernel without `kebab_vol`, with `kebab_noinit=1 kebab_install=1` |
| `work/p1/boot-final.img` | kernel without `kebab_vol` or `kebab_noinit`, with `kebab_dsc=1 kebab_dsc_map=1` |
| `work/p1/KebabRoot.vext` | 1,769,472-byte VEXT stream from the **new** `scripts/make-root-volume.py`: a bare HFS+ volume, no `/dev` skeleton |
| `work/p1/skel.kist` | 16-entry KIST v2 skeleton from `scripts/make-minimal-userland.py --only skel` (10 dirs, 3 symlinks, 3 files; 153 B installed, 65,536 B wire) |
| `work/p1/base.kist` | 5-entry minimal userland from `scripts/make-minimal-userland.py` |
| `work/p1/cache.kist` | 44-entry split dyld shared cache from the same tool |

Every path can be overridden (`--vol-image`, `--skel-stream`, `--base-stream`,
`--work-dir`, ...).  All images are checked for size `100663296`, the
`ANDROID!` header and the required/forbidden boot arguments before a flash.

## Checkpoints

| ID | Name | Observable requirement |
| --- | --- | --- |
| C01 | precondition.fastboot | `fastboot devices` lists the board, or a console `FASTBOOT` brings it there |
| C02 | precondition.slot | `current-slot: b` |
| C-ARGS | precondition.args | the image's embedded `args = "..."` match the phase |
| C-BOOT-BANNER | boot.banner | `KEBAB USB: running (DSTS ...)` / `event thread started` after the reboot |
| C-BOOT-NOINIT-* | boot.noinit | `KEBAB: kebab_noinit=1: staying in the kernel, no init program` |
| C-VOL-STREAM | vol.stream | VEXT size, extents and embedded HFS+ header (4,194,304 blocks) |
| C-VOL-STATUS | vol.status | `VOL? gated 1 ... writable 1`, partition and on-disk header |
| C-VOL-OLD | vol.old-header | 3 `VOL OLD vh` lines + 32 `VOL OLD bytes` lines (the old 512-byte header) |
| C-VOL-DRY | vol.dry | `VOL PLAN extents 3 bytes 1744896 wire 1769472 volume 17179869184` |
| C-VOL-WRITE | vol.write | `VOL NEW ... bsize 4096 blocks 4194304` and `VOL DONE extents 3 ...` |
| C-VOL-REBOOT | vol.reboot | `KEBAB REBOOT: PEHaltRestart(kPERestartCPU)` |
| C-VOL-MOUNT | vol.mount | `KEBAB HFS: KebabRoot: ... total blocks 4194304` |
| C-INST-GEOM | install.geometry | `INST? root rw`, `space ... of 17179869184`, `bsize 4096 blocks 4194304 free > 0` |
| C-SKEL-STREAM | skel.stream | `INST STREAM v2 skeleton entries 16 bytes 153 wire 65536` |
| C-SKEL-ATTRS | skel.attrs | one `OK <path> ... mode 0%o uid 0 gid 0` per entry (`SKIP ... link exists` allowed for links); a wrong owner fails the phase |
| C-{BASE,CACHE}-PRECHECK | precheck.* | every `INST CHECK` destination is same or missing |
| C-SEND-{SKEL,BASE,CACHE} | send.* | `INST BEGIN ... write`, `INST ACK`, `INST DONE entries N ok+skipped=N ...` |
| C-VERIFY-{BASE,CACHE} | verify.* | sampled `INST CHECK` size and SHA-256 match the stream |
| C-FINAL | final.launchd | no CS_KILLED/initproc/DSC failure; `launchd logging initialized` or the zprint `zone name`/`ZONE TOTALS` table |

`skel.kist` is always sent whole (one 64 KiB chunk): `INST CHECK` describes a
file size and hash, so it cannot precheck a directory or symlink, and the kernel
re-applies the exact mode/owner on a re-run.  `base.kist` is checked entry by
entry; if all 5 already verify the stream is not sent.  `cache.kist` is checked
the same way for all 44 entries.  A partial stream is sent in full and the
kernel reports the already-correct entries as `SKIP`; a complete stream is never
re-sent.

## Usage

```sh
# full run
python scripts/bring-up.py --from vol

# start from the installer, the skeleton or a userland stream
python scripts/bring-up.py --from install
python scripts/bring-up.py --from skel --to skel
python scripts/bring-up.py --from cache --to cache

# only inspect artifacts and print the plan
python scripts/bring-up.py --dry-run --work-dir work/p1

# the final boot without kebab_noinit (no stream/flash before it)
python scripts/bring-up.py --from final
```

Useful options: `--force-vol` (imprint even when `INST?` already reports the
large volume), `--no-precheck` (skip the per-entry `INST CHECK` pass; not
recommended), `--expected-vol-stream/--expected-vol-extents/--expected-vol-data`
(update the VEXT shape if the skeleton changes the stream size),
`--port`, `--fastboot`, `--boot-timeout`, `--phase-timeout`,
`--final-timeout`, `--json-summary`.

## Resume

* At start-up, if the console answers, `INST?` (or `VOL?` on a vol image)
  reports the root geometry.  A large geometry skips the vol phase.
* If the board is already in fastboot, the geometry is unknown.  `--from auto`
  flashes `boot-inst.img` and reads `INST?`; a small geometry sends it back
  through the vol phase and retries install.  `--from vol` imprints by default
  when the state is unknown; use `--from install` to forbid that.
* `base`/`cache` prechecks are per entry.  If every destination verifies, the
  stream is skipped entirely.  If only some do, the stream is sent and the
  kernel's own resume prints `SKIP` for the rest.
* `skel` has no precheck: it is 64 KiB and is re-applied on every run.  The
  kernel re-sets directory modes/owners and rewrites the three files; existing
  symlinks print `SKIP ... link exists` after checking the type.
* A reboot or panic does not leave the orchestrator in a half-configured state:
  the next run re-probes `INST?` and the stream hashes.

## Panic behaviour

Patch `0134` makes an unserviced panic reboot through PSCI instead of parking.
The console reader stays alive across the reset and re-opens the CDC-ACM port.
After a phase is ready, any of these is reported as a panic/reboot rather than a
timeout:

* a fresh `KEBAB USB: running` / `event thread started` / `Darwin Kernel
  Version` line;
* a `panic(cpu ...)` or `KEBAB USB: panic console cannot be serviced` line;
* the console re-enumerating (`console reopened`) or staying gone for >10 s.

`kebabkdp=1` is rejected by the image-argument check unless `--allow-kdp` is
given: it is the one switch that restores the old "wait for a debugger"
behaviour and defeats automatic panic recovery.

## Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | sequence completed |
| 2 | precondition failed (no fastboot, no console, wrong slot, missing artifact) |
| 3 | a checkpoint failed or the device refused the stream |
| 4 | panic/reboot detected mid-phase |
| 5 | console lost and could not be re-opened |
| 6 | dry-run verification failed |
| 130 | interrupted |

## Offline validation

```sh
python3 scripts/test-bring-up.py
python3 scripts/test-bring-up.py --kernel-source /path/to/bsd/dev/kebab_usb.c
python3 scripts/test-bring-up.py --make-fixtures /tmp/p1-fixtures
```

The fake board parses the actual KIST/VEXT payloads, enforces one chunk per ACK,
models `VOL DRY`/`VOL BEGIN`/`VOL NEW`/`VOL PLAN`, `INST CHECK`, resume `SKIP`,
a mid-stream panic with and without a panic line, and wrong-slot /
wrong-argument failures.  Its KIST v2 fixture is a 16-entry skeleton with a
01777 directory and three symlinks, and one case reports a wrong uid/gid from
the kernel read-back to prove the orchestrator fails that checkpoint.  A
separate check covers v2 parsing, the widened 07777 mask and v1 compatibility.
The command lint checks the nine command literals and the `VOL BEGIN` argument
order against the kernel source.

## Known gaps

* `/dev` and the rest of the root-volume tree are now created by the kernel
  from `skel.kist`; the VEXT itself is a bare HFS+ volume.  `make-root-volume.py`
  must still produce the 1,769,472-byte / 3-extent shape.  The creation path
  (`vn_create` + `vnode_setattr`, `VNOP_SYMLINK`, the uid/gid read-back) is
  link-verified but has not run on real HFS+ hardware.
* `skel.kist` has not crossed USB; `INST STREAM v2 skeleton` and the
  `OK ... uid 0 gid 0` lines are only modelled by the fake board.
* Three phase images are required; the args are in the payload TOML, so a
  single boot image cannot be re-used for all three boots without rebuilding
  the payload.
* `kebab_cs=15` alone is not enough for the final boot: `kebab_dsc=1
  kebab_dsc_map=1` must be in `boot-final.img`, otherwise launchd cannot map
  the shared cache.
* `ufsrw=1` is required for both VOL and INST writes even though the short P1
  argument list mentions only `rootrw=1`; the orchestrator checks for it.
* The fake board validates protocol logic, not the real USB/timing behaviour;
  the first hardware run is still the acceptance test.
