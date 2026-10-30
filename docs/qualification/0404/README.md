# 0404：SE6 时钟准备及 GENI FIFO 单位修正

见[范围报告](../../BLUETOOTH-SE6-CLOCK-20261031.md)。0402和完整重启0403使用同一原生内核，各有首次时钟获取、重复调用和实际GENI UART微码/几何证据；0404原版只读Data检查通过，0405恢复正常。279 common +4 host /5246文件一致。

```sh
python3 scripts/qualification/replay-xnu.py --repo . --xnu-source /path/to/xnu --manifest docs/qualification/0404/all-mac-source-0402.json --receipt /tmp/kebab-replay-0402.json --expected-common 279
python3 scripts/tests/run-bt-hardware.py --source scripts/tests/bt-hardware-0402.cpp --panic scripts/tests/bt-hardware-panic-0402.cpp --cache /path/to/writable-cache
```

429项主机用例执行实际资源采集、固定映射、时钟及回滚代码、触摸字段读取/回退片段。MMIO、ARM屏障及外围内核接口为明确替身，7个行为突变须编译成功再被断言拒绝。USB完整分发器快照另验证三个命令均在panic拒绝。实际射频、UART字节收发、物理超时注入、触摸整条传输和驱动完整停机/休眠没有验收。

`bluetooth-local-dtbo-audit.json`记录与固定OTA清单匹配的四个不同机型DTBO及外部引用；不是当前bootloader已选择某条目的证明。审计脚本接受`--image`、`--ota`及`--manifest-sha256`（取收据的`pinned_manifest_sha256`），不需要也不分发原始镜像。不同机型的PM8009/ASD供电不得混入kebab。

硬件Root/nonce、原版MKB、C/D明文/原始密文、defaults和原版watchdog分别回归，未禁用监控。保留daily-fast与0341完整系统原始证明；没有每次重扫全部数据。

**蓝牙仍不可用。** 本项证明SoC串口前置资源，尚无QCA控制器上电、HCI、原版iOS消费接口或真实扫描/连接/数据交换。继续实际硬件开发，不能靠服务名称或停止报错代替。

仅发布源码、必要哈希和收据；不发布闭源组件、固件、原始镜像/日志、地址/校准、设备密钥或私人Data。
