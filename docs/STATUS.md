# 当前状态：0460 实机基线

更新时间：2026-10-31。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常验收 0460，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0460 收据](qualification/0460/0460-qualified-runtime.json)、[原始日志](qualification/0460/0460-media-boot.log) |
| 共同源码 | 324 个共同补丁 + 4 个 Mac 主机补丁，全部 5,261 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0460/public-all-source-replay-0460.json)、[源码清单](qualification/0460/all-mac-source-0460.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；实际进程进入 system-shell check-in 等待，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0460 原生内核：18,944,304 字节，SHA-256 `25ab228e22dc4ac29cea74b887f1097ef3409fa8440549e25f4aac60dc642156`。最近实机源码提交为 [4662532](https://github.com/GFander/UniXNU-kebab/commit/466253282294f3f88478bac78bbbec8e6001b90f)。本次文档提交不改变内核、载荷或实机状态。

## 当前直接阻塞

原版迁移调用者等待 `CLInternalPerformMigration`；0460 固定选择原版 locationd 的三次实机采样覆盖全部 11 个线程，数据库工作线程都经过 SQLite 主错误码 10 对应的重试分支，然后 `sleep(5)`。这证明存在数据库 I/O 错误重试，但尚未证明具体数据库路径、失败的 VFS 操作、权限错误、缺失密钥或损坏原因。调用者和接收端尚无匹配的 IPC transaction ID，不能把所有等待都认定为同一请求。

0461 正在补充原版 locationd 的有界 open/fcntl/fsync 结果观测，目标是拿到真实路径与 errno。候选尚未完成主机/ABI/源码回放和实机验收，**没有作为已修复功能发布或刷入**。不能因构建产物存在就把其列入已验收基线。

下一顺序：

1. 确认实际失败操作，修复底层功能并实机复验；不删除数据库、重建密钥、降级保护类别或改写失败返回。
2. 跑完原版 DataMigration、安装服务和 system-shell check-in，继续逐项处理实际 daemon 功能失败。
3. 依据最新状态继续图形呈现与 GUI 输入，验证真正可见、可交互的 SpringBoard；单独测量物理刷新率。
4. 可用桌面之后恢复蓝牙控制器/HCI/无线业务开发。

## 必须保留的边界

- 已验收密钥策略为 factory 无口令 C/D；A/B 类、口令、多用户、完整 keychain/Apple SEP 尚未完成。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0460 聚合心跳收据覆盖到 321.203 秒，原始日志另有更晚观察；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
