# 0389：原版AKS通知状态位

见[范围报告](../../AKS-NOTIFY-20261031.md)。0388和完整重启后的0389使用同一原生内核；源代码为270 common +4 host，5239文件一致。正常默认0390复用该内核。尚未达到SpringBoard，蓝牙硬件功能未通过验收。

```sh
python3 scripts/qualification/replay-xnu.py --repo . --xnu-source /path/to/xnu --manifest docs/qualification/0389/all-mac-source-0388.json --receipt /tmp/kebab-replay-0388.json --expected-common 270
python3 scripts/tests/run-aks-notification.py --source scripts/tests/aks-notification-0388.cpp --cache /path/to/writable-cache
```

主机快照包含该版本实际生产函数和显式IOKit/密钥导出替身，检查16384项及三个行为变异；不是硬件或完整AKS证明。后续生产源变化必须重新生成并比对，不能复用此快照为新实现背书。原版用户态实际103读0、102设1及硬件密钥/加密Data/watchdog回归证据另见收据；未虚构实机设置后再次103的调用。

只发布代码、报告、必要哈希和元数据；不发布闭源HAL、原版组件、固件、IPSW、图像、原始日志、地址/校准记录、密钥或私人Data。蓝牙输入记录明确区别已提取的vendor分区与尚未读取的bluetooth_b固件分区。
