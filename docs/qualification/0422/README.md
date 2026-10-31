# Read-only Bluetooth supply discovery 0421/0422

Only real fixed command-DB resource discovery is accepted. Regulator votes, controller power, HCI and RF are unqualified. See BLUETOOTH-POWER-DISCOVERY-ACCEPTED-0422.json. Host tests contain explicit memory/context/mutex/driver stand-ins, not hardware simulation accepted as real Bluetooth.

Run from the repository root:

```sh
python3 scripts/tests/run-bt-power.py --qualification docs/qualification/0422 --cache "$HOME/.cache/hios-bt-power-public"
```

The CPP fixtures include the exact production parser/native-report fragment and actual full USB input/dispatch, pinned by SHA256 receipts. Formal0310–0313 patches and the all-source manifest prove source/build provenance. Original hardware C/D, MKB, defaults and watchdog regressions pass on both complete boots. Last independent readonly Datafsck is0416; no new full System/SystemOS verification or repair claimed.
