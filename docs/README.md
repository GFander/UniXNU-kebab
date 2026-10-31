# 文档与验收索引

当前能力以[首页](../README.md)和[STATUS](STATUS.md)为准。阶段文档保留当时状态，必须结合日期、版本和原始收据阅读；不要把较早的“下一步”覆盖到后续验收。

最新主线：[0464–0465 原版 keychain C/D](KEYCHAIN-CD-20261031.md)、[0463 密码学基础与 AKS 调用](CLASS-B-CRYPTO-20261031.md)、[0461–0462 数据库 I/O](LOCATION-DATABASE-IO-20261031.md)及[真实 B 类开发要求](CLASS-B-BACKEND-NEXT-20261031.md)。

## 当前入口

| 文档 | 用途 |
| --- | --- |
| [STATUS.md](STATUS.md) | 最新实机基线、候选边界与当前阻塞 |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 实际开发/构建/验证与发布约定 |
| [REPOSITORY-LAYOUT.md](REPOSITORY-LAYOUT.md) | 文件整理、现存入口与上游格式 2 差距 |
| [PLATFORM-BASELINE-20261029.md](PLATFORM-BASELINE-20261029.md) | 补充公开的硬件 nonce/APFS/SMP/内存历史证据 |

## 密钥、存储与原版启动任务

| 文档 | 用途 |
| --- | --- |
| [DATA-PHASE2-QUALIFIED-0351.md](DATA-PHASE2-QUALIFIED-0351.md) | 真实 Data 内容与原版 phase 2 |
| [KEYBAGD-BOOTSTRAP-0356.md](KEYBAGD-BOOTSTRAP-0356.md) | 原版 system bag 与 AKS 用户客户端 |
| [WATCHDOG-CLOCK-20261030.md](WATCHDOG-CLOCK-20261030.md) | watchdog 与 RTC 时间路径 |
| [DEFAULTS-LIFECYCLE-20261031.md](DEFAULTS-LIFECYCLE-20261031.md) | defaults/NVRAM 的真实资源匹配 |
| [SANDBOX-WATCHDOG-20261031.md](SANDBOX-WATCHDOG-20261031.md) | 策略映射、真实授权与心跳 |
| [AUTHENTICATED-DEVICE-STATE-20261031.md](AUTHENTICATED-DEVICE-STATE-20261031.md) | factory 状态读取与管理操作分离 |
| [INSTALLER-LIBRARY-20261031.md](INSTALLER-LIBRARY-20261031.md) | 原版安装器目录修复 |
| [LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md](LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md) | iOS 本地身份/ACL 与保护文件冷启动 |
| [CORELOCATION-WAIT-20261031.md](CORELOCATION-WAIT-20261031.md) | 最新 locationd 数据库重试现场 |

## 输入、显示与图形

| 文档 | 用途 |
| --- | --- |
| [HID-INPUT-AND-DATA-IDENTITY-20261031.md](HID-INPUT-AND-DATA-IDENTITY-20261031.md) | 真实音量键与运行时 Data 设备身份 |
| [TOUCH-PROTOCOL-20261031.md](TOUCH-PROTOCOL-20261031.md) | GENI/TCM/S3908 协议修复 |
| [TOUCH-PHYSICAL-20261031.md](TOUCH-PHYSICAL-20261031.md) | 0459 人工触摸实机验收 |
| [GRAPHICS-PREREQUISITE-20261031.md](GRAPHICS-PREREQUISITE-20261031.md) | 原版 HID 组件闭包 |
| [GOP-DISPLAY-ENUMERATION-20261031.md](GOP-DISPLAY-ENUMERATION-20261031.md) | 真实 GOP 屏幕枚举 |
| [DISPLAY-AREA-20261031.md](DISPLAY-AREA-20261031.md) | 显示区域 ABI |
| [DISPLAY-SCALE-20261031.md](DISPLAY-SCALE-20261031.md) | 真实缩放参数修复 |
| [GPU-DISPLAY-INPUT-REVIEW-0424.md](GPU-DISPLAY-INPUT-REVIEW-0424.md) | 0424 版本的指南复核，需结合最新状态阅读 |
| [DIRECTION-GPU-DISPLAY-INPUT.md](DIRECTION-GPU-DISPLAY-INPUT.md) | 更早设计输入，不作为当前验收 |

## 蓝牙资料（无线功能延期）

