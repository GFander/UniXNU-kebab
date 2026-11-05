# UniXNU-kebab / HiOS

在 **OnePlus 8T（一加 8T，kebab / Qualcomm SM8250）** 上移植原生 XNU，运行原版 **iOS 16.7.16（20H392）用户态**。

项目使用 Mu-Silicium UEFI 和 DarwinBoot 引导自编译 XNU 8796.141.3，通过板级驱动、设备树和固定供体 ABI 接入硬件，再启动原版 launchd、UIKit 与 SpringBoard。用户态来自 iPhone X / D22AP；硬件适配面向 SM8250，不代表完整模拟 Apple A11。

**当前 GPU 里程碑：0609 已完成 A650 真实命令执行、ZAP 认证和确定性离屏绘制。SpringBoard 继续使用 CPU 显示路径，完整 Metal 加速仍在开发。** 当前主线为 `main`。

## 当前开发状态

状态仅覆盖表中限定行为。实机验收、主机测试、交叉编译和静态审计分别记录；代码存在或服务注册不等于功能可用。

| 模块 | 状态 | 已验证范围与边界 |
| --- | --- | --- |
| UEFI / DarwinBoot | 实机启动通过 | 原厂 bootloader → `boot_b` → UEFI → DarwinBoot → XNU；设备专用实验引导链 |
| XNU 内核 | 实机启动通过 | ARM64、16 KiB 页、XNU 8796.141.3 / Darwin 22.6.0；真实 Mach/BSD 用户进程运行 |
| CPU / 内存 | 部分实机验收 | 8 核上线；常规连续窗口为 8 GiB−2 MiB；完整 12 GiB、AMP、DVFS 与休眠唤醒未验收 |
| UFS / APFS / 加密 Data | 限定实机验收 | 真实 UFS、原版 APFS、只读 System、加密 Data 与旧密钥恢复；完整存储 IRQ / 电源生命周期仍有缺口 |
| SpringBoard / UIKit | 基础人工交互通过 | CPU 桌面、点击打开应用和返回主屏已有实测；不代表长期稳定或全部应用兼容 |
| 显示 / 触摸 / HID | 部分实机验收 | CPU 显示、布局、真实触摸和边缘手势已有证据；完整 DPU/DSI 接管、物理 120 Hz 未验收；亮度滑块任务暂缓 |
| GMU / HFI | 限定实机验收 | 固件启动、版本通信、主要初始化消息与 ACK；不将 ACK 扩大为全部频率、带宽和资源效果验收 |
| GPU VM / GX / SQE / CP | 限定实机验收 | 已归属 BO / 地址空间、OOB/GX、SQE/CP、Ringbuffer/IB 和硬件 Fence 路径通过 |
| GPU First Execution | **PASS** | `A650_FIRST_EXECUTION=PASS`；真实 GPU 命令执行及硬件完成结果 |
| GPU First Render | **PASS（0609）** | `A650_FIRST_RENDER=PASS`；两个离屏目标的完整像素、行填充与保护区检查通过 |
| GPU IRQ | **NOT_RUN** | 真实中断投递尚未验收；0609 使用轮询完成路径，GPU IRQ mask 为 0 |
| NativeAdreno v2 | 主机 / 交叉编译通过 | 客户端契约测试通过；真实用户态 GPU UserClient 尚未发布，客户端 BO/Submit/Fence 实机链路未验收 |
| Metal / Turnip | 适配中，实机执行未验收 | `A650_METAL_FIRST_EXECUTION=NOT_RUN`；20H392 私有 ABI 与前后端执行链仍待完成 |
| CoreAnimation GPU 合成 | 未验收 | IOSurface 导入、GPU 合成和 SpringBoard GPU 呈现仍是后续目标 |
| USB / 调试与恢复 | 部分实机验收 | CDC-ACM 控制台、有界诊断及已覆盖的 FASTBOOT/panic 恢复路径；不保证所有失响应场景可软件恢复 |
| 网络 / 音频 / 蓝牙等 | 未完成或未验收 | Wi-Fi、蜂窝、音频、相机、传感器及完整电池/热管理尚无完整可用验收；蓝牙仅有 UART 等前置证据 |

