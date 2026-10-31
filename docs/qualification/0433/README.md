# S3908 protocol prerequisite; physical gestures unqualified

See TOUCH-PROTOCOL-PREREQUISITE-0433.json for the exact limited scope. Native hardware accepted the full128B report configuration; original touch HID device/event driver matched. No physical gesture or usable UI is claimed.

```sh
python3 scripts/tests/run-touch-protocol.py --source /path/to/replayed/xnu --qualification docs/qualification/0433 --cache "$HOME/.cache/hios-tcm-tests"
```

Tests verify hashes and run actual framing/command bodies against explicit I2C models. Six malformed read/write regressions must be rejected. Private firmware, original device logs, cache binaries and keys are excluded. Primary source provenance and direct source URLs are recorded in touch-primary-reference.json.
