# Original HID volume-key input and live Data identity

See HID-INPUT-ACCEPTED-0430.json for the exact measured scope. Raw device logs, private firmware and key material are not distributed. No working touch, UI consumption, GPU or usable SpringBoard is claimed.

```sh
python3 scripts/tests/run-hid-input.py --source /path/to/replayed/xnu --qualification docs/qualification/0430 --cache "$HOME/.cache/hios-hid-tests"
python3 scripts/tests/run-data-identity.py --source /path/to/replayed/xnu --qualification docs/qualification/0430 --cache "$HOME/.cache/hios-data-tests"
```

Tests check source/fixture hashes and execute actual production bodies with explicit stand-ins. Six HID and three Data-identity regressions must be rejected by behavioral assertions. Public source references: https://github.com/apple-oss-distributions/IOHIDFamily/tree/IOHIDFamily-1915.140.3 ; this tag is a semantic reference, not a claim of exact donor source equivalence. Exact donor binary hashes and slot/binding audit receipts are recorded separately.
