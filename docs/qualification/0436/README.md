# Original daemon call-site sampling, not GUI acceptance

```sh
python3 scripts/tests/run-backboard-user-waits.py --source /path/to/replayed/xnu --qualification docs/qualification/0436 --cache "$HOME/.cache/hios-user-waits"
```

See BACKBOARDD-USER-WAITS-ACCEPTED-0436.json and user-wait-findings.json for exact scope and limitations. No raw original binaries, caches, device logs, key material or user stack data are included. Fixtures use explicit VM/kernel models and verify the complete production sampler bodies.