详细边界见[当前状态](docs/STATUS.md)、[功能证据索引](docs/README.md)和[平台基线](docs/PLATFORM-BASELINE-20261029.md)。

## 最新 GPU 里程碑：0609

[0609 资格记录](docs/qualification/0609/first-render-qualified.json)确认以下实机结果：

- **First Execution：PASS。** SQE/CP、IB 与硬件 Fence 提供真实执行和完成证据。
- **ZAP 实际认证：PASS。** PAS INIT、MEM_SETUP、AUTH 返回成功，认证和资源父链独立核验；不以 PAS13 支持查询代替实际认证。
- **安全模式退出：限定 PASS。** 已验证 `CP_SET_SECURE_MODE(0)` 命令及独立 GPU 完成结果；HYP 保护状态和原始安全模式寄存器仍为 UNKNOWN。
- **First Render：PASS。** 两个 **32×32 RGBA8** 离屏目标位于**同一块 64 KiB BO 内的两个不重叠区域**，不是两块独立分配的 BO。硬件 Fence、Ringbuffer 读指针、颜色缓存写回时间戳和缓存语义正确的 CPU 读回均有证据；全部像素四通道、行填充和保护区检查通过，三份 CAP38 一致。
- **安全回归：通过。** 启动时原版安全、加密 Data 与 watchdog 回归覆盖 **304.384 秒**；绘制后另有 **335.275 秒**只读系统/watchdog 观察。后者不等于重新执行全部加密测试。

0609 在 PAS 操作前的 SpringBoard 显示、点击打开应用和返回手势获人工确认；绘制后的人工复验因用户离开保持 **NOT_RUN**。此前的人工 PASS 不转移为绘制后验收。

这些结果证明限定的 A650 2D 离屏绘制能力，**不代表 Metal、Shader/GMEM、CoreAnimation GPU 合成或完整 GPU 加速已经可用**。后续资格见[GPU / Metal 检查点](docs/GPU-METAL-0609-CHECKPOINT.md)和[NativeAdreno 客户端主机记录](docs/qualification/0609/native-client-contract-host-qualified.json)。客户端 iOS 16.7 对象编译使用 SDK 17.5 和显式 macOS IOKit 类型头，不是 20H392 私有运行时 ABI 或插件验收。

实机证据绑定资格文件中的原始 Git 基线、补丁/源码清单、native 和 boot 镜像哈希；后续发布、文档或合并提交的 SHA 不构成新的实机验收。历史 FAIL/PARTIAL 继续保留，包括 0605 像素仍为 `0xA5` 的失败和 0608 在实际 PAS 调用前的拒绝。

## 项目架构

```text
原厂 bootloader → Mu-Silicium UEFI → DarwinBoot → 板级 XNU → 原版 iOS 用户态
                                                       └→ UIKit / SpringBoard（CPU 显示）
```

UEFI/DarwinBoot 负责引导交接与固件描述；XNU 板级适配接入 SM8250 的中断、存储、内存、输入和电源资源；供体组件通过固定 20H392 ABI 与 prelink 路线接入。源码职责和现存布局见[仓库说明](docs/REPOSITORY-LAYOUT.md)。

GPU 开发链路覆盖 **GMU/HFI → GPU VM/GX → SQE/CP/Ringbuffer → ZAP → 离屏渲染 → NativeAdreno / Metal（后续）**。这是依赖概览，不是可任意重排的初始化顺序：PAS 内存准备必须在 GPU VM 发布前完成，实际认证与安全退出按既有生命周期门禁执行。未来用户态路线为 Metal → metal2vulkan → Turnip → A650，仍需逐级验收。详见[GPU 架构](docs/GPU-ADRENO650-ARCHITECTURE.md)和[PAS 生命周期审计](docs/GPU-PAS13-LIFECYCLE-0607.md)。

## 已实现功能与已知限制

### 已实机验收的基础能力

