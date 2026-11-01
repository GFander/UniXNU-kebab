# 当前状态：0478实机里程碑（原版FairPlay资源名现场、加密回归）

更新时间：2026-11-01。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常运行0478，原版FairPlay资源名有界验收，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0478收据](qualification/0478/0478-fairplay-resources-qualified.json)、[原始日志](qualification/0478/0478-media-boot.log) |
| 共同源码 | 337个共同补丁 + 4 个 Mac 主机补丁，全部 5,280 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0478/public-all-source-replay-0478.json)、[源码清单](qualification/0478/all-mac-source-0478.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；实际进程进入 system-shell check-in 等待，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0478原生内核：18,949,360字节，SHA-256
`5ff6bb402b791f10b1565fef87ce4bd8b9f8d8927d603f0b903a8d2eba53ed6d`。
此次仅增强观察器，A/B/C/D及keychain代码不变；非全AKS、迁移或桌面完成。

## 当前直接阻塞

0478取得六份ENOENT后路径快照，集中于dyld缓存目录、Info.plist和日志过滤配置；
未证明是根因，未随意补文件。已覆盖registry入口无事件，不代表全IOKit/Mach。
上游iPadOS17也未显式选FairPlayIOKit，缺组件假设仍需实际消费证据。
[本轮范围](MIGRATION-FAIRPLAY-RESOURCES-20261101.md)。


0477实际PT_DENY_ATTACH成功，NOATTACH置位、TRACED未置位、debugger能力0。
防附加失败假设已排除；原daemon仍退出，几个ENOENT未证明是根因。
[返回现场及范围](MIGRATION-FAIRPLAY-SYSCALLS-20261101.md)。


0476用5份完整原生标签确认：迁移等待目的为com.apple.locationd.synchronous；
locationd另一条请求等待com.apple.fairplayd.versioned，该服务在采样时由launchd
持有且队列6/6。原fairplayd.H2首次记录为status0退出，随后反复启动，实际初始化
退出原因尚待查明。BLS/SpringBoard/backboardd/迁移等待仍在。

0475晚期独立查询距startup note墙钟1039s，原等待仍在；不把此间隔当kernel uptime
测量。完整事务/唯一依赖环和迁移成功均未证明。详见[本轮结果](MIGRATION-SERVICE-IDENTITY-20261101.md)。

顺序：真实密钥/数据库接口与迁移等待 → 迁移/system-shell登记 → 图形及GUI输入。
完整目标继续进行；蓝牙延期至可用SpringBoard之后。

## 必须保留的边界

- 持久bank策略为factory无口令C/D，B与keychain AK/AKU从现有Root/bank用途隔离派生；锁屏口令、多用户、完整keychain/Apple SEP尚未完成；A/B缓存均为内部加密句柄，factory无PIN；非全RAM擦除证明。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0478聚合心跳收据覆盖到313.871秒；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
