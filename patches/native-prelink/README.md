# NativePrelink kernel-import patch

This is a tooling patch for `kits/NativePrelink`; it is not part of the
xnu-8796-kebab patch series.

Apply from the NativePrelink root:

```
git apply ../../patches/native-prelink/0001-prelink-kernel-imports.patch
# or
patch -p1 < ../../patches/native-prelink/0001-prelink-kernel-imports.patch
```

The patch adds `emit_prelink.py --kernel-import SYMBOL=ORIGINAL_VA[@NATIVE_VA]`,
which rewrites direct B/BL, ADRP+ADD, ADRP+LDR, and absolute 8-byte
vtable/GOT/literal references in the native kernel to the final address of a
selected donor component. It refuses an import with no reference, an address
outside the selected closure, or a B/BL outside the signed +/-128 MiB range.

Tests:

```
cd kits/NativePrelink
python3 -m unittest -v test_native_prelink.py test_emit_prelink.py
```

See `docs/BRING-UP-STATE.md` section 89 for the design, reach analysis and the
offline probe result.
