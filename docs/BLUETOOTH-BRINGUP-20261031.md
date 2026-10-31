最新阶段：0423/0424 实机板型与共享锁收尾验收通过；正常默认0424。293 common +4 host /5252源文件一致。

按用户最新安排，蓝牙从现在起延期到GPU、显示、输入和可用SpringBoard之后。实际板型19805、供电拓扑、内部UART、固件/资源只读采集及测试证据已保存；控制器/HCI/RF仍未完成，蓝牙不可用。见[归档与恢复入口](BLUETOOTH-DEFERRED-20261031.md)及[本轮实机/源码证据](qualification/0424/README.md)。下列旧阶段顺序与“待读取板型”描述属于历史，以上述归档为准。

最新阶段0422：真实Command DB供电资源只读发现及完全等价条目合并通过重复查询/完整重启；没有电源、GPIO、固件下载或HCI/RF操作。蓝牙整体仍不可用。下一步为实际板型绑定与有所有权的供电后端，再接外部UART和真实控制器。见[阶段报告](BLUETOOTH-POWER-DISCOVERY-20261031.md)、[LineageOS对照](BLUETOOTH-LINEAGE-COMPARISON-20261031.md)。

当前阶段验收：0415；正常默认0417。288 common + 4 host / 5248 源码一致。

真实SE6内部UART收发与恢复已实机验收（0414/0415）：每次9组/1225字节、重复调用及完整重启通过；蓝牙控制器、HCI、扫描/连接/传输仍未完成。见[范围报告](BLUETOOTH-UART-20261031.md)。

# 蓝牙：以真实硬件功能验收

用户明确要求：蓝牙模块必须发挥实际作用；bluetoothd 不退出、不报错或单纯出现 IOService 均不构成功能完成。

## 已确认的退出路径

0387实机 bluetoothd PID85 的加载基址为 `0x104f9c000`，返回地址 `0x104fc3054`，对应原版文件调用点 `0x100027050`。原版20H392 bluetoothd SHA256为 `8b7c0a0bea51544db82269d7f2775ab7996b8740927283428c4753e032df3a8f`。该指令调用 `_errx`，参数 status=0，文本为 `No bluetooth on this device.`；与实机 signal=0/status=0吻合。

静态分支 `0x100026af0` 在读取的6字节本地地址全零时进入此出口；原版函数还查询名为 bluetooth/marconi-bt 的服务和 local-mac-address 属性，并检查蓝牙串口。当前实际内核的 KebabGeniBus 连接的是 SE13/触摸I2C，不是蓝牙UART；没有原生蓝牙HCI传输驱动。不能通过生成地址、伪造服务或删除退出检查把这一缺口标记为完成。

## 固定硬件参考

