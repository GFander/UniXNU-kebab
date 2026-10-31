# Repository working rules

This is the `ios16` OnePlus 8T / SM8250 device port. Read `docs/STATUS.md`
for the latest qualified state, `docs/DEVELOPMENT.md` for the actual workflow,
and `docs/REPOSITORY-LAYOUT.md` before changing build or component layouts.
Historical MIGRATION/RESUME notes do not supersede these current records.

- Communicate with the owner in Chinese. Distinguish source/host tests, native
  build evidence and physical device evidence; never infer a usable feature
  from service registration, process survival or fewer log lines.
- Preserve hardware Root/nonce, the existing class bank/systembag, encrypted
  Data, original MACF/ACL/Sandbox checks and original watchdog monitoring.
  Do not replace real failures with success, suppress logs, skip initialization,
  regenerate keys or downgrade protection classes to pass a test.
- Match the pinned 20H392 donor ABI. Current ordered board patches are in
  `patches/xnu-8796-kebab/`; host changes are in `patches/hosts/`.
- Keep one builder per shared Mac tree and one serial owner per device.
  Revalidate the actual process/session before treating a wait as finished.
- Device testing uses the confirmed device and `boot_b` only. Never reformat,
  repartition or overwrite userdata as an incidental development step.
- Work on Git in the Ubuntu checkout, preserve unrelated changes, and stage
  explicit paths only. Use `GFander <gfander@users.noreply.github.com>` and
  English conventional commits. Fetch before publishing; never force push.
- Publish reviewed source, tests, documentation and sealed evidence only.
  Keep IPSWs, original proprietary binaries, flashable firmware, hardware keys,
  keyblobs, credentials and private Data out of commits and logs.
- Update README/STATUS for qualified milestones. Keep historical evidence
  unchanged and describe remaining scope honestly. Bluetooth is deferred
  until physically usable SpringBoard; panel 120 Hz is not yet measured.
