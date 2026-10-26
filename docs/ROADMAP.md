# 路线图：从当前状态到 iOS 16 用户态

本文件把 [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)（SM7150，红米
Note 10 Pro，已跑到 iOS 16.7.16 桌面）的 81 个内核补丁逐条映射到 kebab 的现状，
用来回答两个问题：**我们缺什么**，以及**下一步该做什么**。

状态标记：✅ 已有 · 🔄 进行中 · ⬜ 未开始 · ➖ 不适用/不需要

---

## 一、当前定位

ios-sweet 用 **HFS+ 承载完整的 iPhone X 16.7.16 用户态**（launchd → backboardd →
SpringBoard → 桌面），APFS 只用在**主机侧**（`apfs-fuse` 读 IPSW 里的 OS/SystemOS/AppOS
卷，合成一个 HFS+ 镜像）。**手机上从不跑 APFS。**

因此本项目的路线同样以 HFS+ 为终点，APFS 不在关键路径上。

我们在阶段 6 的门口：块设备已通（UFS → `ufsroot0`，229043 MiB），缺文件系统。

---

## 二、逐条映射

### 阶段 A：让内核挂载并执行（当前）

| ios-sweet | 内容 | kebab |
| --- | --- | --- |
| 0001–0002 | 新 clang 兼容、`UIO_STACKBUF` 回移 | ✅ 构建已通过 |
| 0004–0009 | machine config、非 PAC、缺失头文件桩 | ✅ |
| 0010–0012 | 早期帧缓冲控制台、physmap 对齐、TTBR0 之后保留控制台 | ✅ |
| 0013–0014 | 无 corecrypto 的 PRNG、内核 HMAC | ✅ |
| 0015 | trust cache 降级 | ✅ 补丁 `0059` |
| 0016 | 设备树平台专家 | ✅ 补丁 `0060` |
| 0019 | 内置 libpthread | ✅ 补丁 `0073` |
| **0026** | **内置 Apple HFS（hfs-627.100.6）** | 🔄 移植中 |
| 0028 | UFS 根设备（`rd=ufsroot0`） | ✅ 补丁 `0070`–`0072` |
| 0029 | 未密封根卷（跳过 SSV 校验） | ⬜ 挂真实 iOS 根时需要 |
| 0021/0022/0024 | mockfs + RAMDisk 根 | ➖ 设计上不用 RAMDisk |

### 阶段 B：让真实 iOS 用户态开始跑

| ios-sweet | 内容 | kebab |
| --- | --- | --- |
| 0025 | 内置 AMFI（无 entitlements） | ⬜ **关键** |
| 0035 | AMFI entitlements / 平台二进制 | ⬜ |
| 0036 | AMFI `cs_signed` | ⬜ |
| 0037 | bindfs | ⬜ |
| 0038 | AppleKeyStore 桩 | ⬜ **关键** |
| 0039 | 默认关闭 coredump | ⬜ 低 |
| 0040/0045 | IOWatchdog 关闭 | ⬜ **关键**（否则慢启动被杀）|
| 0041 | HFS 链接备注 | ⬜ 低 |
| 0020/0027/0032/0033 | BearSSL AES-CBC / MD5 / SHA-1 / SHA-384-512 | 🔄 SHA-1 已内置；其余补丁已备好未启用 |

### 阶段 C：输出与输入（GUI 与交互）

| ios-sweet | 内容 | kebab |
| --- | --- | --- |
| 0046 | IOSurface + IOMobileFramebuffer（`AppleCLCD`）| ⬜ **GUI 的前提** |
| 0056 | 面板拷贝限流 | ⬜ 依赖 0046 |
| 0058 | HID event services | ⬜ **输入的前提** |
| 0059 | iPhone X 身份（`hw.product` / `hw.target`）| ✅ ADT 已是 D22AP |
| 0060 | 按键（SPMI）+ 触摸（GENI I²C）| ⬜ |
| 0061 | 呈现为 1125×2436 | ⬜ 依赖 0046 |
| 0062 | 触摸/电源/电池 | ⬜ |
| 0063 | Goodix GT9896 协议 | ➖ 面板不同，需自研 |
| 0064 | `PRIV_WORK_INTERVAL`（UIKit CoreAnimation）| ⬜ |
| 0068 | 已映射 surface 的查找语义 | ⬜ 依赖 0046 |
| 0079 | 显示电源管理 | ⬜ |

### 阶段 D：稳定性与通道