| 文档 | 用途 |
| --- | --- |
| [BLUETOOTH-BRINGUP-20261031.md](BLUETOOTH-BRINGUP-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-DEFERRED-20261031.md](BLUETOOTH-DEFERRED-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-FIRMWARE-20261031.md](BLUETOOTH-FIRMWARE-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-LINEAGE-COMPARISON-20261031.md](BLUETOOTH-LINEAGE-COMPARISON-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-POWER-DISCOVERY-20261031.md](BLUETOOTH-POWER-DISCOVERY-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-SE6-CLOCK-20261031.md](BLUETOOTH-SE6-CLOCK-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |
| [BLUETOOTH-UART-20261031.md](BLUETOOTH-UART-20261031.md) | 阶段范围与证据；当前延期边界以 BLUETOOTH-DEFERRED 为准 |

## 原版服务等待与崩溃分析

| 文档 | 用途 |
| --- | --- |
| [AKS-NOTIFY-20261031.md](AKS-NOTIFY-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [BACKBOARDD-USER-WAITS-20261031.md](BACKBOARDD-USER-WAITS-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [BACKBOARDD-WAIT-OBSERVER-20261031.md](BACKBOARDD-WAIT-OBSERVER-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [DAEMON-LIFECYCLE-20261031.md](DAEMON-LIFECYCLE-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [FIXED-UI-WAITS-20261031.md](FIXED-UI-WAITS-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [MIGRATION-PLUGIN-WAITS-20261031.md](MIGRATION-PLUGIN-WAITS-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [MIGRATOR-WAITS-20261031.md](MIGRATOR-WAITS-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [UI-KILL-CALLER-20261031.md](UI-KILL-CALLER-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [UI-KILL-ORIGIN-20261031.md](UI-KILL-ORIGIN-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |
| [USERLAND-CRASHES-20261031.md](USERLAND-CRASHES-20261031.md) | 当时的原版调用/接收端或退出现场，按版本阅读 |

## 早期设计、构建与交接资料

这些文档中的旧当前状态、硬编码卷号、单核/HFS+ 或旧工具命令只代表当时版本。

- [BRING-UP-STATE.md](BRING-UP-STATE.md)
- [BUILD-BASELINE-20261029.md](BUILD-BASELINE-20261029.md)
- [DRIVER-AUDIT-20261029.md](DRIVER-AUDIT-20261029.md)
- [DRIVER-STANDARDIZATION-20261029.md](DRIVER-STANDARDIZATION-20261029.md)
- [EP0-PROBE.md](EP0-PROBE.md)
- [EP0-TEST-PLAN.md](EP0-TEST-PLAN.md)
- [FIRMWARE.md](FIRMWARE.md)
- [GPU-ASSESSMENT.md](GPU-ASSESSMENT.md)
- [HANDOFF-2026-10-29.md](HANDOFF-2026-10-29.md)
- [IMAGE4-BLOCKER-20261029.md](IMAGE4-BLOCKER-20261029.md)
- [IOS-USERLAND.md](IOS-USERLAND.md)
- [PANEL-DSI.md](PANEL-DSI.md)
- [ROADMAP.md](ROADMAP.md)
- [SESSION-2026-10-27.md](SESSION-2026-10-27.md)
- [SESSION-2026-10-28-PRELINK.md](SESSION-2026-10-28-PRELINK.md)
- [STANDARDIZATION-PLAN.md](STANDARDIZATION-PLAN.md)
- [STANDARDIZATION-RESULT-20261029.md](STANDARDIZATION-RESULT-20261029.md)
- [STORAGE-PLAN.md](STORAGE-PLAN.md)

- [B类真实存储、数据库与六类keychain重启恢复](CLASS-B-STORAGE-20261101.md)

- [B类加密缓存、新旧文件与keychain重启恢复](CLASS-B-CPX-20261101.md)

## 原始证据与测试

- [0469重启验收](qualification/0469/)
- [0468加密CPX、源码与测试](qualification/0468/)

- [0467 B存储/keychain冷恢复](qualification/0467/)
- [0466 B后端、源码与测试](qualification/0466/)

- [0465 原版 keychain 完整重启恢复](qualification/0465/)
- [0464 原版 keychain 后端、授权、源码与测试](qualification/0464/)

- [0463 原生密码学自检、AKS 元数据与源码](qualification/0463/)

- [0462 正常运行/源码/失败读写观测](qualification/0462/)
- [0461 路径与类别观测](qualification/0461/)
- [0460 原版接收端观测](qualification/0460/)
- [0459 本地身份与保护文件生命周期](qualification/0459/)
- [0459 人工触摸记录](qualification/0459-touch/)
- [平台历史收据补充公开](qualification/platform-history/)
- [其他阶段验收目录](qualification/)
- [标准化阶段资料](standardization-run-20261029/README.md)
- [公共校验工具](../scripts/qualification/) 与 [生产代码主机测试](../scripts/tests/)
- [原首页、交接和草稿归档](history/README.md)

原始日志/样本保持哈希绑定。源码回放、主机模型和真实硬件各自说明，诊断阶段不自动转化为功能完成。
