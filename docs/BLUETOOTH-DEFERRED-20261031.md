# 蓝牙阶段归档及延期（2026-10-31）

用户决定本轮收尾后跳过蓝牙，将真实蓝牙功能开发移至 GPU、显示、输入完成并进入可用 SpringBoard 之后。蓝牙尚不可用；不以服务存活、错误减少或内部 UART 回环代替无线功能。

## 已保存且已验证

- 0398：原固件文件只读读取；尚未下载到控制器。
- 0404：SE6 时钟、GENI 协议2、微码修订3及真实 FIFO 参数。
- 0414/0415：实际内部 UART，每次9组、1225字节 TX/RX 比较，重复调用及完整重启通过。不是控制器 HCI 数据。
- 0421/0422：Command DB 中 smpa5=0x40100、smpa6=0x40200、smpc8=0x40400；每项两个完整等价别名，冲突仍拒绝。
- 0423/0424：两个完整内核启动，各3次实际 SMEM item135 查询；SMEM v12、OCDT v1、project/dtsi 均19805、记录100字节。实际 TCSR3 获取/释放成功，临时映射回收，独立 GPIO/GCC 前后快照完全一致。TCSR PID1/0 寄存器有同步写入；SMEM 内容、供电票据、GPIO 和固件没有写入。
- 本轮81例生产代码主机测试、9个行为变异及USB解析优先级变异通过；硬件、IRQ、时钟、映射在主机测试中为明确替身。全量构建、293 common +4 host补丁独立回放、5252源文件一致。
- 两次启动保留原硬件 Root/nonce、原版 MKB/systembag、factory C/D真实加密Data、defaults、RTC/NVRAM及原版watchdog心跳超过180秒。0424正常运行，捕获已关闭并固定哈希；最近独立只读Data fsck仍为0416，没有新增全量System/SystemOS校验。

## 实际板型与后续入口

19805匹配已固定OTA的DTBO entry2，AON与DIG共用pm8150_s6（950000µV），RFA1为pm8150_s5（1900000..2040000µV），RFA2为pm8150a_s8（1350000µV）。ASD虽留有电压/电流属性，但没有supply引用，不能因此给ldoa16投票。原始DTBO哈希和提取字段见qualification/0424/bluetooth-19805-dtbo-profile.json。该资料匹配既有OTA清单，不额外声称完成OTA签名验证。

恢复开发先实现有所有权、共享聚合且不破坏其他消费者的RPMh票据及真实PMIC回读。不要直接放宽触摸电源白名单，不碰bootloader使用的TCS0；必须与现有TCS1使用者串行化，明确完成/回收/错误隔离。PMIC寄存器地址须由原始资料验证，不使用猜测。

再接GPIO21复位、GPIO124输入、GPIO16–19外部UART/CTS，读取实际ROM版本，选择匹配的Hastings固件/NVM及真实蓝牙地址，接通IBS和原版iOS HCI/IOKit接口。验收要求真实扫描、连接、业务数据、冷启动和错误恢复。当前ROM、固件下载、地址、HCI和RF均未验证。

LineageOS仅作为对应硬件实现参考，Android HAL不替代iOS接口。固定内核参考98a7970af93b7e01ee77156e7fa0eb7e059a57f0：drivers/bluetooth/bluetooth-power.c、drivers/soc/qcom/smem.c、drivers/hwspinlock/qcom_hwspinlock.c、drivers/regulator/rpmh-regulator.c、drivers/soc/qcom/cmd-db.c。通用btqca没有本机Hastings选择分支，不可直接套用其他芯片固件名。

## 后续顺序

保留真实密钥与加密Data、MACF/Sandbox及watchdog；只处理会阻塞启动主线的剩余服务问题。按最新0424构建审核GPU/显示/输入指南，再推进真实驱动与可见可用SpringBoard；蓝牙排在这之后。未完成A/B、口令、SEP及AKS17/35明确保留为待办，不伪造成功；若成为主线实际阻塞则针对性解决。本归档不代表完整iOS或SpringBoard已完成。

实机与源码收据见[0424验收目录](qualification/0424/README.md)。原始设备日志、私有固件及构建工件留在本地，公开材料仅含补丁、测试和不含密钥的验收元数据。
