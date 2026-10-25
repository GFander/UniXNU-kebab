# rootfs

The first userland program. `launchd` is a hand-built static arm64 Mach-O: it
writes one line to stdout and exits. `scripts/make-init.py` regenerates it, so
the binary is only here to be easy to fetch from a machine that has no Python.

    python3 scripts/make-init.py rootfs/launchd

It exists to prove that the kernel can `exec` from the HFS+ root; replace it
with a real init once the port reaches the userland stage.