- 原版 launchd/dyld、共享缓存、SystemOS/Cryptex 库路径和后续系统服务运行；USB 控制台、panic/stackshot 与开发诊断已有记录。
- 原版 APFS 的真实加密 Data、factory 无 PIN 条件下 A/B/C/D 文件保护与旧密文重启恢复、有限 keychain 类别 6–11 包封/解封已有分项证据。见[保护数据生命周期](docs/LOCAL-IDENTITY-AND-PROTECTED-DATA-LIFECYCLE-20261031.md)、[A 类保护](docs/CLASS-A-STORAGE-20261101.md)、[B 类与 keychain 边界](docs/CLASS-B-STORAGE-20261101.md)。
- RTC 日历、持久 NVRAM/defaults、原版 watchdog 用户客户端和心跳已有验收；watchdog 后端为软件截止时间观察器，不声称独立 Qualcomm 硬件 WDT。见[时钟与 watchdog](docs/WATCHDOG-CLOCK-20261030.md)及[defaults 生命周期](docs/DEFAULTS-LIFECYCLE-20261031.md)。
- 原生 S3908/GENI 触摸经原版 HID 进入 UIKit；0541 布局与滑动、0544 边缘返回/桌面/多任务手势已有人工确认。见[布局修复](docs/LAYOUT-SCALING-0541.md)、[边缘手势验收](docs/EDGE-GESTURES-0544.md)和[历史桌面截图](docs/qualification/0536/springboard-desktop.png)。早期滑动故障属于历史，长期稳定性仍待验证。

### 部分实现、正在开发与尚未验证

- **安全兼容范围有限：** Qualcomm QSEE/Keymaster 硬件封装 Root 与用途隔离派生密钥不等于 Apple UID/GID、SEP 或硬件 AES。systembag envelope 承载本后端封装记录；口令、多用户、完整 AKS/keychain、全 RAM 擦除及真实 Apple 激活未完成或未验收。软件密码学不能称为硬件加速。
- **图形仍分阶段：** 离屏 2D 通过，真实客户端隔离、Shader、GMEM、IOSurface 导入和 GPU 桌面呈现未验收；当前 CPU 显示路径保留。不能用逻辑刷新率或软件定时器证明物理 120 Hz。
- **系统与外设仍有缺口：** 完整迁移、全部应用安装与运行、长时压力、完整电源管理和休眠唤醒尚无完整验收。蓝牙前置研究不等于 HCI、扫描或连接可用，见[蓝牙延期记录](docs/BLUETOOTH-DEFERRED-20261031.md)。
- **安全门禁保持：** 保留既有 Root/nonce、bank/systembag、加密 Data、MACF/ACL/Sandbox 和 watchdog。策略数据存在精确板级适配，不声称 Apple 原厂签名或安全启动认证；未知接口返回真实错误，不通过绕过授权、伪造完成或隐藏故障取得 PASS。

历史成果与失败见[文档索引](docs/README.md)、[0583 GMU/HFI 限定资格](docs/qualification/0583/limited-gmu-fw-version-sn11-qualified.json)、[0587 OOB/GX 限定资格](docs/qualification/0587/oob-gx-qualified-GPU-PARTIAL.json)和[同代 A650 参考审计](docs/A650-PEER-AUDIT-20261103.md)。阶段编号是构建/诊断/验收序列，不表示每个编号都新增完整功能。

## 构建与开发

```sh
git clone --branch main --recurse-submodules https://github.com/GFander/UniXNU-kebab.git
cd UniXNU-kebab
```

实际路线使用 Apple OSS XNU 8796.141.3、ARM64/16 KiB 页和 `KEBAB` 板级配置；按序应用共同补丁与四个 Mac 主机补丁，经固定 Xcode/SDK/KDK 环境原生构建，再结合合法取得的 20H392 供体输入执行 ABI 核验、prelink、封包和镜像回读。0609 对应 463 个共同补丁、4 个主机补丁与 5,462 项源码清单；这不覆盖外部工具链和私有输入。

**公开仓库尚不能保证从全新环境独立复现完整可刷镜像。** `board.toml` 等历史入口不描述当前完整实机流程，统一配方和仓库标准化仍有缺口；不提供未经复验的一键构建或刷机命令。

