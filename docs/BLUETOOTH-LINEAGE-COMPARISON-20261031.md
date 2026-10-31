最新阶段：0423/0424 实机板型与共享锁收尾验收通过；正常默认0424。293 common +4 host /5252源文件一致。

按用户最新安排，蓝牙从现在起延期到GPU、显示、输入和可用SpringBoard之后。实际板型19805、供电拓扑、内部UART、固件/资源只读采集及测试证据已保存；控制器/HCI/RF仍未完成，蓝牙不可用。见[归档与恢复入口](BLUETOOTH-DEFERRED-20261031.md)及[本轮实机/源码证据](qualification/0424/README.md)。下列旧阶段顺序与“待读取板型”描述属于历史，以上述归档为准。

# 蓝牙移植与 LineageOS 对照（2026-10-31）

验收目标是真实控制器工作，并由 iOS 完成扫描、连接、数据传输和重启恢复。UART 内部回环、固件文件读取或 bluetoothd 存活都只是前置条件。

## 同机型的硬件链路

硬件主参考固定为 `LineageOS/android_kernel_oneplus_sm8250` 的 `98a7970af93b7e01ee77156e7fa0eb7e059a57f0`。对应本地 OTA 的四个 DTBO 已分别解析、核对 OTA manifest 中的长度和 SHA；实际板型仍需从设备读取，不能合并四个 overlay 的资源。

| 层 | LineageOS 实现 | HiOS 当前状态与接入要求 |
| --- | --- | --- |
| 板型与引脚 | kona DTS/DTBO，QCA6390，BT_EN GPIO21，SW_CTRL GPIO124，SE6 UART GPIO16–19 | 内部 UART 已验收；外部引脚及控制器尚未启动。GPIO124 是输入状态信号。 |
| 供电 | `bluetooth-power.c` 先申请电源、可选时钟，再处理复位和状态 | 需要资源持有、共享请求聚合、回读与失败恢复。不能沿用触摸的一次性 rail 白名单冒充长期电源服务。 |
| 电源资源 | `cmd-db.c` 查询名称；该板 DTS 使用 `rpmh-regulator.c` 的 `qcom,rpmh-vrm-regulator` | 原版实际查询取第一个匹配。0420 实机证明 smpa5 有同址重复条目，需验证等价元数据后合并，不能将冲突条目当成功。 |
| 串口 | `msm_geni_serial.c`、vendor `qcom-geni-se.c`，SE6 `0x998000`，SPI607，CTS/RTS | 真实内部9帧1225字节收发与完整重启通过；外部流控、IRQ收包和完整电源生命周期未验收。 |
| 控制器启动 | 此设备实际 vendor HAL 使用 `/dev/ttyHS0` 和 `/dev/btpower` | 需要先读真实 ROM，再选择原 patch/NVM；不能从文件存在推断固件已经运行。 |
| 休眠唤醒 | IBS `FD` WAKE_IND、`FC` WAKE_ACK、`FE` SLEEP_IND | 需要独立于 H4 payload 的状态机、TX/RX 时钟持有、超时和复位恢复。 |
| 操作系统接口 | Android vendor HAL/HIDL 与系统蓝牙栈 | XNU 必须实现真实 iOS HCI/IOKit 接口；Android 服务接口不能直接替代 iOS ABI。 |

## 上电和共享资源

同提交的 [bluetooth-power.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/bluetooth/bluetooth-power.c) 实际顺序包括电源配置、可选时钟、复位输出低等待50ms、考虑 WLAN reset 的安全置高、等待50ms，再将 SW_CTRL 配为输入并读取。若 WLAN reset 有效且为低，还会等待100ms 让 AON 放电。不能把 SW_CTRL 当电源使能输出，也不能直接省去共享 WLAN 的时序处理。

本地 DTBO entry2/3 的 AON 和 DIG 都引用 pm8150_s6；RFA1 是 pm8150_s5，RFA2 是 pm8150a_s8。entry0/1 的 DIG 和 ASD 不同。对应名称由 [kona-regulators.dtsi](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/arch/arm64/boot/dts/vendor/qcom/kona-regulators.dtsi) 给出，不能按 PMIC SID 猜 RPMh accelerator 地址。`smpa5/smpa6/smpc8` 的实际地址应来自设备 Command DB；表中 checksum 字段在原 ABI 中 unused，不能自造校验算法。

真正的供电后端要按 [rpmh-regulator.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/regulator/rpmh-regulator.c) 的思路聚合共享电源请求。物理电源已经开启不代表 HiOS 拥有其他消费者的票据；不明确的初始状态、RSC 应答或清理结果必须明确保留，不能直接撤销。当前 HiOS 没有发出蓝牙电源请求。

## 固件和 IBS 的参考边界

本机 OTA 的实际 vendor HAL 文件已按 manifest 提取并固定 SHA，能确认 ttyHS0、btpower、IBS 和 Hastings2.0 文件选择线索。原 bluetooth_b 只读加载器已实机读到 `htbtfw20.tlv` 与 `htnv20.bin`，但控制器 ROM、下载和实际运行均未验收。真实地址应来自控制器/本机持久化数据，不能生成地址冒充原设备。

本提交内核中的 [hci_qca.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/bluetooth/hci_qca.c) 可参考 IBS 的 TX/RX 状态、WAKE 重传和时钟投票；[btqca.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/bluetooth/btqca.c) 可参考厂商 HCI 和分段下载结构。但该版本 generic btqca 没有 Hastings 文件分支，不能将它的其他芯片文件选择直接用在本机。

另已阅读 [android_hardware_qcom_bt 的 bt_vendor_qcom.c](https://github.com/LineageOS/android_hardware_qcom_bt/blob/2141b0040b518f9fcbb12be02cd9c962c5a8bc7e/libbt-vendor/src/bt_vendor_qcom.c)。它包含 Hastings 和 Android 设备/过滤服务控制，可用于理解层次，不能据此宣称与本机已提取的较新 QTI HAL 完全相同，更不能把 Android HIDL 当作 iOS 接口。

## 后续实机门槛

1. 明确实际板型和正确电源拓扑，查询资源并验证等价别名，完成有所有权的电源请求及 PMIC 回读。
2. 接通外部 UART/CTS/RTS，读真实 HCI ROM、完成匹配 patch/NVM 下载和复位，确认真实地址和控制器能力。
3. 实现 IRQ 收发、IBS、错误超时、完整内核重启和异常恢复，并回归现有存储与硬件密钥。
4. 接入真实 iOS HCI/IOKit ABI，用真实对端验证发现、连接、业务数据和恢复。成功前 `bluetooth_functional` 保持 false。

保持原版 MACF/watchdog、硬件 Root 和原版 system bag/C/D 加密 Data。A/B、AKS17/35 和可用 SpringBoard 仍未完成。
