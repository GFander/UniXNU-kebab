#!/usr/bin/env python3
"""Recover the IOStorageFamily/IOBlockStorageDevice symbol addresses from a
stripped iOS prelinked kernelcache, structurally.

Usage: python3 recover_iofssymbols.py [kernelcache]

Method (see docs/BRING-UP-STATE.md section 93):
  1. parse the outer Mach-O segment map (VA <-> file offset)
  2. find the nested MH_KEXT_BUNDLE header of IOStorageFamily inside
     __PRELINK_TEXT and read its __TEXT/__TEXT_EXEC/__DATA_CONST/__DATA
  3. inside the kext's __DATA_CONST,__const find the vtable: two zero header
     words followed by a run of code pointers; identify it by slot 7 being a
     `adrp/add &gMetaClass; ret` function whose target is the class's
     gMetaClass (found from the __mod_init_func registration thunk)
  4. slots 0..167 are IOService's, 168..189 are IOBlockStorageDevice's 22 own
     virtuals, in declaration order; pure ones all point at the same
     __cxa_pure_virtual address, which fixes the mapping
  5. C2/D2 are not in the vtable: C2 is the unique function that stores
     vtable+16 into [this], D2 is the function right after it that tail-calls
     the IOService destructor stub (also called by subclass D1/D0 thunks)
"""
import struct
import sys

KC = sys.argv[1] if len(sys.argv) > 1 else "/home/albert/kernelcache.fileset.macho"
CLASS = sys.argv[2] if len(sys.argv) > 2 else "IOBlockStorageDevice"
BUNDLE = sys.argv[3] if len(sys.argv) > 3 else "com.apple.iokit.IOStorageFamily"
MH_MAGIC_64 = 0xFEEDFACF
MH_KEXT_BUNDLE = 11
B = 0xFFFFFFF000000000

with open(KC, "rb") as f:
    D = bytearray(f.read())


def u32(va):
    off = va2off(va)
    return struct.unpack_from("<I", D, off)[0] if off is not None else 0


def u64(va):
    off = va2off(va)
    return struct.unpack_from("<Q", D, off)[0] if off is not None else 0


def cstr(va, n=96):
    off = va2off(va)
    if off is None:
        return None
    e = D.find(b"\0", off, off + n)
    return D[off:e].decode("utf-8", "replace")


segs = []


def parse_header(off_or_va, is_va):
    global segs
    off = va2off(off_or_va) if is_va else off_or_va
    magic, cputype, cpusub, ftype, ncmds, sizeofcmds, flags, _ = \
        struct.unpack_from("<IiiIIIII", D, off)
    if magic != MH_MAGIC_64:
        return None
    p = off + 32
    out = []
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", D, p)
        if cmd == 0x19:  # LC_SEGMENT_64
            (_, _, sn, vmaddr, vmsize, foff, fsize, maxp, initp,
             nsects, sflags) = struct.unpack_from("<II16sQQQQiiII", D, p)
            secs = []
            sp = p + 72
            for _ in range(nsects):
                (sec, segn, addr, size, so, align, reloff, nreloc, sf,
                 r1, r2, r3) = struct.unpack_from("<16s16sQQIIIIIIII", D, sp)
                secs.append((sec.split(b"\0")[0].decode(), addr, size))
                sp += 80
            out.append(dict(name=sn.split(b"\0")[0].decode(), vm=vmaddr,
                            vs=vmsize, off=foff, fs=fsize, prot=initp,
                            secs=secs))
        p += cmdsize
    return dict(ftype=ftype, segs=out)


def va2off(va):
    for s in segs:
        if s["vm"] <= va < s["vm"] + s["fs"]:
            return s["off"] + (va - s["vm"])
    return None


# ---- 1. outer segment map -------------------------------------------------
segs = parse_header(0, False)["segs"]


def segof(va):
    for s in segs:
        if s["vm"] <= va < s["vm"] + s["vs"]:
            return s["name"]
    return None


# ---- 2. find the kext ------------------------------------------------------
# Use __PRELINK_INFO: the kext's dict carries _PrelinkExecutableLoadAddr right
# before CFBundleIdentifier, so the load address of the class's kext is the
# last such integer before the bundle-id string.
target_str = CLASS
target_id = BUNDLE
info = [s for s in segs if s["name"] == "__PRELINK_INFO"][0]
blob = bytes(D[info["off"]:info["off"] + info["fs"]])
pos = blob.find(target_id.encode())
assert pos > 0, "bundle id not in __PRELINK_INFO"
q = blob.rfind(b"_PrelinkExecutableLoadAddr", 0, pos)
frag = blob[q:q + 200]
hv = int(frag.split(b">")[2].split(b"<")[0], 0) if b"0x" in frag else None
assert hv, frag
kext = parse_header(hv, True)
kext["hdr"] = hv
assert kext, "no MH_KEXT_BUNDLE at %#x" % hv
print("donor        :", KC)
print("kext header  : %#x" % kext["hdr"])
for s in kext["segs"]:
    print("  SEG %-12s vm=%#x size=%#x (%s)" %
          (s["name"], s["vm"], s["vs"],
           ",".join(x[0] for x in s["secs"])))

