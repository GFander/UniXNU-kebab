# 0398：蓝牙固件只读加载器

见[范围报告](../../BLUETOOTH-FIRMWARE-20261031.md)。0396、完整重启0397和原版只读fsck0398使用同一原生内核；276 common +4 host /5241源文件一致。正常镜像0399也复用该内核。

```sh
python3 scripts/qualification/replay-xnu.py --repo . --xnu-source /path/to/xnu --manifest docs/qualification/0398/all-mac-source-0396.json --receipt /tmp/kebab-replay-0396.json --expected-common 276
python3 scripts/tests/run-bt-firmware.py --loader scripts/tests/bt-firmware-0396.cpp --panic scripts/tests/bt-firmware-panic-0396.cpp --cache /path/to/writable-cache
```

生产函数主机快照包含显式UFS/锁/内核接口替身，2261项ASAN/UBSAN用例、3个存储行为及2个LFN行为突变拒绝；完整USB分发器另验证panic拒绝不安全诊断。每次生产源变化需重新生成、绑定源码哈希和实测，不能复用旧结果代替新实现。原生KDK编译、全量构建和源码回放分别检查。

0396与0397各两轮实机读取相同patch/NVM字节，公开消费接口再次读取并验证哈希；system bag、C/D密钥、defaults及原版watchdog回归收据单独保存。0398原版签名fsck只读退出0、DataOK、crypto-io启用、无新增警告，不执行修复。0341为先前完整System/SystemOS证明；daily-fast保留，不每次重扫全系统。

这不是蓝牙功能验收：控制器版本、上电、UART、HCI、固件下载、iOS消费接口和真实扫描/连接/数据交换尚未实现或验证。不会发布假服务或地址来消除bluetoothd的退出。参见[硬件功能门槛](../../BLUETOOTH-BRINGUP-20261031.md)。

只发布源码、必要哈希和收据；不分发闭源HAL、固件字节、原版组件/IPSW、地址/校准、设备密钥、私人Data或原始日志。
