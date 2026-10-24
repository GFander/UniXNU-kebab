# UniXNU-kebab

Device repository for the **OnePlus 8T (kebab, SM8250)**: a 16 KiB-page XNU
port that targets the iPhone X / iOS 16.7.16 userland.

Layout follows the project's
[repository guide](https://unixnu.com/zh/contributing/repository): the device
repository carries the board description and the device-specific patch set, and
every driver lives in its own public repository referenced as a submodule.

```
board.toml                  board description read by the unixnu driver
configs/                    defconfig and the iPhone X /product subtree
patches/xnu-8796-kebab/     the kernel patch set for this machine
drivers/KebabCPU/           submodule -> GFander/KebabCPU
drivers/KebabEarlyConsole/  submodule -> GFander/KebabEarlyConsole
scripts/                    ADT generator and helper scripts
feeds.toml, unixnu.lock     pinned external sources
```

Clone with `--recurse-submodules` (or run `git submodule update --init`).

`package/` is inherited from the sheng device template (Adreno and
offline-metal kernel probes). Kebab does not use it; it is kept only because
`unixnu build firmware` still lists `{tree}/package`.

## Machine

| Item | Value |
| --- | --- |
| SoC | SM8250 (Cortex-A77 x4 + Cortex-A55 x4) |
| Pages | **16 KiB** (`__ARM_16K_PG__`) — required by the iOS userland ABI |
| Machine config | `KEBAB`, plain `arm64` (no pointer authentication) |
| Kernel | xnu-8796.141.3 (the iOS 16 generation) |
| Build | `blacktop/darwin-xnu-build` on `macos-14`, Xcode 15.4 |
| Display | `0x9C000000`, 1080x2400, 4320 bytes per row, BGRA |
| GIC | GICv3, GICD `0x17A00000`, GICR `0x17A60000`, stride `0x20000` |
| Virtual timer | PPI 11 → **INTID 27** |
| UFS | `0x1D84000` |
| DRAM | base `0x80000000`, 12 GiB SKU (256 MiB boot window) |

## Boot chain

```
ABL -> boot image on boot_b -> Mu-Silicium UEFI (SILICIUM_UEFI.fd)
    -> UniXNU menu -> DarwinBoot -> ExitBootServices -> kernel, x0 = boot_args
```

The firmware reads the payload volume from the boot image at offset `0x800000`.
**It currently reads `boot_b` regardless of the active slot**, so bring-up
images must be flashed to `boot_b`.

## Current status

Working:

* the kernel compiles and links against xnu-8796 with this patch set;
* the payload volume is assembled and boots through the whole UEFI chain;
* `start.s` completes: bootstrap page tables, framebuffer mapping, MMU enable,
  exception vectors;
* `arm_init` and `PE_init_platform` run, and the **early framebuffer console
  prints** (`KebabEarlyConsole`), which is the only console this phone has.

Blocked:

* `arm_init` is currently entered at its **identity (physical) address**: the
  bootstrap KVA alias maps the wrong physical memory (verified by comparing the
  instruction word at both addresses), so a KVA hand-off faults;
* the kernel therefore panics in `_enable_timebase_event_stream` with
  `invalid bit index (-2)`, which is the visible symptom of
  `gPEClockFrequencyInfo.timebase_frequency_hz == 0`;
* the GIC and virtual-timer port is compiled in but has not run yet, because
  `pe_init_fiq()` is only reached once `pe_identify_machine()` stops returning
  early.

Next steps, in order:

1. fix the bootstrap KVA mapping (or link the kernel at the iOS address so the
   existing T1SZ assumptions hold) and hand `arm_init` a KVA pointer;
2. let `pe_identify_machine()` run, so the timebase comes from the device tree
   and `pe_init_fiq()` programs the GIC;
3. verify repeated timer interrupts (`KEBAB VTIMER irq ...`), then raise
   `cpus=1` to the full eight cores.

## Patch set

`patches/xnu-8796-kebab/` applies on top of a clean `xnu-8796.141.3` checkout,
in file-name order. It is device-specific, so it lives here rather than in the
base repository. Two patches still create driver sources because
darwin-xnu-build only accepts patches; the canonical copies are the `drivers/`
submodules and must stay identical.

## Flashing

```sh
fastboot flash boot_b XNU-<n>.img     # boot_b, see the note above
fastboot reboot
```

Recovery back to LineageOS: `fastboot flash boot_a` / `boot_b` with the
LineageOS `boot.img` of the matching build.