| ios-sweet | 内容 | kebab |
| --- | --- | --- |
| 0030/0031 | UFS 写入 + 可写根 | ⬜ 当前只读 |
| 0034 | 4 GiB 以上 DMA | ⬜ 视需要 |
| 0017 | 设备树虚拟定时器中断 | 🔄 GIC/定时器已配，中断链路未端到端验证 |
| 0018 | 启动 CPU 驱动 | 🔄 `drivers/KebabCPU` 已有，多核未验证 |
| 0065/0078 | SMP（PSCI，6→8 核）| ⬜ 当前 `cpus=1` |
| 0042–0044 | 执行日志 / 崩溃日志 | ⬜ 诊断用 |
| 0047 | 持久控制台日志环（userdata 末尾 4 MiB）| ⬜ 依赖 UFS 写入（0030），见第五节 |
| 0048–0054 | /dev/null tee、IOKit trace、sandbox、堆采样 | ⬜ 诊断用 |
| 0066/0067 | 故障区域信息、控制台静音 | ⬜ |
| 0069/0070/0071 | UFS 加速、pid hibernate、触摸边缘 | ⬜ |
| 0072–0077, 0081 | USB 设备模式 / ECM / 实时日志 | ⬜ 可选 |

---

## 三、与参考实现的已知差异（不能直接照搬）

| 项目 | ios-sweet (SM7150) | kebab (SM8250) |
| --- | --- | --- |
| 启动方式 | `fastboot boot` 一个自研 shim，不刷写 | 刷 `boot_b`：Mu-Silicium UEFI + 载荷 |
| 控制台 | 内核直接画框架缓冲 | 同，但经 UEFI GOP 的 framebuffer |
| 触摸 | Goodix GT9896 | 面板不同，需另行适配 |
| 显示 | `AppleCLCD` + KernelIOSurface | 同方向，但需按 SM8250 的 DSI/DPU 写 |
| CPU | 6×A55（A76 未用）| `cpus=1`，多核待做 |
| 根镜像来源 | IPSW → `apfs-fuse` → HFS+ | 同（可复用其 `mkiosroot.sh`）|

---

## 四、建议的推进顺序

```
A. HFS 内置（进行中）
   └─ 最小 HFS+ 根镜像 + 静态 init → 验证 exec          ← 阶段 6/7 打通
B. 真实 iOS 根
   ├─ 0029 未密封根卷
   ├─ 0025 AMFI 内置 + 0038 AppleKeyStore 桩 + 0040 看门狗
   └─ IPSW → HFS+（复用 mkiosroot.sh 思路）→ launchd
C. 显示与输入
   ├─ 0046 IOSurface + IOMobileFramebuffer
   └─ 0058 HID + 0060/0062 触摸与按键
D. 稳定性
   ├─ 0047 持久日志环（摆脱拍照）
   └─ 0065 SMP
```

**每完成一步，都以"屏幕上出现新的东西"为验收** —— 这是本项目一贯的推进方式。

---

## 五、值得优先偷师的两件事

1. **持久日志环**（他们的 0047）：内核把控制台写进 `userdata` 末尾 4 MiB 的环形
   缓冲，panic 后进 recovery 用 `dd` 拉出来。**同代硬件验证可行。**

   **前提是 UFS 可写** —— 他是在 `0030-sweet-ufs-writes` 之后才做这一块的。所以
   顺序固定为：UFS 写入（含缓存一致性、flush、写入边界）→ 日志环。

   曾考虑过更省事的替代方案：XNU 本来就把 panic log 写进 `pram`（我们的 ADT 已配
   `pram` 节点与 `embedded-panic-log-size`），只需 DarwinBoot 开机时读出来打印。
   但那条路仍然要靠看屏幕，且断电即失；而当前用**慢动作录像逐帧截取**已经够用，
   因此不做，等 UFS 可写后直接上日志环。
2. **根镜像构建链**（`tools/mkiosroot.sh` + `sweet-root-tweaks.py`）：把 IPSW 的
   OS/SystemOS/AppOS 三个 APFS 卷合成一个 case-sensitive journaled HFS+，
   再打上跳过 Setup、种子激活、去 baseband、关 watchdog 等 bring-up 改动。
   阶段 B 直接参考，不用重新发明。

   本机已经有**创建** HFS+ 卷的能力：`scripts/build-newfs-hfs.sh` 从 Debian 的
   hfsprogs 源码编译出 `newfs_hfs` / `fsck_hfs`（Debian 版就是 Apple
   `diskdev_cmds` 的 Linux 移植），装到 `~/.local/share/kebab-tools/`，不需要 root：

   ```sh
   scripts/build-newfs-hfs.sh
   dd if=/dev/zero of=root.img bs=1M count=64 status=none
   ~/.local/share/kebab-tools/newfs_hfs -v KebabRoot -J root.img
   ```

   局限：它只能**造卷**，不能往卷里放文件（那需要挂载，要 root 或 macOS）。
   所以带内容的根镜像仍然在 CI 的 macOS runner 上生成 —— 见
   `docs/FIRMWARE.md` 里那条待应用的 workflow 补丁。

---

# 阶段目标

按依赖顺序排列。每一阶段都有可验证的验收条件，未通过不进下一阶段。

## P0 调试基础设施（当前）

内核已经能跑、能挂根文件系统、能把 9.5 KB 的 panic 报告从 USB 送出来。缺的是
"崩溃之后还能继续对话"的能力，它决定了后面每一阶段的排障速度。