参考 [LineageOS kernel 98a7970](https://github.com/LineageOS/android_kernel_oneplus_sm8250/tree/98a7970af93b7e01ee77156e7fa0eb7e059a57f0)，而不是套用苹果博通控制器配置。

- [kebab板级DTS](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/arch/arm64/boot/dts/vendor/oplus/kebab/kona-mtp.dtsi)：QCA6390，BT_EN GPIO21、SW_CTRL GPIO124；AON/DIG供电pm8150_s6，RFA1 pm8150_s5，RFA2 pm8150a_s8。
- [QUP定义](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/arch/arm64/boot/dts/vendor/qcom/kona-qupv3.dtsi)：SE6四线UART，MMIO0x998000/0x4000，GIC SPI607，CTS/RTS及唤醒资源；板级DTS启用此端口。需继续与本机固件/板型和Android蓝牙HAL核对，不能仅凭设备树即声称实机传输已验证。
- [蓝牙供电实现](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/bluetooth/bluetooth-power.c)：供电/复位顺序、WLAN共存和SW_CTRL检查的参考；不直接复制GPL代码到现有驱动。

## 开发和验收顺序

1. 核对实际板级资源、控制器固件与NVM版本、出厂地址来源和持久化格式；只读调查期间不改写校准记录，不复用iPhone地址。
2. 接通真实电源、时钟、pinmux、复位与四线UART，完成控制器命令/事件收发及超时恢复；真实读取版本、地址、能力，加载匹配固件/NVM。
3. 按原版iOS消费接口实现HCI/数据通道、生命周期和权限，接入真实bluetoothd；继续实现固件需要的流控/休眠唤醒与WLAN共存。
4. 实机验证BLE扫描发现另一台真实设备、建立连接并交换GATT数据；验证经典蓝牙发现/配对、重连及至少一种实际数据业务。音频须单独验证链路、音频通道和声音，不能把配对成功当A2DP可用。
5. 经蓝牙开关、冷启动、持续流量及错误恢复回归；保留控制器事件和对端证据，继续验证硬件密钥、加密Data与watchdog。

当前全部蓝牙硬件功能门槛仍未通过；本报告是定位与开发约束，不是蓝牙完成收据。原版二进制、固件、校准信息、设备地址和私有日志不随源码发布。

## 本机 LOS 固件链审计补充

本地 `lineage-24.0-20261016-nightly-kebab-signed.zip` 已按 AOSP OTA 描述格式只提取 vendor 分区；263个操作的数据块哈希及最终549879808字节分区哈希逐一吻合。OTA SHA为 `e8d823da5208712379a29e913dbfde65a202162637dfdf2ef7c7c57b8c2bf975`，vendor SHA为 `f17cfaa173f95e22d806dc31db1e4dc11930e9c931b9151b8ee05c7ffab745f4`。这是固定输入和完整性证据，不宣称额外完成了OTA签名验证。

从该分区的真实 QTI HAL 与 fstab 核对到：

- 用户态串口是 `/dev/ttyHS0`，电源控制接口为 `/dev/btpower`；存在IBS WAKE_IND=0xFD、WAKE_ACK=0xFC、SLEEP_IND=0xFE握手，不能只实现裸H4而忽略实际休眠/唤醒状态。
- HAL包含Hastings2.0及 `htbtfw20.tlv`、`htnv20.bin` 等配置分支；具体选择仍须读本机控制器版本，不能凭文件名直接加载。
- `/vendor/bt_firmware` 是独立bluetooth槽分区的只读VFAT挂载点。vendor镜像中的这个目录为空，不代表固件缺失，更不能从别的设备随意补固件。
- 本机bootloader实际返回当前槽b、`bluetooth_b` 大小0x100000；但明确不支持fastboot fetch，因此尚未读取该分区内容。未格式化、写入或替换该分区；调查后恢复正常0390固件。
- `libbtnv.so` 指向 `/mnt/vendor/persist/bluetooth/.bt_nv.bin`；实际地址/记录尚未读取。未来应只读核对本机记录，不覆盖persist，也不生成一个地址来掩盖硬件未接通。

下一步为原生UFS增加固定蓝牙固件分区的只读加载器，验证GPT、VFAT边界和文件内容，在真实UART/电源与iOS消费接口接入后再进行RF扫描/连接验收。所有校准、私有地址和闭源HAL/固件仅保留在所有者工作台，源码发布只含审计工具及必要元数据。


## 0396–0399更新

固定分区只读固件消费接口已通过首次/完整重启读取、硬件密钥/Data/watchdog回归及0398原版只读fsck，默认恢复0399正常用户态。此前“尚未读取bluetooth_b”是历史调查状态；现在已实际读取其中patch/NVM并重复核对哈希。见[范围报告](BLUETOOTH-FIRMWARE-20261031.md)。真实控制器、UART/HCI、扫描、连接、数据业务仍未验收，整体蓝牙功能未完成。


## 0402–0405更新

SE6固定时钟准备、NORTH资源与实际UART微码/FIFO读取在首次/完整重启通过，原版Data只读检查与正常回归通过；既有GENI水位改用实际字数。见[范围报告](BLUETOOTH-SE6-CLOCK-20261031.md)。这是SoC串口前置条件；QCA控制器、电源/引脚、HCI和真实蓝牙业务仍未完成。
