# Fixed backboardd kernel-wait observer

See BACKBOARDD-OBSERVER-ACCEPTED-0435.json for the exact limited scope and rejected0434 evidence. It does not qualify physical touch, presentation, GPU or SpringBoard.

```sh
python3 scripts/tests/run-backboard-observer.py --source /path/to/replayed/xnu --qualification docs/qualification/0435 --cache "$HOME/.cache/hios-backboard-tests"
```

The runner verifies source and fixture hashes, checks full production function bodies and requires six behavioral mutants to fail assertions. Hardware and kernel layouts are explicit models. Private firmware, caches, original device logs and key material are excluded.
