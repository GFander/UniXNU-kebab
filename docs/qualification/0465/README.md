# 0465: original keychain C/D operation qualification

See [scope and remaining failures](../../KEYCHAIN-CD-20261031.md) and
[0465-qualified-runtime.json](0465-qualified-runtime.json). Original hardware Root,
old bank/C/D files and enabled original watchdog remain under regression.

0465 reuses the exact 0464 native binary/source after a complete kernel restart;
it is a persistence test, not a second source build. Source/build/tests are in
[0464](../0464/). No power-loss safety claim, new touch sampling, full content
scan, Data fsck, AK/AKU/passcode, class B or usable SpringBoard qualification.
The candidate manifest retains its preparation-time flags. Final compiled and
physical state is established by the source manifest and qualified runtime,
not by the earlier preparation flags.