| 目录 / 文档 | 用途 |
| --- | --- |
| `package/`、`drivers/`、`firmware/` | 设备 kit、已有驱动组件和固件侧源码/说明 |
| `patches/xnu-8796-kebab/`、`patches/hosts/macos-dxb-13.5/` | 按序回放的板级与主机补丁 |
| `scripts/tests/`、`scripts/qualification/` | 生产代码/契约主机测试与公开证据校验 |
| `docs/qualification/`、`docs/history/` | 分阶段资格资料与历史记录 |

开发入口为[DEVELOPMENT](docs/DEVELOPMENT.md)、[构建基线](docs/BUILD-BASELINE-20261029.md)和[固件流程](docs/FIRMWARE.md)。源码回放需要 Git、Python 3、本地 Apple OSS XNU 仓库及与目标版本匹配的完整源码清单，可参考 `python3 scripts/qualification/replay-xnu.py --help`。公开的 [0587 源码清单](docs/qualification/0587/all-mac-source-0587-expected.json)是历史版本清单，不可直接用于断言当前 0609 源码匹配。触摸记录校验器只检查封存记录，不生成新的人工实测。

设备测试仅使用已确认的 `boot_b`，需通过镜像、构建、设备身份、日志、唯一操作者及恢复路径门禁。development-fast 日常启动不等于全量 System/SystemOS 内容复验；完整扫描和只读 fsck 单独记录。不将格式化 userdata、重建密钥或改写旧证据作为日常修复步骤。

## 开发路线图

1. A650 GPU IRQ 真实投递与有界故障处理的实机验收。
2. NativeAdreno v2 客户端真实 BO/VM/Submit/Fence，以及对象生命周期和隔离。
3. iOS 16.7.16（20H392）Metal 私有 ABI 与正常授权接入适配。
4. 独立 Metal First Execution，验证真实 GPU 完成和正确读回。
5. IOSurface、CoreAnimation 与 SpringBoard GPU 合成和呈现。
6. 后续硬件、存储/电源生命周期、完整内存、应用兼容和稳定性工作。

后续每阶段独立保存源码、构建、实机与失败证据；不将前一阶段的限定 PASS 扩大为完整功能。

## 文档与资格证据

| 入口 | 内容 |
| --- | --- |
| [STATUS](docs/STATUS.md) | 已验收状态、阶段记录和剩余阻塞 |
| [DEVELOPMENT](docs/DEVELOPMENT.md) | 实际工作流、验证层级和发布规则 |
| [GPU 架构](docs/GPU-ADRENO650-ARCHITECTURE.md) | A650 底层与用户态适配框架 |
| [0609 First Render 资格](docs/qualification/0609/first-render-qualified.json) | 真实认证、完成同步、像素与安全观察的绑定记录 |
| [GPU / Metal 检查点](docs/GPU-METAL-0609-CHECKPOINT.md) | IRQ、NativeAdreno v2、20H392 ABI 与未验证范围 |
| [GPU 接续记录](docs/GPU-CONTINUATION.md) | 恢复开发的准确入口和历史失败 |
| [项目文档索引](docs/README.md) | 各子系统、历史里程碑与资格导航 |

历史说明保留当时的状态；当前 GPU 结论以 0609 资格和后续明确验收为准。原始哈希与详细日志留在对应资格资料中，历史 FAIL/PARTIAL 不因新里程碑被删除或重写。

## 参考项目、许可与致谢

感谢 [UniXNU](https://github.com/UniXNU/UniXNU)、[UniXNU 移植指南](https://unixnu.com/zh/porting/)、[UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)、Mu-Silicium、[Apple OSS](https://github.com/apple-oss-distributions)、[LineageOS](https://github.com/LineageOS)、[Linux](https://www.kernel.org/)、[Freedreno / Mesa Turnip](https://docs.mesa3d.org/drivers/freedreno.html)，以及已用于同代源码审计的 [UniXNU-alioth](https://github.com/FFT-infra/UniXNU-alioth) 等项目。参考实现和其他设备的实机结果不替代 kebab 验收。

项目许可见 [LICENSE](LICENSE) 与 [licenses/](licenses/)，第三方来源保留各自许可与署名。仓库发布板级源码、补丁、工具和经审阅的资格资料；不分发 IPSW、Apple 闭源二进制、可刷镜像、私有固件、硬件密钥、keyblob 或私人 Data。
