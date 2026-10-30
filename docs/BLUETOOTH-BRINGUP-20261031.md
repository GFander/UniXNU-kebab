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
