# 蓝牙 SE6 时钟与 FIFO 前置条件

本项接通真实 SoC 串口资源的时钟准备和状态读取，仍不是蓝牙功能验收。没有向 QCA 控制器下载固件或发送 HCI，没有修改蓝牙电源、复位或引脚复用，也没有发布一个假控制器来消除 bluetoothd 的退出。

## 实现与实机证据

新增独立 C 板级后端 `kebab_bluetooth.c`。使用 SE6 0x998000、NORTH tile 的 GPIO16–19/21/124 和固定 GCC 资源，与已有触摸 GENI 后端共享 GCC 操作锁。panic、IRQ、禁抢占、禁中断上下文在映射或加锁前拒绝操作；串口 FIFO 数据寄存器不属于只读诊断白名单。

0400 实际读到 UART 引脚为 GPIO、BT_EN/SW_CTRL 低、SE6 时钟缺少投票，因此拒绝盲读被门控的 SE 寄存器。0401 实际加入 bit16 投票，两次轮询后可读 `GENI_FW_REVISION_RO=0x203`。这是 **SoC GENI 协议编号2（UART）、微码修订3**，不是 QCA 控制器 ROM 版本。该寄存器不能证明控制器已经启动或串口线已经收发。

`BTCLK` 只调整 GCC 0x52008 的 SE6/wrap0 三个位；本机首次实际只新增 bit16，其他投票保持。最多200次、每次1微秒轮询；失败时只移除本次新增投票，保留原有和其他消费者的位，不能确认回滚则隔离后续写入。重复调用不重复获取。成功后的 C 后端当前持有时钟至内核重启；完整驱动启停及休眠尚未验收。

wrap0 从 AHB 已有 HWCG 自动门控。按 [gcc-kona.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/clk/qcom/gcc-kona.c) 与 [clk-branch.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/clk/qcom/clk-branch.c) 的实际语义识别该从总线的 HWCG 位；不把这个例外用于 SE6。实机从总线仍为0x80000002，SE6从0x80004220变为0x00004220，串口状态读取成功。

0402 和完整内核重启0403各验证首次准备及重复调用；GPIO控制和控制器复位状态不变，GCC前后差异仅为bit16。硬件Root/nonce、原版MKB、C/D明文/密文、defaults生命周期和原版watchdog均单独回归，未关闭日志或监控。

## 一并修复的真实 FIFO 单位错误

既有触摸 GENI 初始化把 `HW_PARAM_0` bits29:24 的**每字位宽**当作 FIFO 深度。按 [qcom-geni-se.h](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/include/linux/qcom-geni-se.h)，深度在bits21:16、单位为字。新增共用字段解码器并修正现有水位计算；触摸原来的传输选择、寄存器顺序及异常回退保持。

SE6实际参数0x20102864/0x20204800分别为TX16字、RX32字、每字32位。0402/0403实际读取与解码一致。主机测试执行实际触摸字段读取/回退片段及板级后端，不声称重新验收了触摸整条硬件传输或用户输入。

## 固定输入与验证

279 common +4 host，5246源码文件与Mac编译源一致。KDK原生BT/GENI/USB相关编译及全量构建通过；429项实际生产代码主机用例，7个行为突变必须编译成功再被断言拒绝。主机MMIO和屏障是显式替身；USB完整分发器另检查BTFW/BTHW/BTCLK在panic均被拒绝。物理超时/回滚故障注入尚未执行。

本地LOS DTBO与先前固定OTA清单哈希一致。四个DTBO包含不同机型；kebab条目对应上游 `kona-kebab-overlay.dts` 的19805/20809模型，AON/DIG引用同一S6、RFA1为S5、RFA2为S8，无ASD supply。其他机型使用PM8009/ASD的节点不作为kebab的供电依据。fastboot仅返回通用product=kona，不支持projectname查询，尚未从当前硬件读取确切OEM项目号；后续供电仍需结合实际资源发现。原始DTBO及固件不随源码分发。

旧本地FDT工具对未按4字节对齐的嵌入blob使用了文件绝对对齐，导致后三个条目只被读成空根。已修正相对对齐；公开审计脚本隔离每个blob、验证边界和END token并解析全部四个条目。容器格式参考 [AOSP DTBO定义](https://source.android.com/docs/core/architecture/dto/partitions)。

0404原版只读fsck退出0、DataOK、crypto-io启用、无新增警告和修复；0405已恢复正常并回归超过180秒。见[实机收据](qualification/0404/README.md)。仍保留daily-fast和0341完整System/SystemOS参考，不每次重扫全部系统。

下一步是实际UART字节收发、正确的供电/复位与版本匹配、原版iOS消费接口，再与真实对端验证扫描、连接和数据交换。蓝牙、完整A/B/口令/SEP、剩余daemon和可用SpringBoard均未完成，GPU/显示/输入新开发尚未开始。

源码已发布：[a8fa16a256f2](https://github.com/GFander/UniXNU-kebab/commit/a8fa16a256f29f8dbe5a5de104738b76da7000bf)（ios16），远端已核对。
