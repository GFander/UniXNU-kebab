# 当前状态：0462 实机基线

更新时间：2026-10-31。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常验收 0462，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0462 收据](qualification/0462/0462-qualified-runtime.json)、[原始日志](qualification/0462/0462-media-boot.log) |
| 共同源码 | 326 个共同补丁 + 4 个 Mac 主机补丁，全部 5,261 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0462/public-all-source-replay-0462.json)、[源码清单](qualification/0462/all-mac-source-0462.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；实际进程进入 system-shell check-in 等待，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0462 原生内核：18,944,624 字节，SHA-256 `ab4589701e4db872c5980309af872c18651115c80501860c54b0464c38184b36`。本轮是新增观测和实机取证，不是 B 类密钥实现或可用桌面验收。

## 当前直接阻塞

原版 locationd 的 `cache_encryptedB.db-journal` 在带偏移写入时返回 EPERM(1)，
重复发生，并紧邻原版 APFS class2 新文件密钥 Unsupported。0461有C类数据库fsync成功，
0462有C类数据库/WAL barrier成功；不是所有APFS写入失败。原版数据库工作线程仍在
SQLiteIOERR/sleep5重试。准确文件路径和失败操作已取得，尚未通过真实B类服务实现消除失败。

本机后端目前只支持factoryC/D。下一步审计并接通原版B类公钥/密钥与open/close/lock生命周期，
保留现有硬件Root与旧C/D数据；不改Unix权限、不删除数据库、不降保护类或伪造成功。
原版用户态selector10的精确ABI仍须核对。见[实际I/O证据](LOCATION-DATABASE-IO-20261031.md)
及[B类开发要求](CLASS-B-BACKEND-NEXT-20261031.md)。

下一顺序：

1. 接通真正的B类密钥与生命周期，修复原版journal写入并实机复验；不删除数据库、重建密钥、降级保护类别或改写失败返回。
2. 跑完原版 DataMigration、安装服务和 system-shell check-in，继续逐项处理实际 daemon 功能失败。
3. 依据最新状态继续图形呈现与 GUI 输入，验证真正可见、可交互的 SpringBoard；单独测量物理刷新率。
4. 可用桌面之后恢复蓝牙控制器/HCI/无线业务开发。

## 必须保留的边界

- 已验收密钥策略为 factory 无口令 C/D；A/B 类、口令、多用户、完整 keychain/Apple SEP 尚未完成。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0462 聚合心跳收据覆盖到 321.203 秒，原始日志另有更晚观察；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
