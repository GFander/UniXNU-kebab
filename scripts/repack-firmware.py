#!/usr/bin/env python3
"""Repack the Mu-Silicium firmware into the boot image we flash.

The flashed image is an Android boot image v0 (`ANDROID!`) whose "kernel"
section is the UEFI firmware and whose UniXNU payload sits at 0x800000:

    0x000000  boot image header            (page_size 0x1000)
    0x001000  0x70-byte shim + UEFI FV     (kernel_size = 0x70 + 0x300000)
    ...
    0x800000  UniXNU payload volume        (67,108,864 bytes)

Only the FV region is replaced, so the boot header, the shim and the payload
survive byte for byte. The script refuses to splice an FV of a different size,
because that would also require rewriting the boot header.

    python3 scripts/repack-firmware.py \
        --template signed.img \
        --fv Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd \
        --payload payload.img --out XNU-NNN.img
"""
import argparse
import struct
import sys
from pathlib import Path

PAYLOAD_OFFSET = 0x800000
SHIM_SIZE = 0x70
HEADER_PAGE = 0x1000


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", required=True)
    ap.add_argument("--fv", required=True)
    ap.add_argument("--payload")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    img = bytearray(Path(args.template).read_bytes())
    fv = Path(args.fv).read_bytes()

    if img[0:8] != b"ANDROID!":
        print("template is not an Android boot image", file=sys.stderr)
        return 1

    kernel_size = struct.unpack_from("<I", img, 8)[0]
    old_fv_size = kernel_size - SHIM_SIZE
    if len(fv) != old_fv_size:
        print(f"FV size changed: template holds {old_fv_size}, new one is {len(fv)}",
              file=sys.stderr)
        return 1

    start = HEADER_PAGE + SHIM_SIZE
    img[start:start + len(fv)] = fv
    print(f"  firmware replaced at 0x{start:x} ({len(fv)} bytes)")

    if args.payload:
        payload = Path(args.payload).read_bytes()
        if len(payload) + PAYLOAD_OFFSET > len(img):
            print("payload does not fit in the template", file=sys.stderr)
            return 1
        img[PAYLOAD_OFFSET:PAYLOAD_OFFSET + len(payload)] = payload
        print(f"  payload replaced at 0x{PAYLOAD_OFFSET:x} ({len(payload)} bytes)")

    Path(args.out).write_bytes(bytes(img))
    print(f"  wrote {args.out} ({len(img)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
