# 蓝牙供电资源发现阶段（2026-10-31）

蓝牙整体仍不可用。这次完成的是给真实供电后端使用的固定只读资源发现，不代表蓝牙已经上电、能扫描或能连接。

0421 和完整内核重启后的 0422 各至少两次实际查询固件 Command DB，三条资源稳定一致：smpa5 → `0x00040100`、smpa6 → `0x00040200`、smpc8 → `0x00040400`。GPIO 与 GCC 的独立前后快照完全一致。没有增加触摸电源写白名单、发起 RSC 电源投票或驱动控制器引脚。

解析器按固件保留区边界检查所有表头和条目，检查所选资源的 VRM 类型、对齐、辅助数据范围、等价重复条目的完整地址/版本/辅助数据，并拒绝冲突名称或跨电源地址别名；任何失败均不发布部分结果。支持固件同类 slave 多表，Command DB checksum 按原 ABI 为 unused。正常 BSD 尚未完成和 panic/IRQ/禁中断上下文均在锁和映射前拒绝。

0420 实机证明 smpa5 资源在相同地址有重复条目。参照原版查询规则，0313 合并完整元数据相等的条目并报告真实计数；不同地址、版本、辅助数据长度或内容仍拒绝。

0418 实机发现旧 PWR? 子串扫描截获 BTPWR?，实际执行了触摸只读查询，因此不接受其蓝牙资源测试。0311 修正优先级，测试现已执行真实输入解析器和完整命令分派，覆盖短包、既有 PWR、输入前缀和 KDP 分支；撤销修正会被行为断言拒绝。

主机测试直接使用生产解析器和原生查询逻辑：778 例、6 个行为变异，另有 USB 输入优先级变异。内存、映射、锁、上下文和驱动动作明确替身；ASAN/UBSAN 与 KDK 编译通过。全量构建通过，292 common +4 host 补丁回放后 5249 源文件一致。两次正常实机均保留硬件 Root/nonce、原版 system bag/MKB、C/D 真实加密读写证明、defaults、NVRAM/RTC 和原版 watchdog，观察超过180秒。

当前默认为正常 0422，没有留下 fsck PID1 维护镜像。最新独立 Data 只读检查仍为0416；本阶段没有格式化、修复或重新全量校验 System/SystemOS。A/B、AKS17/35 和可用 SpringBoard 仍待完成。

下一步需读取实际板型，再绑定正确电源关系；AON/DIG 共享 S6 时合并票据，保留其他消费者的请求，并验证真实 PMIC 回读。随后接通 GPIO21 复位、GPIO124 输入状态、GPIO16–19 外部 UART/CTS，读取实际控制器 ROM，按版本下载原固件/NVM，处理真实地址、IBS、原版 iOS 接口及扫描、连接、业务数据和重启恢复。这些均未在本阶段验收。

固定主参考：[Command DB](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/soc/qcom/cmd-db.c)、[板级资源](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/arch/arm64/boot/dts/vendor/qcom/kona-regulators.dtsi)、[实际 RPMh 聚合驱动](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/regulator/rpmh-regulator.c)、[蓝牙上电流程](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/bluetooth/bluetooth-power.c)。

收据：[qualification/0422/README.md](qualification/0422/README.md)。
