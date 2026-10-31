# Fixed board metadata qualification 0423/0424

See BLUETOOTH-BOARD-ACCEPTED-0424.json and ../../BLUETOOTH-DEFERRED-20261031.md. Actual SMEM identity/TCSR lifecycle is qualified; controller, HCI and RF are not. Bluetooth is deferred until usable SpringBoard by explicit user direction.

Run from repository root:

```sh
python3 scripts/tests/run-bt-board.py --qualification docs/qualification/0424 --cache "$HOME/.cache/hios-bt-board-public"
```

Host fixtures contain exact production fragments and explicit hardware stand-ins, bound by SHA256. They are not physical radio qualification. Formal0314 plus source manifests bind native source. Original encrypted Data and watchdog regressions pass on both boots. No new filesystem repair or full System/SystemOS verification is claimed.
