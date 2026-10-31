# 当前状态：0471实机里程碑（复用0470内核，A锁定准入/冷恢复）

更新时间：2026-11-01。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常运行0471，A类保护/冷恢复有界验收，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0471收据](qualification/0471/0471-A-qualified.json)、[原始日志](qualification/0471/0471-media-boot.log) |
| 共同源码 | 331个共同补丁 + 4 个 Mac 主机补丁，全部 5,278 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0470/public-all-source-replay-0470.json)、[源码清单](qualification/0470/all-mac-source-0470.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；实际进程进入 system-shell check-in 等待，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0470原生/0471复用内核：18,947,880字节，SHA-256
`c0a8b2f44a3eed4eb43f9658d8325d3377620266e4a7026c582855e7d63c8ee2`。
A类原版锁定准入与完整重启恢复、旧B/C/D/keychain保持；非SEP/口令/全AKS/迁移/桌面。

## 当前直接阻塞

0470–0471已接通factory无PIN A类：原APFS锁定已有cached/nocache读写均拒绝且零
传输，解锁及完整重启恢复同一旧key与原密文；旧B/C/D和六类keychain保持。

CoreLocation/BacklightServices/DataMigration等待仍在。需追实际IPC接收者及事务/资源
生命周期，再补实际缺失的原版密钥/数据库接口并完成原版迁移和system-shell登记。
尚未证明唯一依赖环，也没有可用桌面。详见[最新报告](CLASS-A-STORAGE-20261101.md)。

顺序：密钥/数据库剩余接口与迁移真实等待 → 迁移/安装/system-shell登记 →
图形及GUI输入 → 可用SpringBoard后恢复蓝牙。完整目标未完成。

## 必须保留的边界

- 持久bank策略为factory无口令C/D，B与keychain AK/AKU从现有Root/bank用途隔离派生；锁屏口令、多用户、完整keychain/Apple SEP尚未完成；A/B缓存均为内部加密句柄，factory无PIN；非全RAM擦除证明。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0470/0471聚合心跳收据分别覆盖到321.307/329.608秒；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
