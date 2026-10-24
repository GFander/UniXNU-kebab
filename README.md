# UniXNU-kebab

Device repository for the **OnePlus 8T (kebab, SM8250)** — a 16 KiB-page XNU
port that targets the iPhone X / iOS 16.7.16 userland.

## Layout

Following the [repository layout](https://unixnu.com/zh/contributing/repository)
of the project:

```
board.toml                 board description read by the unixnu driver
configs/                   defconfig and the iPhone X product subtree
patches/xnu-8796-kebab/    the kernel patch set for this machine
drivers/KebabCPU/          submodule -> GFander/KebabCPU        (topic: unixnu)
drivers/KebabEarlyConsole/ submodule -> GFander/KebabEarlyConsole (topic: unixnu)
scripts/                   ADT generator and helper scripts
feeds.toml                 pinned external sources
unixnu.lock                resolved source revisions
```

Every driver lives in its own public repository and is pulled in as a
submodule, so the device repository never carries driver sources itself.
Clone with:

```sh
git clone --recurse-submodules https://github.com/GFander/UniXNU-kebab
```

`package/` is inherited from the sheng device template (Adreno / offline-metal
kernel code). Kebab does not use it; it is kept only because `unixnu build
firmware` still lists `{tree}/package`.

## Machine

| Item | Value |
| --- | --- |
| SoC | SM8250 (Cortex-A77 x4 + A55 x4) |
| Pages | **16 KiB** (`__ARM_16K_PG__`), required by the iOS userland ABI |
| Machine config | `KEBAB`, plain `arm64` (no pointer authentication) |
| Kernel | xnu-8796.141.3 (the iOS 16 generation) |
| Build | `blacktop/darwin-xnu-build` on `macos-14` (Xcode 15.4) |
| Display | framebuffer at `0x9C000000`, 1080x2400, 4320 bytes/row |
| GIC | GICv3, distributor `0x17A00000`, redistributor `0x17A60000`, 0x20000 stride |
| Virtual timer | PPI 11 → **INTID 27** |
| UFS | `0x1D84000` |
| DRAM | `0x80000000`, 12 GiB SKU |

## Current state

* the kernel compiles and links against xnu-8796 with this patch set;
* the payload volume is assembled and boots through Mu-Silicium + UniXNU +
  DarwinBoot on the phone;
* the kernel currently stops before `arm_init`, and `KebabEarlyConsole` exists to
  make that visible on the panel — the bring-up is still in progress.

See `MIGRATION-16K.md` in the owner's working notes for the long-form record.

## Patch set

`patches/xnu-8796-kebab/` is applied on top of a clean `xnu-8796.141.3`
checkout. Order matters; the files are numbered and applied sorted.

The set is device-specific, so it lives here rather than in the base
repository. Two patches (`0032`, `0034`) still *create* driver sources because
the darwin-xnu-build harness only accepts patches; the canonical copies live in
the `drivers/` submodules and must stay identical.
