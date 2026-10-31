当前阶段验收：0415；正常默认0415。288 common + 4 host / 5248 源码一致。

真实SE6内部UART收发与恢复已实机验收（0414/0415）：每次9组/1225字节、重复调用及完整重启通过；蓝牙控制器、HCI、扫描/连接/传输仍未完成。见[范围报告](BLUETOOTH-UART-20261031.md)。

# 0404/0405历史验收状态（2026-10-31）

新增真实SE6时钟准备和FIFO字段修复：0402/0403各验证首次获取与重复调用，UART协议2、微码修订3、TX16/RX32字且每字32位；0404原版只读Data检查通过，0405正常回归。279 common +4 host /5246源码一致。控制器未上电，无UART字节交换/HCI，蓝牙仍不可用。见[范围报告](BLUETOOTH-SE6-CLOCK-20261031.md)、[实机与源码证据](qualification/0404/README.md)。AKS17/35、真实A/B密钥、剩余daemon和SpringBoard仍待开发；下列为历史基线。

当前分支为iOS16设备移植。目标用户态为iPhone X的16.7.16/20H392，原生XNU固定8796.141.3、16KiB页；源码回放为268 common +4 host /5239文件一致。

| 范围 | 已验证行为 | 边界 |
| --- | --- | --- |
| 原版APFS/Cryptex | 原版System/Preboot只读、真实加密Data，独立SystemOS库路径已接通 | 不是Apple SSV认证；完整系统镜像原始参考0341保留 |
| 硬件密钥 | 既有QSEE硬件Root/nonce恢复，原版MKB_INIT及factory无口令C/D文件明文/密文保持 | 完整A/B、口令、SEP/AKS接口尚未完成 |
| 资源生命周期 | 原版AppleImage4/defaults正常返回且busy归零 | 未关闭watchdog监控 |
| watchdog/Sandbox | owner平台类映射；真实watchdogd允许、本任务无关服务拒绝；原版心跳保持 | 后端是原版截止时间的软件观察器，不是独立硬件WDT |
| 维护验收 | 原版只读fsck退出0、DataOK、crypto-io；已恢复正常0386 | 没有修复或放宽警告白名单 |
| 用户态/UI | 前述被监测的daemon信号崩溃在两次窗口未复现 | classB密钥失败、部分重复启动仍需追踪；SpringBoard显示监控断言未解决 |

详见[Sandbox/watchdog报告](SANDBOX-WATCHDOG-20261031.md)、[实机收据与公共源码回放](qualification/0386/README.md)、[上一轮崩溃分类](USERLAND-CRASHES-20261031.md)。源代码、构建通过和实机行为是独立验收项，不能相互替代。

下一顺序：补齐真实密钥和daemon功能 → 结合最新构建重新审核GPU/显示/输入指南 → 实现对应驱动并实机验证 → 可见可用SpringBoard。全部目标仍在进行，每个后续验收小目标整理并上传一次。

已发布源码检查点：[9f48d5969d63](https://github.com/GFander/UniXNU-kebab/commit/9f48d5969d63128914b37c94145555e7a5d93610)（ios16分支，蓝牙固件只读读取）；[发布核对](qualification/0398/publication.json)。完整SpringBoard目标仍在进行。
