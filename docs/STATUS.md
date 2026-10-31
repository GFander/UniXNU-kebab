最新图形/输入前置阶段：0425–0427；正常默认0427。295 common +4 host /5252源码一致。

已修正iOS16 selector74可选追踪契约，并在两次正常启动中加入原版IOHIDFamily/EventDriver闭包；实机确认一个原版IOHIDResource。真实触摸/按键HID事件、GPU、图像呈现与可用SpringBoard尚未完成，主CADisplay断言仍在。见[本轮范围](GRAPHICS-PREREQUISITE-20261031.md)、[最新版指南复核](GPU-DISPLAY-INPUT-REVIEW-0424.md)及[实机/源码证据](qualification/0427/README.md)。蓝牙继续延期至可用SpringBoard之后。以下是历史阶段。

最新阶段：0423/0424 实机板型与共享锁收尾验收通过；正常默认0424。293 common +4 host /5252源文件一致。

按用户最新安排，蓝牙从现在起延期到GPU、显示、输入和可用SpringBoard之后。实际板型19805、供电拓扑、内部UART、固件/资源只读采集及测试证据已保存；控制器/HCI/RF仍未完成，蓝牙不可用。见[归档与恢复入口](BLUETOOTH-DEFERRED-20261031.md)及[本轮实机/源码证据](qualification/0424/README.md)。下列旧阶段顺序与“待读取板型”描述属于历史，以上述归档为准。

当前阶段验收：0422；正常默认0422。292 common +4 host /5249 源码一致。

真实蓝牙供电资源只读发现与等价别名检查已通过0421/0422完整重启实测。控制器供电、HCI、扫描/连接/数据仍未完成，蓝牙整体不可用。见[本阶段范围](BLUETOOTH-POWER-DISCOVERY-20261031.md)、[LineageOS对照](BLUETOOTH-LINEAGE-COMPARISON-20261031.md)和[源码/实机证据](qualification/0422/README.md)。

历史阶段验收：0415；正常默认0417。288 common + 4 host / 5248 源码一致。

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