| 编号 | 目标 | 验收条件 |
| --- | --- | --- |
| P0-1 | **EP0 进度探针** —— 每次 EP0 状态迁移记录一个打包值（请求号、阶段、完成/错误码），放在 RAM 里而不是走 USB 打印 | panic 报告之后能读出最后一个 EP0 请求走到了哪一步 |
| P0-2 | **panic 后重连** —— 重写 EP0 状态机：总线复位恢复、`SET_CONFIGURATION` 重建、短包/中止/stall、`XferNotReady` 驱动的状态阶段 | 关闭串口再重新打开成功，且能收发 |
| P0-3 | **serial KDP over CDC-ACM** —— 把 `pal_serial_putc/getc` 接到轮询 USB 后端 | LLDB 连上，读寄存器、读内核内存、符号化栈回溯 |
| P0-4 | **崩溃日志持久化** —— panic 文本写入 UFS 保留区 | 重启后能取回上一次的 panic 全文 |

P0-1 必须先做：前三轮重连尝试都是猜测，没有一次命中，原因就是不知道主机卡在哪个
控制请求上。

## P1 真实用户态

现在跑的是桩 `launchd`。真正的目标是用 iOS 16.7.16 的身份启动真实用户态。

| 编号 | 目标 | 验收条件 |
| --- | --- | --- |
| P1-1 | 从 IPSW 取出 `dyld`、共享缓存、`launchd` 与基础库，构建可启动根卷 | 根卷里有真实二进制，且内核能映射执行 |
| P1-2 | `dyld` 加载共享缓存 | `dyld` 打印自己的版本并跳到 `launchd` 入口 |
| P1-3 | 代码签名/AMFI 降级路径 | 未签名二进制能通过 `cs_*` 校验 |
| P1-4 | `launchd` 完成第一轮 bootstrap | 至少 `notifyd`、`configd` 起来，`launchctl list` 有输出 |

### P1 的根卷构成（2026-10-27 更新）

mtree 清单解出来之后（308,564 条路径），有一件事必须先纠正：**OS DMG 不足以启动 P1**。

| 内容 | 位置 | 结论 |
| --- | --- | --- |
| `/usr/lib/dyld` | OS DMG ✅ | 在 |
| `/sbin/launchd` | OS DMG ✅ | 在 |
| `dyld_shared_cache_arm64e` | **不在 OS DMG** | 在 `Cryptex1,SystemOS` |
| `/usr/lib/libSystem.B.dylib` | **不在 OS DMG** | 同样在 cryptex |

`/System/Library/Caches` 里只有 `apticket.der`、`com.apple.factorydata`、
`com.apple.kernelcaches` —— 没有共享缓存。

所以 P1 的根卷至少由两块拼成：OS DMG（4.73 GB）+ SystemOS cryptex（2.74 GB）。
在做 P1-2 之前，必须先能读 cryptex 里的 APFS 文件，这是当前的实际瓶颈。

## P2 输入

屏幕有了但还不能操作。触摸是 GUI 的前置条件。

| 编号 | 目标 | 验收条件 |
| --- | --- | --- |
| P2-1 | `S6SY761` 触摸驱动（QUPv3 SE13，I2C `0x48`，1079×2399，tx/rx 16×36）| 读到原始坐标，打印触点 |
| P2-2 | 接上 `IOHIDFamily`，产生 `IOHIDEvent` | 用户态能收到触摸事件 |
| P2-3 | 按键（电源、音量）走 PMIC/GPIO | 事件可达用户态 |

## P3 显示直驱

当前画面是引导器建立的扫描输出，内核只是 CPU 拷贝图层进去。真正直驱需要自己
配置 DPU/DSI。

| 编号 | 目标 | 验收条件 |
| --- | --- | --- |
| P3-1 | DSI 面板初始化序列（从引导器或面板规格提取）| 自己点屏成功 |
| P3-2 | DPU 图层与合成 | 两个图层叠加显示正确 |
| P3-3 | `IOMobileFramebuffer` 换成直驱后端，`SwapEnd` 走 vsync 中断 | 无撕裂、CPU 占用下降 |

## P4 图形与系统界面

这一阶段才知道"启动 iOS 界面"到底缺什么。

| 编号 | 目标 | 验收条件 |
| --- | --- | --- |
| P4-1 | GPU 通路评估（Adreno 650 无开源驱动）| 明确走 GPU 驱动、软件合成、还是替换合成器 |
| P4-2 | `BackBoard` / `WindowServer` 起来 | 出现系统界面或明确的失败点 |
| P4-3 | 身份一致性收尾（iPhone10,3 / D22AP）| 系统属性、`/product` 与真机一致 |

## 依赖关系

```
P0-1 → P0-2 → P0-3 ─┐
                    ├→ P1 → P2 → P3 → P4
P0-4 ───────────────┘
```

P0 是加速器：它不产出功能，但把后面每一阶段的排障从"猜"变成"看"。P1 是主线，
P2/P3 可以并行推进，P4 依赖前三者。