KSEG = {s["name"]: s for s in kext["segs"]}
KCODE = KSEG["__TEXT_EXEC"]
KCONST = KSEG["__DATA_CONST"]
KDATA = KSEG["__DATA"]
KMIN, KMAX = KCODE["vm"], KCODE["vm"] + KCODE["vs"]
CMIN, CMAX = KCONST["vm"], KCONST["vm"] + KCONST["vs"]
KMODINIT = [s for s in KCONST["secs"] if s[0] == "__mod_init_func"][0]
KCONSTS = [s for s in KCONST["secs"] if s[0] == "__const"][0]


def in_kext_code(v):
    return KMIN <= v < KMAX


# ---- 3. class registration: every init that writes a __DATA_CONST vptr ----
def adrp(a, w):
    if (w & 0x9F000000) != 0x90000000:
        return None
    imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
    if imm & (1 << 20):
        imm -= 1 << 21
    return (a & ~0xFFF) + (imm << 12)


def addimm(w):
    if (w & 0x7F000000) != 0x11000000:
        return None
    return (w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) << (12 if (w >> 22) & 3 else 0))


stores = []
for a in range(KMIN, KMAX, 4):
    w = u32(a)
    if (w & 0xFFC00000) != 0xF9000000 or ((w >> 5) & 31) != 0:
        continue
    rt = w & 31
    for k in range(1, 13):
        ad = addimm(u32(a - k * 4))
        if ad and ad[0] == rt and ad[1] == rt:
            for k2 in (1, 2):
                t = adrp(a - (k + k2) * 4, u32(a - (k + k2) * 4))
                if t is not None and (u32(a - (k + k2) * 4) & 31) == rt:
                    tgt = t + ad[2]
                    if CMIN <= tgt < CMAX:
                        stores.append((a, tgt))
                    break
            break

# The canonical source of gMetaClass is the kext's __mod_init_func entry: the
# static initialiser sets x0 = &gMetaClass, x1 = class-name string, w3 = size,
# calls OSMetaClass::OSMetaClass(), then stores the MetaClass vtable into [x0].
def scan_init(e):
    gm = name = size = None
    for i in range(80):
        a = e + 4 * i
        w = u32(a)
        if w == 0xD65F03C0:      # end of this initialiser
            break
        t = adrp(a, w)
        if t is None:
            continue
        rd = w & 31
        ad = addimm(u32(a + 4))
        if not (ad and ad[0] == rd and ad[1] == rd):
            continue
        tgt = t + ad[2]
        if rd == 1:
            c = cstr(tgt, 64)
            if c and all(32 <= ord(ch) < 127 for ch in c):
                name = c
        elif rd == 0 and segof(tgt) in ("__PRELINK_DATA", "__DATA"):
            gm = tgt
        elif rd == 3:
            pass
        elif 2 <= rd <= 31:
            # possible MetaClass vtable: followed within 8 insns by str xRd,[x0]
            for k in range(2, 10):
                w2 = u32(a + 4 * k)
                if ((w2 & 0xFFC00000) == 0xF9000000 and (w2 & 31) == rd
                        and ((w2 >> 5) & 31) == 0 and prot_of(tgt) == 1):
                    metaztv = tgt
                    break
    return gm, name, metaztv

def prot_of(va):
    for sg in kext["segs"]:
        if sg["vm"] <= va < sg["vm"] + sg["vs"]:
            return sg["prot"]
    return None

