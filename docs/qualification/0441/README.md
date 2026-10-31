# Original blockSystemApp path identified; cause remains open

```sh
python3 scripts/tests/run-ui-kill-link.py --source /path/to/replayed/xnu --qualification docs/qualification/0441 --cache "$HOME/.cache/hios-ui-kill-link"
```

For the intermediate0440 replay, run scripts/tests/run-ui-kill-caller.py against that source using the same qualification directory. Each runner checks exact complete current/parent source before compiling the production observer and original/current syscall bodies. Models do not prove actual Mach state/unwind semantics; the separate real saved LR, frame chain and original binary call-site evidence are in UI-KILL-CALLER-ACCEPTED-0441.json. Raw logs, firmware, caches, executable files and keys are excluded. This is progress, not a fixed SpringBoard, GPU or physical-touch acceptance.
