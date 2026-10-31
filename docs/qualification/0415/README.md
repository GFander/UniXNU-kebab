# SE6 UART prerequisite 0414/0415

Physical internal loopback only: each boot has repeated 9-frame, 1225-byte bidirectional comparisons and a final independent register snapshot. Native `851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715`; full source replay 288+4/5248. Bluetooth controller/HCI/RF remains unqualified. Original watchdog and hardware-backed factory C/D regressions pass beyond180s. Post-crash original signed read-only Data fsck0416 exits0, encrypted volumeOK, no newwarnings orrepairs. Normal0417 restored and passes original-root/MKB/C,D/defaults/watchdog beyond270s. FullSystem/SystemOS content verification remains the0341reference; current boot remains developmentfast.

Run host fault coverage (explicit MMIO/FIFO/kernel stand-ins):

```sh
python3 scripts/tests/run-bt-uart.py --qualification docs/qualification/0415 --cache "$HOME/.cache/hios-uart"
```

The two test receipts pin complete production snapshots and the source manifest pins their input files. Physical captures remain private; the receipts contain their SHA256 and actual observed gates. No firmware, HAL, addresses, calibration or key material is published.

Published source: [c946dec09558](https://github.com/GFander/UniXNU-kebab/commit/c946dec095581fd98539b7882eea3fd2c20d37e7). Native code is unchanged for maintenance0416 and normal0417.
