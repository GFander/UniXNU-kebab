#!/usr/bin/env python3
"""Build the first userland program: a static arm64 Mach-O that says hello.

Used by CI to place /sbin/launchd into the HFS+ root image.

Why hand-build it: `load_init_program()` execs `/sbin/launchd` from the root
volume, and XNU's exec needs a Mach-O carrying LC_UNIXTHREAD (a static
binary). Darwin toolchains no longer link user binaries statically, and our
program is ten instructions, so emitting the Mach-O directly is simpler and
fully reproducible.

Syscall ABI on arm64 XNU: `svc #0x80`, x16 = syscall number, x0..x8 = args.
write(2) is 4, exit(2) is 1.

Layout of the produced file:

    [mach header][LC_SEGMENT_64][LC_UNIXTHREAD][pad]
    [text at 0x1000][pad to 16 KiB][message][pad]

One segment covers the whole file and is mapped at VMADDR.
"""
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64_ALL = 0
MH_EXECUTE = 0x2
LC_SEGMENT_64 = 0x19
LC_UNIXTHREAD = 0x5
ARM_THREAD_STATE64 = 6

# Must stay below 4 GiB: on arm64 the kernel's Mach-O loader rejects any
# executable segment whose vmaddr + vmsize + slide exceeds UINT32_MAX.
VMADDR = 0x10000
DATA_OFF_PLACEHOLDER = 0x4000
PAGE = 0x4000          # the machine runs a 16 KiB-page kernel
TEXT_OFF = 0x1000


def arm64_code(data_addr: int, msg_len: int):
    words = []

    def emit(w):
        words.append(w)

    def mov_imm64(reg, value):
        emit(0xD2800000 | ((value & 0xFFFF) << 5) | reg)          # movz
        for shift, shift_bits in ((16, 1), (32, 2), (48, 3)):
            chunk = (value >> shift) & 0xFFFF
            emit(0xF2800000 | (shift_bits << 21) | (chunk << 5) | reg)  # movk

    emit(0xD2800020)                       # x0 = 1   (stdout)
    mov_imm64(1, data_addr)                # x1 = message
    emit(0xD2800000 | (msg_len << 5) | 2)  # x2 = length
    emit(0xD2800000 | (4 << 5) | 16)       # x16 = 4  (write)
    emit(0xD4001001)                       # svc #0x80
    emit(0xD2800000)                       # x0 = 0
    emit(0xD2800000 | (1 << 5) | 16)       # x16 = 1  (exit)
    emit(0xD4001001)                       # svc #0x80
    emit(0x14000000)                       # b .   (never reached)
    return words


def build(message: bytes) -> bytes:
    text = b"".join(struct.pack("<I", w)
                    for w in arm64_code(VMADDR + DATA_OFF_PLACEHOLDER, len(message)))
    data_off = (TEXT_OFF + len(text) + PAGE - 1) & ~(PAGE - 1)
    text = b"".join(struct.pack("<I", w)
                    for w in arm64_code(VMADDR + data_off, len(message)))

    total = data_off + len(message)
    seg = struct.pack(
        "<II16sQQQQiiII",
        LC_SEGMENT_64, 24 + 80,
        b"__TEXT".ljust(16, b"\0"),
        VMADDR, total,              # vmaddr, vmsize
        0, total,                   # fileoff, filesize
        7, 5,                       # maxprot rwx, initprot rwx
        1, 0,                       # nsects, flags
    )

    # arm_thread_state64: x[29], fp, lr, sp, pc, then cpsr + pad.
    regs = [0] * 29 + [0, 0, VMADDR + total, VMADDR + TEXT_OFF]
    state = b"".join(struct.pack("<Q", v) for v in regs)
    state += struct.pack("<II", 0, 0)                   # cpsr, pad
    assert len(state) == 272, len(state)
    unixthread = struct.pack("<II", LC_UNIXTHREAD, 16 + len(state))
    unixthread += struct.pack("<II", ARM_THREAD_STATE64, len(state) // 4)
    unixthread += state

    sizeofcmds = len(seg) + len(unixthread)
    hdr = struct.pack(
        "<IiiIIIII", MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
        MH_EXECUTE, 2, sizeofcmds, 0, 0,
    )
    out = bytearray(hdr + seg + unixthread)
    assert len(out) <= TEXT_OFF, len(out)
    out += b"\0" * (TEXT_OFF - len(out))
    out += text
    out += b"\0" * (data_off - len(out))
    out += message
    assert len(out) == total
    return bytes(out)


if __name__ == "__main__":
    dest = sys.argv[1] if len(sys.argv) > 1 else "launchd"
    msg = b"kebab: userland alive\n"
    blob = build(msg)
    with open(dest, "wb") as f:
        f.write(blob)
    print(f"  {dest}: {len(blob)} bytes, message {msg!r}")
