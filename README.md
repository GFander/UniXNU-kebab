# UniXNU-kebab

在 **一加 8T（kebab / Qualcomm SM8250）** 上移植 XNU，运行原版 **iOS 16.7.16（20H392）用户态**。项目采用 Mu-Silicium UEFI、DarwinBoot、自编译的 XNU 8796.141.3 和供体原版组件的 prelink 路线，参考 [UniXNU 移植指南](https://unixnu.com/zh/porting/) 与 [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)。

**截至 2026-10-31：原版 iOS 用户态、硬件密钥后端、真实加密 Data、8 核和原版 HID 触摸已经有实机证据；尚未显示可用的 SpringBoard。** 当前主线开发分支为 [`ios16`](https://github.com/GFander/UniXNU-kebab/tree/ios16)。本页汇总立项以来的成果，历史阶段的“已完成”不代表整台手机的对应功能已完成。

## 当前基线

| 项目 | 已核对的状态 |
| --- | --- |
| 实机 | OnePlus 8T，12 GB / 256 GB 机型；实际交给 XNU 的连续内存约 7.998 GiB |
| CPU / 内核 | 8 核上线；ARM64、16 KiB 页、XNU 8796.141.3 / Darwin 22.6.0 |
| 用户态来源 | iPhone X / D22AP，iOS 16.7.16，build 20H392；不是对 A11 硬件的完整模拟 |
| 最近正常启动验收 | **0460**；最新人工触摸验收 **0459** |
| 源码核对 | 324 个共同补丁 + 4 个 Mac 主机补丁，5,261 个源码文件与实际构建输入一致 |
| 当前阻塞 | 原版 `locationd` 数据库初始化进入 `SQLITE_IOERR` 重试；迁移、system-shell check-in 和图形呈现尚未完成 |
| 桌面 / 刷新率 | SpringBoard 进程已执行，尚无可用桌面；面板目标 120 Hz，真实时序尚未验证 |

最近实机源码检查点：[4662532](https://github.com/GFander/UniXNU-kebab/commit/466253282294f3f88478bac78bbbec8e6001b90f)。0460 原生内核 SHA-256：

```text
25ab228e22dc4ac29cea74b887f1097ef3409fa8440549e25f4aac60dc642156
```

详见[当前状态与下一步](docs/STATUS.md)、[功能与里程碑证据索引](docs/README.md)和[0460 实机验收记录](docs/qualification/0460/0460-qualified-runtime.json)。编译、源码回放、实机测试分别记录，互不替代。

## 已达成的功能

“已验证”仅覆盖表内所写的行为。比如 HID 接收成功不等于桌面已消费触摸，内部 UART 收发成功不等于蓝牙可连接。

### 启动、内核与调试

| 功能 | 已经实际做到 | 验证边界与资料 |
| --- | --- | --- |
| 完整引导交接 | 原厂 bootloader → `boot_b` → Mu-Silicium UEFI → DarwinBoot → XNU → 原版 launchd；GOP 可显示内核与 panic 文本 | 当前是设备专用实验链；[早期引导记录](docs/BRING-UP-STATE.md)、[正常启动日志](docs/qualification/0460/0460-media-boot.log) |
| XNU 平台接入 | 板级 PlatformExpert、设备树、GICv3、架构定时器、异常、调度、Mach/BSD、内置 pthread 接入并运行真实用户进程 | 不是完整 Apple SoC 电源管理；[驱动标准化](docs/DRIVER-STANDARDIZATION-20261029.md) |
| 8 核与大内存 | PSCI 次级核启动，8 个物理 affinity 的定时器中断、SGI 和本核回调验收；实际可用窗口 8 GiB−2 MiB | 12 GiB 全量、多 bank 接管、AMP 调度、DVFS、挂起/唤醒和系统负载压力尚未验收；[平台证据](docs/PLATFORM-BASELINE-20261029.md) |
| 原版用户态加载 | 原版 launchd/dyld 执行、共享缓存映射、动态配置及依赖接入，后续原版系统服务运行 | 仍有 daemon 失败和系统迁移等待；[用户态资料](docs/IOS-USERLAND.md)、[CoreLocation 现场](docs/CORELOCATION-WAIT-20261031.md) |
| 内核 USB 控制台 | DWC3 设备模式 CDC-ACM 输出，正常命令与有界诊断，实际完整传输超过 16 KiB 环容量的线程报告 | 不是 UEFI 通用 USB、主机模式或 Type-C 全功能；[0460 报告](docs/CORELOCATION-WAIT-20261031.md) |
| panic 与恢复 | 原生 panic、stackshot、回溯及 USB panic 命令；正常同步 FASTBOOT 与已覆盖的 panic FASTBOOT 路径验证 | 内核/USB 尚未启动、控制器完全失去响应等情况不能保证软件恢复；[历史恢复测试](docs/BRING-UP-STATE.md)、[标准化验收](docs/STANDARDIZATION-RESULT-20261029.md) |
| KDP 调试前置 | 历史阶段已接入 CDC-ACM serial KDP，保存 LLDB/线上协议与调试记录 | 这是独立开发模式；不声称所有当前多核场景可继续执行或已重复验收；[历史测试](docs/BRING-UP-STATE.md) |
| 开发诊断与复现 | 固定对象/UUID 的进程与线程采样、退出原因追踪、源码逐文件回放、ABI 检查、打包回读、带哈希的实机记录 | 诊断成功不等于被诊断服务已修复；[开发与验证流程](docs/DEVELOPMENT.md) |

### 存储、加密与原版系统服务

| 功能 | 已经实际做到 | 验证边界与资料 |
| --- | --- | --- |
| UFS 块存储 | 使用真实内部 UFS，识别 GPT/userdata，读写、同步与 SCSI cache flush 接入；IOStorage ABI 核对 | 当前仍含轮询及继承固件状态的路径；完整 IRQ/SMMU/电源生命周期尚未完成；[存储与驱动审计](docs/DRIVER-AUDIT-20261029.md) |
| 原版 APFS | prelink 原版 APFS；真实容器上使用 System、Preboot 和加密 Data，System 保持只读；受控 Preboot 持久写与重启读取通过 | 早期 HFS+ 根卷属于历史；当前路线为 APFS；[Data phase 2](docs/DATA-PHASE2-QUALIFIED-0351.md) |
| Cryptex / SystemOS | 原始 SystemOS 只读介质、挂载身份、共享缓存及相关库路径接通，原版代码从中加载 | owner 内容策略与原版 trust-cache 交接不等于 Apple IM4M/SSV 认证；[启动日志](docs/qualification/0460/0460-media-boot.log)、[Cryptex 库测试](scripts/tests/cryptex-libraries-0378.cpp) |
| 硬件 Root 与 nonce 2212 | 经 Qualcomm QSEE/Keymaster 硬件封装密钥恢复 Root，派生专用 Image4 nonce key，原版 ignition 路径继续启动；复位恢复、认证与失败拒绝已有实测 | 不是 Apple 熔丝 UID/SEP；不声称 UFS ICE 硬件数据加密或物理断电安全验收；[最新密钥回归证据](docs/qualification/0460/0460-systembag-qualified.json) |
| 软件密码学基础 | 内核所需哈希/HMAC、PRNG/DRBG、AES-CBC 与 APFS 所用 XTS 路径接入，正反向/已知答案与真实加密文件验证 | 密钥来源的硬件保护和数据运算实现是两件事，不把软件 AES 称为硬件加速；[平台与密钥里程碑](docs/PLATFORM-BASELINE-20261029.md)、[加密 Data](docs/LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md) |
| 原版 APFS 密钥接口 | FS/FV、卷控制、metadata/每文件密钥与 CPX I/O 接通；原版工具创建并挂载真实加密 Data | 当前为硬件 Root 支撑的 factory 无口令 C/D 策略，未实现全类别；[Data 与密钥生命周期](docs/LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md) |
| C/D 类加密文件 | 真实文件写入、同步、复位后读取、受控 class rewrap；旧明文与原始密文样本保持；私有测试文件的类别生命周期已修复 | 不能推导 A/B 类、锁屏口令、完整 keychain 或 SEP 可用；[0459 验收](docs/qualification/0459/0459-qualified-runtime.json)、[0460 回归](docs/qualification/0460/0460-systembag-qualified.json) |
| 真实 Data 内容与 phase 2 | 恢复 9,624 项固定初始布局、约 511 MB 逻辑文件；FactoryData/Pearl 布局、原版挂载 phase 2 及正常卸载已有实测 | 不伪造校准数据、不预设 setup 完成；原版首次系统迁移仍未完成；[恢复与验收](docs/DATA-PHASE2-QUALIFIED-0351.md) |
| keybagd / system bag | 原版 `MKB_INIT` 完成；真实持久化 systembag envelope、认证 cookie 与实际 Data 映射，重启复用原 bank | envelope 内是本后端真实封装记录，不是 Apple SEP blob；用户态 AKS 仅已覆盖接口可用；[原版 keybagd 验收](docs/KEYBAGD-BOOTSTRAP-0356.md) |
| RTC / 系统日历 | PMIC RTC 读取、持久日历偏移与重启恢复，解决早期系统时间回退引发的时间警告 | 不是可信时间、抗回滚或完整低功耗时钟验收；[时钟报告](docs/WATCHDOG-CLOCK-20261030.md) |
| 持久 NVRAM / defaults | 原生 NVRAM 接口配合加密 Data 双银行文件，保留早期变量；真实 provider 匹配完成，Image4/defaults busy 回到 0 | 不是硬件 NVRAM；非 root 写者和断电撕裂写未完整验收；[60 秒等待根因及修复](docs/DEFAULTS-LIFECYCLE-20261031.md) |
| Sandbox / watchdog 授权 | 原版 Sandbox 执行器与 MACF 保留；owner 策略精确映射真实本机 watchdog 类，watchdogd 允许、无关服务拒绝 | 策略数据经过板级适配，并非字节未改的 Apple 策略；[授权修复](docs/SANDBOX-WATCHDOG-20261031.md) |
| 原版 watchdog 监控 | 原版用户客户端、真实心跳及截止时间持续推进；最新正常回归包含超过 300 秒的监控窗口 | 后端是软件截止时间观察器，尚非独立 Qualcomm 硬件 watchdog；有限窗口不能证明永久稳定 |
| iOS 身份与 ACL | 去除误继承的 macOS 外部 DirectoryService resolver，恢复 Apple 原生本地 UID/GID/GUID、组与 ACL 行为 | 不放宽 MACF、ACL 或文件权限；[原始阻塞与修复证据](docs/LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md) |
| 原版安装服务前置条件 | 修复精确识别出的损坏 Library 目录元数据，原版安装协调器实际创建后续目录并继续执行 | 应用安装全部完成、DataMigration 完成与 system-shell 登记仍未验收；[安装器报告](docs/INSTALLER-LIBRARY-20261031.md) |

### 输入、显示与底层外设

| 功能 | 已经实际做到 | 验证边界与资料 |
| --- | --- | --- |
| SPMI / PMIC / RPMh 基础 | 共享总线事务串行化、类型化服务、真实寄存器回读；触摸供电票据与 PON 复位已有实测 | 通用调压/时钟消费者、完整电源管理与休眠唤醒未完成；[标准化阶段记录](docs/STANDARDIZATION-RESULT-20261029.md) |
| GENI / S3908 触摸协议 | 实际触摸控制器识别、供电、GENI 传输、TCM 帧与报告配置；修复续读前缀和 FIFO 上限问题 | 不是 Android HAL 转发或合成事件；[触摸协议验收](docs/TOUCH-PROTOCOL-20261031.md) |
| 原版 HID 音量键 | 实机音量上/下按下、松开，经自制报告生产者进入原版 IOHIDDevice/Interface/EventDriver | GUI 消费、其他键和完整按键策略尚未逐项验收；[0429/0430 验收](docs/HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 原版 HID 真实触摸 | 五点点击、横竖滑动、双指分离、依次抬起及 10 秒双指保持；新增 **2,030** 份报告被原版 HID 接收，队列丢失 **0** | 实测最多 2 指；描述符 10 指容量不等于 10 指实测。GUI 事件消费、像素对齐和采样率仍未验收；[人工实机证据](docs/TOUCH-PHYSICAL-20261031.md) |
| 物理屏幕与原版显示枚举 | 固件 GOP/控制台可见；真实屏幕 DT 声明、framebuffer 打开与原版显示枚举接通 | 此处不代表已有桌面、CoreAnimation 呈现或 GPU；[显示枚举](docs/GOP-DISPLAY-ENUMERATION-20261031.md) |
| 显示 ABI / 布局修复 | 原版 selector29 显示区域合约修复，3x UI 缩放声明接入，原先主显示/零缩放断言在相应窗口消失 | 未证明完整模式切换与实际刷新；[显示区域](docs/DISPLAY-AREA-20261031.md)、[缩放修复](docs/DISPLAY-SCALE-20261031.md) |
| 蓝牙开发前置资料 | 原固件只读提取、SE6 时钟/FIFO、真实内部 UART 9 组/1,225 字节收发、Command DB 供电资源及板型读取 | **蓝牙不可用**：未完成控制器供电、固件下载、HCI、扫描/连接/业务数据；按计划延期至可用 SpringBoard 之后；[完整归档](docs/BLUETOOTH-DEFERRED-20261031.md) |

## 尚未达成

- **可见、可交互 SpringBoard**：进程执行和同 PID 存活不是桌面验收；系统迁移与 shell check-in 仍有等待。
- **当前数据库故障**：0460 三次实机采样确认原版 locationd 进入 SQLite I/O 错误后的 5 秒重试，实际数据库路径和失败操作尚未确定。不能据此认定是权限、损坏或缺失密钥。0461 是诊断候选，尚未实机验收。
- **真实图形加速与呈现**：A650/Metal、CoreAnimation 到屏幕的完整链路、DPU/DSI 时序和物理 120 Hz 尚未验收；软件定时器或 FrontBoard 上报值不算物理 vsync。
- **完整 AppleKeyStore/安全能力**：A/B 类、口令、完整 keychain、多用户及 SEP 兼容尚未完成；未知接口真实返回错误。
- **完整手机外设体验**：蓝牙无线功能、Wi-Fi、蜂窝、音频、相机、传感器、完整充电/电池/热管理与休眠唤醒，没有本项目的完整可用验收。

后续顺序为：定位实际 I/O 故障 → 完成原版迁移与 shell 登记 → 修复阻塞主线的 daemon 功能 → 推进并验证真实图形呈现和 GUI 输入 → 可用 SpringBoard → 恢复蓝牙任务。不通过隐藏日志、停止 watchdog 或伪造成功缩小问题。

## 从立项到当前的里程碑

| 阶段 | 成果及后续演进 |
| --- | --- |
| 早期 bring-up | UEFI/XNU 入口、16 KiB 页、真实 UFS、HFS+ 首个用户进程、屏幕/USB/panic 调试；旧 HFS+ 路线随后被 APFS 替代 |
| 0173–0225 标准化 | IOKit/storage 生命周期与供体 ABI、SPMI/GENI 分层、复位策略、真实服务与 Preboot 持久写；见[标准化记录](docs/STANDARDIZATION-RESULT-20261029.md) |
| 0226–0244 硬件 nonce | 打通 QSEE/Keymaster、持久硬件封装 Root 与原版 Image4 nonce 2212，PID1 越过 ignition 阻塞 |
| 0247–0274 APFS / SMP | APFS 主线、SystemOS/Cryptex 前置接入、2→4→8 核上线及约 7.998 GiB 连续窗口；随后接通真实 DSC/库路径 |
| 0275–0347 加密 Data | 原版 APFS 密钥服务、卷初始化/挂载、真实 C/D 文件与 rewrap、原始明文/密文保持及存储检查 |
| 0348–0356 原版启动任务 | 原始 Data 内容、FactoryData/Pearl 布局、phase 2 挂载、原版 keybagd/system bag 初始化 |
| 0357–0386 时钟与授权 | RTC 日历、持久 NVRAM/defaults 资源生命周期、原版 watchdog 与板级 Sandbox 策略匹配 |
| 0387–0424 外设研究 | AKS/daemon 诊断及蓝牙固件、时钟、UART、供电资源和板型证据；蓝牙功能延期 |
| 0425–0443 HID / 显示前置 | 原版 HID 闭包、真实音量键、触摸协议；真实显示枚举、区域与缩放修复 |
| 0445–0460 原版迁移追踪 | 修复 factory 状态服务、安装器目录和本地身份解析、保护文件生命周期；真实触摸验收；定位 locationd 原版数据库重试 |

阶段编号是构建/验收序列，表中包含诊断与回归；不表示每个编号都新增了完整功能。更详细的历史与精确边界见[文档索引](docs/README.md)。

## 仓库布局

```text
README.md                         当前功能总览
AGENTS.md                         本仓库协作与证据规则
board.toml / configs/             现存板级描述与配置
feeds.toml / unixnu.lock           组件来源与固定版本
drivers/                          已有驱动子模块
package/                          设备 kit 与构建集成
firmware/                         设备固件侧源码与说明
patches/xnu-8796-kebab/            当前共同补丁，按文件名排序
patches/hosts/macos-dxb-13.5/       四个主机构建适配补丁
scripts/driver-standardization/   ABI、设备树、打包与构建工具
scripts/qualification/            公共源码回放及实机记录校验
scripts/tests/                    生产代码/契约主机测试
docs/STATUS.md                    当前已验收状态与阻塞
docs/qualification/               分阶段固定哈希的验收资料
docs/history/                     已替代的首页、交接和早期草稿
```

本仓库仍保留历史格式的 `board.toml`，其中 standalone、单核等设置不描述目前的完整 prelink 实机流程。**不能把旧入口当作一键重建当前 0460 的配方。** 官方格式 2、结构化 `porting.toml`/CI 和统一构建入口的迁移仍未完成；详见[仓库整理与规范差距](docs/REPOSITORY-LAYOUT.md)。

## 获取源码与验证

```sh
git clone --branch ios16 --recurse-submodules https://github.com/GFander/UniXNU-kebab.git
cd UniXNU-kebab
```

公共源码回放需要 Git、Python 3 和本地 Apple OSS XNU 仓库，脚本固定基提交并应用当前补丁。它不需要手机、IPSW、SDK 或任何密钥：

```sh
git clone https://github.com/apple-oss-distributions/xnu.git ../apple-xnu
mkdir -p .cache/qualification
python3 scripts/qualification/replay-xnu.py \
  --repo . --xnu-source ../apple-xnu \
  --manifest docs/qualification/0460/all-mac-source-0460.json \
  --receipt .cache/qualification/replay-0460.json \
  --cache .cache --expected-common 324
```

校验已公开的真实触摸记录：

```sh
python3 scripts/qualification/verify-touch-live.py \
  --qualification docs/qualification/0459-touch \
  --source-manifest docs/qualification/0459/all-mac-source-0459.json \
  --output .cache/qualification/touch-0459.json
```

上述命令分别检查公开源码与已封存测试记录，不会在手机上重新执行触摸，也不证明新构建的运行状态。实际内核构建依赖固定 Xcode/KDK/SDK、合法取得的供体输入、prelink 工具和私有设备配置，参见[开发流程](docs/DEVELOPMENT.md)及[历史构建基线](docs/BUILD-BASELINE-20261029.md)。当前不提供未经复验的一键刷机说明。

## 证据、许可与致谢

每个已验收小目标分别保存源码补丁、主机测试、构建/ABI、实机日志和哈希收据。正常开发启动使用已记录的 development-fast 策略；不在每次启动重复扫描全部 System/SystemOS 或恢复整个 Data，完整扫描与原版只读 fsck 使用独立验收流程。

仓库发布板级源码、补丁、工具和经审阅的验收资料，不分发 IPSW、Apple 闭源二进制、可刷固件、硬件根密钥、keyblob 或私人 Data。开发者 owner 策略与板级兼容服务不代表通过 Apple 原厂安全启动认证。

感谢 [UniXNU](https://github.com/UniXNU/UniXNU)、[UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)、Mu-Silicium、[Apple OSS](https://github.com/apple-oss-distributions)、[LineageOS](https://github.com/LineageOS) 及相关上游项目。许可见 [LICENSE](LICENSE) 与 [licenses/](licenses/)；各来源保留其自身许可。
