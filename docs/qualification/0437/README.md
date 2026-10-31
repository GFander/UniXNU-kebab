# Real GOP display enumeration, not GPU/UI acceptance

```sh
python3 scripts/tests/test-gop-display-adt.py --transformer scripts/driver-standardization/adt-gop-display.py --output /tmp/test-gop-display-adt-result.json
```

The transformer is a packaging step BEFORE the existing owner-manifest/boot-measurement refresh and final boot packaging. Its intermediate FAT output is not a ready-to-flash image. It does not change keys, code or policy. See GOP-DISPLAY-ENUMERATION-ACCEPTED-0437.json for actual native reuse, observed original calls and open issues. No private firmware/images, device logs or key material are included.