MI = [u64(KMODINIT[1] + 8 * i) for i in range(KMODINIT[2] // 8)]
GMETA = METAZTV = SIZE = None
for e in MI:
    if not in_kext_code(e):
        continue
    gm, nm, mz = scan_init(e)
    if nm == target_str and gm:
        GMETA, METAZTV, SIZE = gm, mz, None
        # class size: mov w3,#imm near the bl
        for i in range(80):
            w = u32(e + 4 * i)
            if w == 0xD65F03C0:
                break
            if (w & 0x7F800000) == 0x52800000 and (w & 31) == 3:
                v = (w >> 5) & 0xFFFF
                SIZE = v - 0x10000 if v & 0x8000 else v
        print("\nmod_init initialiser %#x: gMetaClass=%#x MetaZTV=%#x sizeof(class)=%#x"
              % (e, GMETA, METAZTV, SIZE))
        break
assert GMETA, "class registration not found"

# ---- 4. find the instance vtable ------------------------------------------
def runlen(va):
    n = 0
    while segof(u64(va + 8 * n)) in ("__PLK_TEXT_EXEC", "__TEXT_EXEC"):
        n += 1
    return n

cands = []
a = CMIN
while a < CMAX:
    if u64(a) == 0 and u64(a + 8) == 0:
        n = runlen(a + 16)
        if n >= 40:
            cands.append((a, n))
        a += 8
    else:
        a += 8

# the instance vtable is the candidate whose -0x20 qword is &gMetaClass
inst = None
for ztv, n in cands:
    if u64(ztv - 0x10) == GMETA:
        inst = (ztv, n)
        break
assert inst, "no instance vtable with -0x10 == %#x" % GMETA
ztv, nslot = inst
print("instance ZTV : %#x (vptr %#x, %d slots, %d words)"
      % (ztv, ztv + 16, nslot, nslot + 2))
print("gMetaClass   : %#x (vtable[-0x20]; getMetaClass slot 7 = %#x)"
      % (GMETA, u64(ztv + 16 + 7 * 8)))

# ---- 5. tail mapping -------------------------------------------------------
def unsupported(fn):
    return u32(fn) == 0x528058E0 and u32(fn + 4) == 0x72BC0000 and u32(fn + 8) == 0xD65F03C0

TAIL = [
    ("doEjectMedia", "=0"), ("doFormatMedia", "=0"), ("doGetFormatCapacities", "=0"),
    ("doLockUnlockMedia/doSynchronizeCache?", ""),
    ("getVendorString", "=0"), ("getProductString", "=0"), ("getRevisionString", "=0"),
    ("getAdditionalDeviceInfoString", "=0"), ("reportBlockSize", "=0"),
    ("reportEjectability", "=0"), ("reportMaxValidBlock", "=0"),
    ("reportMediaState", "=0"), ("reportRemovability", "=0"),
    ("reportWriteProtection", "=0"), ("getWriteCacheState", ""),
    ("setWriteCacheState", ""), ("doAsyncReadWrite", "=0"), ("requestIdle", ""),
    ("doUnmap", ""), ("doSetPriority", ""), ("doSynchronize", ""),
    ("doGetProvisionStatus", ""),
]
print("\nslots 168..189 (class's own 22 virtuals):")
pure = u64(ztv + 16 + 168 * 8)
for i, (nm, tag) in enumerate(TAIL):
    va = ztv + 16 + (168 + i) * 8
    fn = u64(va)
    kind = "pure" if fn == pure else ("kext" if in_kext_code(fn) else "kernel")
    print("  slot %3d @%#x  %#018x  %-8s %-34s %s"
          % (168 + i, va, fn, kind, nm, "(unsupported)" if unsupported(fn) else ""))
print("  __cxa_pure_virtual = %#x" % pure)

# ---- 6. C2 / D2 ------------------------------------------------------------
vptr = ztv + 16
ctor_store = None
for a in range(KMIN, KMAX, 4):
    w = u32(a)
    if (w & 0xFFC00000) != 0xF9000000 or ((w >> 5) & 31) != 0:
        continue
    rt = w & 31
    for k in range(1, 13):
        ad = addimm(u32(a - k * 4))
        if ad and ad[0] == rt and ad[1] == rt:
            for k2 in (1, 2):
                t = adrp(a - (k + k2) * 4, u32(a - (k + k2) * 4))
                if t is not None and (u32(a - (k + k2) * 4) & 31) == rt and t + ad[2] == vptr:
                    ctor_store = a
                    break
            break
    if ctor_store:
        break
# walk back to function start (after ret/brk/b)
p = ctor_store
while True:
    p -= 4
    w = u32(p)
    if w in (0xD65F03C0, 0xD4200020) or (w & 0xFC000000) == 0x14000000:
        break
c2 = p + 4
print("\nC2 store %#x in function %#x" % (ctor_store, c2))
print("  bytes: %s" % " ".join("%08x" % u32(c2 + 4 * i) for i in range(6)))
# D2: clang emits it immediately after C2; it has no frame, just a tail call
# to the IOService destructor stub.
p = c2
while u32(p) != 0xD65F03C0:
    p += 4
d2 = p + 4
assert (u32(d2) & 0xFC000000) == 0x14000000, "D2 is not a B instruction"
print("D2 %#x: %08x (b %#x)  -- tail call to base dtor stub"
      % (d2, u32(d2), d2 + ((((u32(d2) & 0x3FFFFFF) ^ (1 << 25)) - (1 << 25)) << 2)))

# ---- 7. final table --------------------------------------------------------
print("\n== recovered symbols ==")
print("gMetaClass            %#x" % GMETA)
print("getMetaClass          %#x" % u64(ztv + 16 + 7 * 8))
print("getProperty           %#x" % u64(ztv + 16 + 33 * 8))
print("setProperties         %#x" % u64(ztv + 16 + 44 * 8))
print("getWriteCacheState    %#x" % u64(ztv + 16 + 182 * 8))
print("setWriteCacheState    %#x" % u64(ztv + 16 + 183 * 8))
print("doSetPriority         %#x" % u64(ztv + 16 + 187 * 8))
print("doGetProvisionStatus  %#x" % u64(ztv + 16 + 189 * 8))
print("doLockUnlockMedia*    %#x  (*donor slot 171 is doSynchronizeCache)" % u64(ztv + 16 + 171 * 8))
print("C2                    %#x" % c2)
print("D2                    %#x" % d2)
print("__ZTV                 %#x" % ztv)
