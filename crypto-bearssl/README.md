# BearSSL crypto patch set (prepared, not applied)

These patches replace the `corecrypto_*` wrappers in `libkern/crypto` with
self-contained BearSSL implementations so the kernel never dereferences the
kext-provided `g_crypto_funcs` table. They are the reference solution used by
the negroni project, adapted to xnu-8796.141.3.

Status when prepared: all seven apply with `git apply --check`, compile for
arm64 (with and without KEBAB), x86_64 and with SHA2 disabled, and pass 982
test vectors (SHA-1, MD5, SHA-256, SHA-384, SHA-512, HMAC, HMAC-DRBG and
AES-CBC in all key sizes).

They are **not** in `patches/xnu-8796-kebab/` yet because the boot currently
only needs SHA-1, which is solved by `0047-sha1-without-kext.patch` using the
corecrypto functions already linked into the kernel (`ccsha1_di`,
`ccdigest_init`, `ccdigest_update`).

Apply these when a boot stage needs MD5, SHA-384, SHA-512, HMAC-DRBG or AES
before kexts load. Note that 0050 supersedes 0047 (both touch
`corecrypto_sha1.c`); drop 0047 first.

Order: 0050, 0051, 0052, 0053, 0054, 0055, 0056.
