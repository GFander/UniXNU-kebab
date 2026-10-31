# 0387：诊断范围验收

见 [诊断报告](../../DAEMON-LIFECYCLE-20261031.md) 和 [真实蓝牙验收要求](../../BLUETOOTH-BRINGUP-20261031.md)。本目录不是所有daemon或蓝牙功能完成证明。

```sh
python3 scripts/qualification/replay-xnu.py --repo . --xnu-source /path/to/xnu --manifest docs/qualification/0387/all-mac-source-0387.json --receipt /tmp/kebab-replay-0387.json --expected-common 269
```

269 common +4 host /5239源文件一致。运行日志由设备所有者保管，仅发布SHA、进程数量及必要诊断元数据。原版bluetoothd和dyld缓存不包含在仓库；只读审计脚本需要所有者自行准备固定SHA的输入。最近独立只读Data检查为0385，本次诊断补丁没有改变文件系统或重新格式化Data。

蓝牙硬件功能全部待验收：不以daemon存活或服务节点出现代替扫描、连接、GATT/经典蓝牙业务和恢复测试。
