# UniXNU-kebab

UniXNU device repository for the OnePlus 8T (kebab, Qualcomm SM8250).

Status: first milestone only - produce `kernel.macho` with the minimal
Apple-format device tree platform kernel.

## Layout

- `board.toml` - board description, donor IPSW, kernel archives and prelink pins
- `configs/kebab_defconfig` - kernel configuration

Not present yet: `package/`, `drivers/`, `kits/`, `scripts/make-kebab-adt.py`,
`[payload]` boot image configuration. They are needed for a bootable image and
are added once the kernel builds.

## Build (on macOS)

    export UNIXNU_UEFI=<path to Mu-Silicium checkout>
    unixnu fetch
    unixnu build kernel

Output: `out/kebab/kernel/kernel.macho`.
