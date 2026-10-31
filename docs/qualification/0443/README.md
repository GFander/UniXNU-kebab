# Missing chosen UI scale repaired; GUI/120Hz not qualified

```sh
python3 scripts/tests/test-display-scale-adt.py --transformer scripts/driver-standardization/adt-display-scale.py --output /tmp/display-scale-test.json
python3 scripts/tests/run-fbs-mode-evidence.py --source /path/to/replayed/xnu --qualification docs/qualification/0443 --cache "$HOME/.cache/hios-fbs-mode"
```

The ADT function and test import without private packing tools. Its payload CLI uses the project's existing FAT/pack-payload helpers. Native source is0442;0443 reuses the exact same kernel with one chosen property. See DISPLAY-SCALE-ACCEPTED-0443.json for real old numeric failures, the later same-PID observations and remaining limitations. Raw logs, images, caches, private data and keys are excluded. Successful mode return/visible GUI and real120Hz were not directly sampled or accepted.
