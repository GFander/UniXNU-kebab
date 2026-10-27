# GPU 评估：Adreno 650、iOS 16 界面与 P3 的接口约束

本文回答 `docs/ROADMAP.md` 里 P4-1 留下的问题：Adreno 650 没有开源 iOS 驱动，
P4 到底走 GPU 驱动、软件合成，还是替换合成器。结论同时约束 P3 的
`IOMobileFramebuffer` 设计。

**结论先行**

1. **P4 走软件合成**：保留 CPU 拷贝的 `IOMobileFramebuffer`，把 `IOSurface` 与
   IOMFB 的 ABI 补到 backboardd 真正使用的子集，让 CoreAnimation 的软件渲染器
   出画面（§2.2）。
2. **不追 AGX**：AGX 是 Apple GPU 的架构（指令集、命令流、驱动栈），不是
   Adreno 的驱动层。给 Adreno 写“AGX 驱动”在方向上就不成立（§2.1）。
3. **不替换 WindowServer**：ios-sweet 的证据显示，iOS 16 的窗口服务器在
   backboardd 里，SpringBoard 依赖它才能初始化；换掉它等于放弃真实 iOS 界面
   （§2.3）。
4. **P3 现在就把 IOMFB 设计成“以 IOSurface 为传输、后端可替换”的标准服务**：
   `SwapEnd` 入队而不是同步 memcpy，支持多图层、显示模式/scale、`SwapWait` 与
   vsync 时间戳。以后无论换 DPU 直驱还是 Adreno 原生渲染，用户态 ABI 都不用改
   （§3.2、§3.3）。
5. **加速是 P4 之后的独立项目**：公开先例里唯一可信的方向是 Adreno 原生
   （Mesa/freedreno 的 OpenGL ES，或 metal2vulkan + Turnip），不是 AGX（§2.4）。

证据标记：**已证实** = 有代码、日志或公开文档；**推断** = 从已知事实推出来的，
没有逐行验证；**未知** = 还没有数据。

---

## 一、iOS 16 的画面是怎么来的

### 1.1 进程与 launchd job

iOS 16.7.16 的系统 job 都写在 `/System/Library/xpc/launchd.plist` 里。ios-sweet
数过这份 plist：**532 个 daemon，66 个 `RunAtLoad`/`KeepAlive`**
（[NOTES.md L155](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L155)）。
与画面直接相关的只有下面几个。

| 组件 | 角色 | 证据 |
| --- | --- | --- |
| `launchd`（PID 1） | 读 launchd.plist，按需拉起 daemon；`com.apple.backboardd` 与 `com.apple.SpringBoard` 都是它的 job | 已证实：ios-sweet 对 plist 的解析与 job 改名（[NOTES.md L110](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L110)） |
| `backboardd` | BackBoard 服务端；HID 事件系统；**窗口服务器（`CAWindowServer`）就在这个进程里**；打开 `IOMobileFramebuffer`、创建 `IOSurface`、驱动 CoreAnimation 渲染器 | 已证实：反汇编到 `StartWindowServer` → `+[CAWindowServer serverWithOptions:]` → `mainDisplay`（[NOTES.md L120](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L120)） |
| `SpringBoard` | 系统 shell：锁屏、主屏、Control Center、通知中心、Settings 的宿主；通过 BackBoardServices（`BKS...`）与 backboardd 通信，通过 FrontBoard（`FB...`）管理 scene | 已证实：`BKSSystemShellService _checkIn` 的同步 XPC 栈（[NOTES.md L81](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L81)）、`FBSystemShellInitialize`（[NOTES.md L126](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L126)） |
| `WindowServer` | 在 ios-sweet 的日志/反汇编里**没有看到独立的 `com.apple.WindowServer` job**；窗口服务器是 backboardd 里的 `CA::WindowServer`，通过 `IOMobileFramebufferCreateDisplayList()` 枚举显示 | 已证实（同一份反汇编）；与 macOS 的独立 `WindowServer` 进程不同 |
| `com.apple.MTLCompilerService` | Metal 的着色器编译 XPC；用到 Metal 时才会被拉起 | 已证实：backboardd 在崩溃前 spawn 过它（[NOTES.md L91](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L91)） |
| `com.apple.datamigrator` / `migrationpluginwrapper` | 数据迁移界面；只在迁移未完成时出现，会阻塞 SpringBoard 的 check-in | 已证实（[NOTES.md L83](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L83)） |

两个容易混淆的点：

* **BackBoard 不是一个进程，是一套服务**。服务端在 backboardd 里，客户端 API 是
  `BackBoardServices.framework`；SpringBoard 还通过 `FrontBoard.framework` 拿显示
  配置和 scene 宿主。`BKS`/`FB` 前缀的调用栈就是这两个框架。
* **WindowServer 与 backboardd 的关系是“宿主”而不是“并列”**。SpringBoard 启动时
  先 `FBSystemShellInitialize`，它要一个主显示；主显示由 backboardd 的
  `CAWindowServer` 通过 IOMFB 枚举出来。没有 IOMFB 服务，SpringBoard 在
  `FBSDisplayMonitor` 就退出（ios-sweet 0045n 的失败点，
  [NOTES.md L126](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L126)）。

### 1.2 IOKit 层

| 服务/类 | 作用 | 在 iOS 16 上的形态 | 证据 |
| --- | --- | --- | --- |
| `IOMobileFramebuffer` | 显示服务：报告尺寸/模式、接收 swap、发 vsync | 具体子类可以是任意名字；ios-sweet 用 `AppleCLCD`，kebab 也用 `AppleCLCD`。框架**不按类名匹配**，只要求注册了 `IOMobileFramebuffer` 子类 | 已证实：`iomfb_match_callback` 只读 `dfr`/`AppleTV`/`external`（[NOTES.md L98](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L98)） |
| `IOMobileFramebufferUserClient` | IOMFB 的 user client：swap 记录、通知端口、display size 等选择器 | 私有 ABI；ios-sweet 逆出 swap 记录 0x46c 字节、选择器 3/4/5/6/…（[NOTES.md L99](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L99)） | 已证实（逆向 + 真机） |
| `IOSurface` | 跨进程共享的像素缓冲；CA 的 layer 后备存储 | 内核服务是 `IOCoreSurfaceRoot`，user client 是 `IOSurfaceRootUserClient`；用户态是 `IOSurface.framework` | Apple 文档说明其用途（[IOSurface](https://developer.apple.com/documentation/iosurface)）；iOS 16 的服务名与选择器由 ios-sweet 逆出（[NOTES.md L102](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L102)） |
| `IOAccelerator` | GPU 加速器家族；CA 用它建 Metal/GLES 渲染器 | iOS 上 backboardd 轮询的是 `IOAcceleratorES`，以及 `AGXAccelerator`/`IOAcceleratorESLegacy`；Metal 还涉及 `IOGPUFamily` | 已证实：backboardd 的 IOKit 查询日志（[NOTES.md L84](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L84)）；`IOGPUFamily` 见 UniXNU 的 AdrenoMetal 依赖（[AdrenoMetal](https://unixnu.com/zh/drivers/adreno-metal)） |
| `IOFramebufferSharedUserClient` | macOS `IOGraphicsFamily` 里 `IOFramebuffer` 的共享 user client | **iOS 的主显示栈不用它**；iOS 走 `IOMobileFramebuffer` 自己的 user client。它是 macOS 血统的对应物 | 已证实：类在 IOGraphics 源码里（[IOFramebufferUserClient.cpp L408](https://github.com/apple-oss-distributions/IOGraphics/blob/main/IOGraphicsFamily/IOFramebufferUserClient.cpp#L408)、[IOFramebuffer.h L277](https://github.com/apple-oss-distributions/IOGraphics/blob/main/IOGraphicsFamily/IOKit/graphics/IOFramebuffer.h#L277)） |
| `IOHIDFamily` | 触摸/按键事件 | 不在显示链路里，但 SpringBoard 的交互依赖它 | 已证实：ios-sweet 的 0058 用 Apple `IOHIDFamily-1915` 源码编译 |

`IOFramebufferSharedUserClient` 值得单独说一句：它是给 **macOS 的
`IOFramebuffer`** 用的，负责把 framebuffer 内存共享给多个客户端。iOS 的
`IOMobileFramebuffer` 是另一套私有家族，有自己的 user client 与 swap ABI。
把 macOS 的 `IOFramebuffer` 驱动照搬过来，iOS 的
`IOMobileFramebuffer.framework` 不会认；反过来，kebab 现在实现的
`IOMobileFramebufferUserClient` 才是 iOS 需要的那个（见
[0093-kebab-display.patch](../patches/xnu-8796-kebab/0093-kebab-display.patch)）。

### 1.3 Metal/AGX 在哪一层

把渲染路径拆开看：

```
UIKit / CoreAnimation
  └─ render server（backboardd 里的 CAWindowServer）
       ├─ Metal  → MTLDevice（AGXMetalA11）→ AGXAccelerator/IOGPU → AGX GPU
       ├─ OpenGL ES → EAGL → GLES 驱动
       └─ CA::OGL::SW（CPU 软件光栅化）→ IOSurface
```

* **AGX 是 Apple GPU 的架构名**，Mesa 的 Asahi 驱动就是给 Apple M1 的 AGX 写的
  （[Mesa Asahi](https://docs.mesa3d.org/drivers/asahi.html)）。Adreno 650 是
  Qualcomm 的 GPU，指令集、命令流、固件都不同（[Mesa freedreno](https://docs.mesa3d.org/drivers/freedreno.html)
  里 A650 是独立的 gen3 配置，固件是 `a650_sqe.fw`）。
* Metal 的着色器先编成 AIR，再由 Apple 的编译器生成 Apple GPU 机器码；
  `.metallib` 里同时有 AIR 与 Apple GPU 机器码（UniXNU 的说明，
  [Adreno740](https://unixnu.com/zh/drivers/adreno740)）。这个编译器是闭源的，
  只面向 Apple GPU。
* Metal 的**用户态驱动**同样巨大：HackiOS 的统计是 `AGXMetalA11` 一个框架就有
  **53 个类、1158 个方法**，`IOGPU.framework` 另有 **711** 个；他们只做了
  AIR→NIR 的编译三分之一，`_MTLDevice`/`IOGPUMetalDevice` 对象图与内核提交 ABI
  都还没开始（[docs/air.md L3-L10](https://github.com/oosexgang/hackios-xnu-kernel/blob/main/docs/air.md#L3-L10)）。

所以“GPU 驱动”在 iOS 上不是一层，而是三层：内核的加速器服务、用户态的
Metal/GLES 驱动、以及着色器编译器。Adreno 650 在 iOS 上三层都没有。

### 1.4 最小可见 UI 与完整 SpringBoard

先把“可见 UI”分成三档，避免把“能画一帧”和“能出 SpringBoard”混为一谈。

| 组件 | 最小可见 UI（我们自己的进程） | 完整 SpringBoard | Metal 应用 |
| --- | --- | --- | --- |
| `launchd` | 需要（现有桩也可以） | 需要 | 需要 |
| `IOMobileFramebuffer` | 需要 | 需要 | 需要 |
| `IOSurface` | 需要（或直接用 default surface） | 需要 | 需要 |
| backboardd / WindowServer | **不需要** | 需要 | 需要（render server） |
| `SpringBoard` | **不需要** | 需要 | 不需要（但正常启动路径要） |
| CA 软件渲染器 | 可选（自己画就不用） | 需要 | 不需要 |
| `IOAccelerator` / Metal | **不需要** | **不需要**（CA 走软件渲染） | 需要 |
| HID / AKS / 身份 / sandbox | 不需要 | 需要 | 部分需要 |

三档的结论：

* **最小可见 UI** 只需要 `launchd`（或我们的桩）+ 一个能打开 IOMFB、拿到
  `IOSurface`、填像素、调用 swap 的进程。它不需要 backboardd、SpringBoard、
  Metal，也不需要 `IOAccelerator`。这是“证明 iOS 内核能显示我们画的东西”的最小
  集合，不是“iOS 界面”。
* **完整 SpringBoard** 需要 backboardd/WindowServer、SpringBoard、正确的显示模式、
  HID、AKS/身份/sandbox/容器；**但不需要 Metal**。ios-sweet 用 CPU 软件渲染跑出了
  Setup、锁屏、主屏、Control Center、通知中心和 Settings（README 的 Display/GUI
  两行，[README.md L20-L21](https://github.com/dipa-studio/ios-sweet/blob/main/README.md#L20-L21)）。
* **Metal 应用** 才需要 Metal/AGX（或等价的翻译层）。在软件合成下，
  ios-sweet 的 Phone/Photos/Messages 是黑屏（[NOTES.md L19](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L19)）。

这也是本项目 P4 的实际目标：不是“最小可见 UI”，而是“完整 SpringBoard”。所以
P4 的必需项是 backboardd/WindowServer + SpringBoard + 显示模式 + 输入 + 身份，
**不是** GPU。

### 1.5 证据边界

* **已证实**：ios-sweet 在同一份 iPhone X 16.7.16 用户态、同一块 1080×2400 面板上
  用 CPU 渲染出了完整 SpringBoard，且补丁集里没有 `IOAccelerator`/AGX；kebab 的
  0093 实现了 IOMFB/IOSurface 的基础子集；Apple/Mesa 的公开文档描述了
  `IOFramebufferSharedUserClient`、IOSurface、Metal、freedreno/Asahi。
* **推断**：Metal 的编译器只生成 Apple GPU（AGX）机器码，所以 Adreno 不能直接
  执行 Metal 二进制；CA 在没有 `IOAccelerator` 时选择软件渲染器（结果被 ios-sweet
  证实，选择逻辑没有被逐行追踪）；DPU 能对 1080×2400 做缩放/居中（要看 P3-2 的
  图层能力）。
* **未知**：kebab 上 backboardd 实际会调用哪些选择器、单核/多核的软件合成帧率、
  a650 的 Darwin 驱动工作量、P3 的 DSI/DPU 点屏细节。

---

## 二、四条路线

### 2.1 (a) 给 Adreno 650 移植/重写一个 AGX 风格的驱动

**要做什么。** 先明确对象：AGX 是 Apple GPU 的架构，Adreno 650 是 Qualcomm 的
GPU。写一个“AGX 驱动”并不会让 Adreno 工作；要让 Adreno 执行 AGX 的命令流和
着色器，只有在 Adreno 上模拟 AGX 这一条路，而 UI 合成需要的内存带宽和指令吞吐
使这条路没有可行性。真正对应“让 Adreno 跑 Metal”的工作是：

1. 一个 Adreno 的内核驱动（KGSL 等价的 UABI：BO、GPU 地址、ring、fence、SMMU、
   GMU/固件、电源与时钟）；
2. 一个 Metal 前端（`MTLDevice`/`IOGPUMetalDevice` 对象图、命令编码、pipeline
   archive、IOSurface 导入）；
3. 一个着色器翻译器（AIR → SPIR-V/NIR → Adreno ISA），或让 Metal 直接用
   Adreno 后端。

这三层没有一层是“AGX 驱动”。

**为什么没人做。** 公开的 iOS-on-Android 项目走的是三条别的路：CPU 软件渲染
（ios-sweet）、Mesa/freedreno 的 OpenGL ES（HackiOS、Redmi Note 12、Pixel 2016）、
Metal→Vulkan（Pixel 4、UniXNU 的 Adreno 740）。公开项目里没有看到给 Adreno 写
AGX 驱动，因为那不是缺的那一层（[How it works](https://hackiosx.dev/how-it-works)、
[Open problems](https://hackiosx.dev/future)）。AGX 的开源实现（Asahi）是给
Apple Silicon 的，不是给 Adreno 的。

**会看到什么。** 在可预见的工期内什么都看不到。即使把 AGX 命令流模拟出来，
性能也不足以跑 UI；而且 Metal 应用仍然需要完整的 `AGXMetalA11` 对象图。

**不会看到什么。** 不会有“Adreno 跑 AGX 二进制”的捷径；也不会有现成的开源
AGX 内核驱动可以搬（Apple 的 AGX 驱动闭源，Asahi 是另一套硬件）。

**第一个实验。** 一天内可以证伪：在 kebab 上调用
`MTLCreateSystemDefaultDevice()`（或让一个最小 Metal 程序建 device），没有
AGX/`IOAccelerator` 时返回 nil；再用 `llvm-objdump` 或 freedreno 的反汇编器看
iOS 的 `.metallib`，确认里面是 AGX 机器码而不是 Adreno ISA。这个实验的结论是
“Metal 不可用”，不是“AGX 能移植”。

**结论：拒绝。** 它不是“难”，是方向不对。

### 2.2 (b) 软件合成：保留 CPU 拷贝的 framebuffer，补一个最小 `IOAccelerator`/`IOSurface` 路径

**要做什么。** 不是“补一个假的 `IOAccelerator`”。ios-sweet 的做法是**不实现
`IOAccelerator`**，让 CoreAnimation 走它自带的软件渲染器
`CA::OGL::SWContext`（ios-sweet 在 QuartzCore 里找到 `sw_new_context`、
`gles_new_context`、`metal_new_context`、`new_null_context` 四个入口，
[NOTES.md L117](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L117)）。
需要补的是显示与 surface 的 ABI：

1. `IOMobileFramebuffer`：显示尺寸/模式、swap begin/set-layer/end/wait、vsync 通知、
   frame info、power；
2. `IOSurface`：create/lookup/lock/unlock/release、values、bulk attachments、mach port
   查找；
3. 显示模式：向 iOS 呈现 iPhone X 的 **1125×2436 @3x、60 Hz、sRGB**，物理面板
   1080×2400 居中（ios-sweet 的 0061，
   [NOTES.md L30](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L30)）；
4. 呈现节流：CPU 拷贝不要每次 swap 都做，按 vsync 定时器限到 10–30 Hz
   （ios-sweet 的 0056，
   [NOTES.md L81](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L81)）。

**已经证明的部分。** ios-sweet 跑的是**同一份 iPhone X 16.7.16（20H392）用户态**，
面板也是 **1080×2400**，和 kebab 的显示几何几乎一样；它用 CPU 渲染出了 Setup、
锁屏、主屏、Control Center、通知中心、Settings，帧率约 **20–30 fps**，并且
**没有 GPU 驱动**（[README.md L7](https://github.com/dipa-studio/ios-sweet/blob/main/README.md#L7)、
[L20-L21](https://github.com/dipa-studio/ios-sweet/blob/main/README.md#L20-L21)、
[L32](https://github.com/dipa-studio/ios-sweet/blob/main/README.md#L32)）。
它的补丁集（0001–0081）里没有 `IOAccelerator`/`AGX` 相关补丁（本文核对时
`xnu/patches` 下无匹配）。这是这条路线最强的证据：**不是理论可行，是在同代
Qualcomm 硬件、同一版用户态上已经做到过**。

**kebab 现在走到哪。** 当前 PID 1 还是桩 `launchd`，真实 `dyld`/共享缓存属于
P1；下面的 ABI 工作可以先做，但 backboardd 的验收要等 P1。
`0093-kebab-display.patch` 已经实现了
`IOMobileFramebuffer`/`AppleCLCD`、`IOCoreSurfaceRoot`/`IOSurfaceRootUserClient`、
4 个图层、60 Hz 定时器 vsync、`SwapBegin`/`SwapEnd` 的 CPU 拷贝。但它是按
“能启动、能打印日志”写的，不是按 backboardd 的完整调用集写的：

| 项 | 现在（0093） | backboardd/CA 会用到 | 影响 |
| --- | --- | --- | --- |
| IOMFB 选择器 | 3/4/5/6/7/8/18/28/72；`SwapWait`(6) 是空实现 | 3/4/5/6/12/15/17/22/27/29/33/50/55/57/68/74/79/83/84 | 缺的会被当成错误或空值；`SwapWait` 不等待 |
| IOSurface 选择器 | 0/4/13/34/35 + trap 0–5 | 9（SetValue）、0x1b（SetBulkAttachments）、0x22/0x23 等 | backboardd 创建 surface 后会设值/设附件；缺了可能拿不到正确的像素格式/布局 |
| 显示模式 | `GetDisplaySize` 返回 1080×2400 | 1125×2436 @3x、60 Hz、sRGB | `FBSDisplayMode` 要校验 width/height、scale、refresh；ios-sweet 在 0052n 因 scale 为 0（`1080x2400/0`）死在这里，0061 把呈现尺寸改成 1125×2436 |
| 呈现 | `SwapEnd` 内同步 memcpy | 入队、vsync 完成、`SwapWait` 可等 | 单帧 10.4 MB 同步拷贝会占满 CPU，且没有完成语义 |
| 通知 | 单一端口、按 type 存回调；vsync 用裸 `mach_absolute_time` | 按 type 的 enable/disable、时基转换、frame info | 时间戳单位可能不对，通知会丢 |

这不是 GPU 问题，是 **P4 的显示 ABI 缺口**。它比 GPU 问题更早挡路，而且无论
选哪条 GPU 路线都要补。

**会看到什么。** 补完之后：迁移/启动进度条、锁屏、主屏、Setup、Settings、
Control Center、通知中心、触摸与按键；20–30 fps（如果 SMP 把 A77 带起来，单核
性能会比 ios-sweet 的 A55 好；当前 `cpus=1`，实际跑在哪个核上要确认）。

**不会看到什么。** Metal 应用（Phone/Photos/Messages 这类）黑屏；游戏、相机预览、
部分第三方 App；高负载下动画掉帧、发热。CPU 占用会很高。

**第一个实验。** 先做零成本的 ABI 对照（§4 第 1 项），再做 ABI 一致性客户端
（§4 第 3 项）：一个静态 arm64 程序创建两个 1080×2400 BGRA surface，填图案，
按 backboardd 的顺序调用 IOMFB（3/4/5/6 + 12/57/74/84）与 IOSurface（9/0x1b），
看面板是否出稳定图案、`SwapWait` 是否返回、vsync 时间戳是否单调。它验证的是
内核侧 ABI，不是 CA 的选择逻辑；CA 的选择逻辑由 ios-sweet 的经验证据覆盖。

**结论：采用。**

### 2.3 (c) 替换合成器：跑真实用户态服务，但用自己的窗口/合成服务替代 WindowServer

**要做什么。** 写一个自己的 compositor，替代 backboardd 里的 `CAWindowServer`，
再让 SpringBoard 或我们自己的 shell 连上去。问题是 SpringBoard 的启动路径要求
`FBSystemShellInitialize` → `FBSDisplayMonitor` → 主显示，主显示必须来自
`CAWindowServer`；没有看到它支持“外部合成器”的接口。ios-sweet 在 0045n 已经
看到 SpringBoard 因为没有主显示而在 `FBSDisplayMonitor` 退出
（[NOTES.md L126](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L126)）。

**会看到什么。** 如果我们放弃 SpringBoard，只跑自己的 shell，可以看到“我们画的
界面 + 部分 iOS daemon”，但它不是 iOS 界面：锁屏、主屏、Control Center、通知、
Settings 都是 SpringBoard/BackBoard 的，不会出现。

**不会看到什么。** 真实 SpringBoard 界面；除非改 SpringBoard 或反向它的
FrontBoard/BackBoard 协议，那等于维护一个私有分支。

**第一个实验。** 用一个只实现 `CAWindowServer` 最小接口的桩替换 backboardd，
启动 SpringBoard；预期在 `FBSystemShellInitialize` 失败。这个实验几小时就能做完，
结论大概率是“要改 SpringBoard 才能过”，也就是放弃真实用户态。

**结论：拒绝。** 它和“启动 iOS 16.7.16 用户态并显示界面”的目标相反。

### 2.4 (d) 别的路：Adreno 原生加速（OpenGL ES 或 Metal→Vulkan）

这是本文认为唯一值得在 P4 之后考虑的加速方向，但要和 P4 分开。

**d1：Mesa/freedreno 的 OpenGL ES。** HackiOS 在 Adreno 642L 上把 Mesa 的
freedreno 移植进 iOS，给 CoreAnimation 提供 OpenGL ES；GPU 渲染可用，但
**Metal 应用仍然不行**（HackiOS README 的对照表：GPU 渲染 = OpenGL ES through
Mesa，Metal = 有 shader 编译器、没有 Metal，
[README.md L47](https://github.com/oosexgang/hackios-xnu-kernel/blob/main/README.md#L47)）。
对 kebab 有利的是：Mesa 的 freedreno 明确支持 A650（gen3），固件是
`a650_sqe.fw`（[Mesa freedreno](https://docs.mesa3d.org/drivers/freedreno.html)）。
代价是要在 xnu 里写一个 KGSL 等价的 KMD：BO/GPU 地址/ring/fence/SMMU/GMU/电源，
Linux 的 MSM/KGSL 是 GPL-2.0（[adreno_gpu.c](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/msm/adreno/adreno_gpu.c)），
不能直接搬，只能按 UABI 重写。HackiOS 已经证明这条路能走通一代硬件，但不是
a650。

**d2：Metal→Vulkan（metal2vulkan + Turnip）。** UniXNU 在 Adreno 740 上把
Metal 设备通过 `MTLAddDevice` 发布，`metal2vulkan` 把 AIR 翻成 SPIR-V，
Turnip 再编成 Adreno IR3，内核侧是 Adreno740 KMD；GPU 合成桌面已经工作
（[Adreno740](https://unixnu.com/zh/drivers/adreno740)、
[AdrenoMetal](https://unixnu.com/zh/drivers/adreno-metal)）。代价更大：UniXNU
的文档明确说“其他 GPU 代际需要先实现对应内核后端和编译器支持”，a740 的
KMD/PM4 不能直接用于 a650（[GPU 与 Metal 适配](https://unixnu.com/zh/porting/gpu)）。
HackiOS 的 AIR 编译器也只覆盖了 456 个模块里的 321 个，`_MTLDevice` 对象图还没开始
（[docs/air.md L17-L26](https://github.com/oosexgang/hackios-xnu-kernel/blob/main/docs/air.md#L17-L26)）。

**会看到什么。** d1：CA 的 UI 用 GPU 渲染，CPU 占用下降，帧率提高；Metal 应用仍
黑屏。d2：Metal 应用有机会运行（在 a740 上已经跑通），但 a650 需要新的内核后端、
PM4 编码和 Turnip 目标，工作量以人年计。

**不会看到什么。** 两条路都不会让 AGX 二进制在 Adreno 上跑；它们都是“在 Adreno
上重新实现 Metal/GLES”，不是“移植 AGX”。

**第一个实验。** 在 kebab 的 LineageOS 上跑 freedreno 的 GLES 三角形和 Turnip 的
Vulkan 三角形，确认 a650 的 userspace 驱动、固件、电源/时钟可用；再用 HackiOS 的
Darwin KGSL 胶水估算内核侧缺口。1–2 天能给出“userspace 可行、内核要重写多少”的
量级，不需要动 xnu。

**结论：作为 P4 之后的独立项目保留，不作为 P4 的前置。**

### 2.5 四条路线对照

| 路线 | 工作量 | 会看到 | 不会看到 | 第一个实验 | 结论 |
| --- | --- | --- | --- | --- | --- |
| (a) AGX for Adreno | 人年～不可行 | 没有可行产物 | 一切 | 创建 Metal device，预期 nil | 拒绝 |
| (b) 软件合成 | 数周～数月（补 ABI + 调性能） | 完整 SpringBoard、20–30 fps | Metal 应用、游戏、相机 | ABI 对照 + 一致性客户端 | **采用** |
| (c) 替换合成器 | 数月，且要改 SpringBoard | 自己的 shell + 部分 daemon | iOS 界面 | 用桩替换 backboardd 启动 SpringBoard | 拒绝 |
| (d) Adreno 原生 | d1 数月～人年；d2 人年 | d1：GPU 渲染 UI；d2：部分 Metal 应用 | AGX 兼容性 | a650 上跑 freedreno/Turnip 三角形 | P4 后再说 |

---

## 三、建议

### 3.1 为什么是软件合成

1. **目标匹配。** 项目目标是“启动 iOS 16.7.16 用户态并显示界面”，不是“让 Adreno
   跑起来”。ios-sweet 用同一份用户态、同一块 1080×2400 面板、同一套 iPhone X
   身份，已经用 CPU 合成做到了完整 SpringBoard；kebab 的差异主要在 SoC 内核侧，
   不在显示协议。
2. **它是唯一被完整验证到 SpringBoard 的路线。** (a) 没有一代 Adreno 跑过
   AGX；(c) 会丢掉 SpringBoard；(d) 只在别的 Adreno 代际上跑通过。只有 (b) 在
   Qualcomm 硬件、同一版 iPhone X 用户态上被完整验证过。
3. **它不排除以后加速。** 软件合成与 GPU 加速共享同一套 IOSurface/IOMFB 呈现
   路径；以后要加速，换的是渲染器，不是显示协议。
4. **它把风险放在已知的地方。** 真正的风险是 ABI 补全、单核性能和 P1 的进度，
   这些都可以测量；AGX 的风险是不可估量的。

### 3.2 GPU 问题是否改变 P3 的建法

**不改 P3 的顺序，但改 P3 的接口约束。**

P3 的目标（DSI 点屏、DPU 图层、`IOMobileFramebuffer` 换直驱后端、`SwapEnd` 走
vsync 中断）与 GPU 无关：无论像素是 CPU 画的还是 GPU 画的，最后都要经过
IOMFB 的 swap 到面板。GPU 加速不会让 P3 少做任何一件事。

但 P3 **不能**把“CPU 拷贝”写进 ABI。具体来说：

* **P3-3 的“换直驱后端”要落在后端接口上，不是改 IOMFB 的 user client。**
  `AppleCLCD` 里应该有一个 `present(surface, rects, flags)` 的后端：现在是
  CPU copy，P3 换成 DPU flip；`SwapEnd`/`SwapWait`/vsync 的用户态语义不变。
* **`SwapEnd` 要入队，不要同步 memcpy。** 现在的 0093 在 `SwapEnd` 里直接按图层
  拷贝；P3 应改成“记录最新 swap + 在 vsync/DPU 完成时提交”，`SwapWait` 等完成。
  这样 CPU 渲染和 GPU 渲染都能共用同一套节流与完成语义。
* **显示模式与物理面板要分开。** 物理是 1080×2400，iOS 要看到 1125×2436 @3x；
  DPU 可以做缩放/居中（取决于 DPU 图层能力，P3-2 验证），IOMFB 报告逻辑模式。
  不要把 1080×2400 写死进 `GetDisplaySize`（ios-sweet 的 0061 就是修这个）。
* **多图层与格式要保留。** 现在支持 4 层 BGRA；以后 GPU 合成可能给多个 surface，
  DPU 也可能直接叠层。不要把“一个全屏 surface”当成唯一形态。
* **vsync/时间戳要可复用。** GPU 路径同样要 vsync 和 frame info；现在用 60 Hz
  定时器可以，P3 换成真实 vsync 中断后，时间戳的时基与通知语义要保持一致。

一句话：**P3 的验收条件不变，但 P3 的实现必须把 IOMFB 当成“显示服务”，而不是
“memcpy 驱动”。**

### 3.3 `IOMobileFramebuffer` 的具体接口要求

按优先级列。第 1–5 项是 P4 之前必须有的；第 6–9 项是 P3/以后加速需要的
（其中错误语义现在就该修）。

1. **服务身份与匹配**
   * 类 `IOMobileFramebuffer`（子类名任意，`AppleCLCD` 可以），挂在
     `device_type = "display-subsystem"` 的 `/arm-io` 子节点下；
   * `external = false`，`canvas-width`/`canvas-height` 放在父节点；
   * `IOMobileFramebufferCreateDisplayList()` 能数到它，且是主显示（type 0）。
   * 现状：0093 已满足（`kebab_display_start`、`kebab_publish_canvas`）。
2. **显示模式**
   * `GetDisplaySize` 返回逻辑尺寸 **1125×2436**（不是 1080×2400）；
   * `GetDotPitch` 返回面板 ppi（kebab 的 395 可以作为初值）；
   * frame info / timing 至少给出 60 Hz、sRGB、rotation 0；
   * 物理 1080×2400 由后端居中/缩放；`FBSDisplayMode` 的校验要能过。
   * 现状：0093 返回 1080×2400，且没有 frame info；这是 SpringBoard 的硬门槛。
3. **Swap 生命周期**
   * `SwapBegin`（4）返回 swap id；`SwapEnd`（5）接收 0x46c 的 swap 记录；
   * 记录里是 **IOSurface id**、每层 enable 位、源/目的 rect、transform；不要传
     内核指针；
   * `SwapWait`（6）按 `[swap id, timeout, 0]` 等待完成；超时返回明确错误；
   * `SwapEnd` 不再同步 memcpy；完成由 vsync/DPU 回调触发。
   * 现状：0093 的 4/5 有，6 是空实现，5 内同步拷贝。
4. **IOSurface 传输**
   * 至少支持 create(0)/lookup(4)/limits(13)/lookup-from-port(34)/create-port(35)
     与 trap 0–5；
   * 补 values（9/0xa/0xb）、bulk attachments（0x1b）；backboardd 创建 surface 后
     会设这些；
   * lock/unlock 要把 base 地址映射进调用者 task，并处理 16 KiB 页与 cache
     （CPU 写 surface 后要 `dc civac`，GPU/显示读之前要可见）；
   * 不要假设物理连续；按 IOBufferMemoryDescriptor/分散页处理。
   * 现状：0093 有 0/4/13/34/35 + trap，缺 9/0x1b。
5. **通知**
   * vsync（type 5）6 个 32 位半字：`(ts lo, hi)`、`(t2 lo, hi)`、`(t3 lo, hi)`；
     时间戳要经过时基转换，不能直接塞 `mach_absolute_time()`；
   * frame info（type 3，选择器 73/74）、power（1）、hotplug（0）；
   * 每个 type 独立 enable/disable；端口断开要清理。
   * 现状：0093 只有 type 5 的定时器通知，时间戳是裸值，没有 frame info/power。
6. **power 与错误语义**
   * 选择器 12（power change）要能 blank/unblank；
   * 不认识的 selector 返回 `kIOReturnUnsupported` 并记录一次，不要静默成功或
     静默清零；
   * 0093 现在对未知 selector 打日志后返回 success（`ret` 初值），这会让上层以为
     调用成功但拿到全 0。
7. **后端抽象**
   * `AppleCLCD` 内部只依赖 `present(surface, rects, flags)` 与
     `wait_complete(swap_id)`；
   * CPU copy 后端是默认实现；P3 的 DPU 后端替换它，IOMFB 的 user client 不变；
   * 后端要能报告“这一帧什么时候上屏”，供 `SwapWait`/vsync 使用。
8. **不要发布 `IOAccelerator`，除非它真能跑 Metal/GLES**
   * ios-sweet 的经验是：没有 accelerator，CA 走软件渲染；一个只会返回成功的
     假 accelerator 可能让 CA 选 Metal 路径然后失败；
   * 如果以后做 d1/d2，`IOAccelerator` 是另一个服务，和 IOMFB 解耦。
9. **可观测性**
   * 计数：surface create/lock/unlock/swap、swap 等待时间、vsync 数、copy 时间、
     fps；
   * 这些数字是 P4 调性能的依据，也是判断“卡在渲染还是卡在显示”的依据。

### 3.4 不要做的事

* **不要给 Adreno 写 AGX 驱动。** 方向不对，且没有一代硬件验证过。
* **不要为了“让 Metal 出现”而发布一个假 `IOAccelerator`。** 它不会让 Metal 工作，
  只会改变 CA 的渲染器选择。
* **不要替换 WindowServer/backboardd。** SpringBoard 依赖它；换掉就不是 iOS 界面。
* **不要把 1080×2400 当成 iOS 的显示模式。** iPhone X 用户态要 1125×2436 @3x。
* **不要在 `SwapEnd` 里做全帧同步 memcpy 并把这件事写进 ABI。** P3 换后端时会
  被这个假设绑死。
* **不要直接搬 Linux 的 MSM/KGSL 代码。** 它是 GPL-2.0，不能直接放进 xnu 的
  源码树；只能按 UABI 重写。

---

## 四、承诺前要测/试的清单（按成本）

成本是量级估计，不是承诺。

| # | 事项 | 成本 | 产出/验收 |
| --- | --- | --- | --- |
| 1 | **ABI 对照**：把 0093 的 IOMFB/IOSurface 选择器与 ios-sweet 中 backboardd 实际调用的集合逐项对齐 | 2–4 小时，零硬件 | 一张缺口表；本文 §2.2 的表是初稿，需要按 0093 源码复核 |
| 2 | **拷贝带宽**：1080×2400×4 = 10.37 MB/帧；30 Hz ≈ 311 MB/s，60 Hz ≈ 622 MB/s；在 kebab 上测 memcpy + `dc civac`（SMP 起来后分别测 A77/A55） | 半天 | 单核能否在 30 fps 下留出渲染余量；决定呈现频率（10/30/60） |
| 3 | **ABI 一致性客户端**：静态 arm64 程序 create/fill/lock/swap/wait，按 backboardd 的顺序走 IOMFB 3/4/5/6 + 12/57/74/84、IOSurface 9/0x1b | 2–4 天（需要 P1 的 dyld/共享缓存） | 面板出稳定图案；`SwapWait` 返回；vsync 单调；选出仍失败的 selector |
| 4 | **显示模式**：以 1125×2436 @3x 呈现，物理 1080×2400 居中；检查 `FBSDisplayMode`/MobileGestalt | 1–2 天 | SpringBoard 不再因 display mode 退出 |
| 5 | **真实 backboardd + SpringBoard**：P1 之后启动，记录首个 IOSurface、首个 swap、fps、CPU | 1–3 周（依赖 P1） | 出现 iOS 界面；量化 20–30 fps 目标；确认无 `IOAccelerator` 也能合成 |
| 6 | **freedreno/GLES 预研**：a650 的 LineageOS 上跑 GLES 三角形；读 HackiOS 的 Darwin 胶水 | 1–2 周 | userspace 可行；KMD 工作量量级 |
| 7 | **metal2vulkan/Turnip 预研**：a650 的 Turnip Vulkan 三角形；读 UniXNU 的 a740 栈 | 3–6 周 | 是否值得为 Metal 应用单开一条线；a650 后端缺口 |

顺序理由：1 是纸面工作，先做；2 决定软件合成在 kebab 上是否够快；3/4 是 P4 的
显示门槛；5 是真正的验收；6/7 只在 P4 成功后才有意义。

---

## 五、未知与风险

* **CA 软件渲染器的选择逻辑没有逐行追过。** ios-sweet 的 NOTES 明确说
  “selection logic not traced yet”（[L117](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md#L117)）。
  我们有的是经验证据：同一份用户态、没有 `IOAccelerator`、SpringBoard 出画面。
  如果 iOS 16.7.16 的某个配置改变了选择，需要重新验证。
* **kebab 的 ABI 缺口可能比 §2.2 的表更多。** 那张表来自 ios-sweet 的日志与
  0093 的源码对照，不是 kebab 上 backboardd 的实测。
* **单核性能。** 当前 `cpus=1`；软件合成 + USB 日志 + UFS 都在一个核上。P2/P3
  的 SMP 进度会直接影响 P4 的帧率。
* **P1 是真正的前置。** 没有 dyld/共享缓存/launchd，后面的显示实验都只能做
  ABI 级别，不能做 CA 级别。GPU 评估不改变这一点。
* **a650 的 Darwin 驱动没有先例。** freedreno/Turnip 在 Linux/Android 上支持
  a650，不等于在 xnu 上可用；KGSL UABI 重写、SMMU、GMU 固件、电源域都是新的。
* **P3 的 DPU/DSI 风险与 GPU 无关。** 面板有 4 家供应商（README 的显示一节），
  P3-1 的点屏序列本身就是一条独立的风险线。
* **“没有公开的 a650 iOS 移植”是截至本文核对的观察。** 生态在变；如果出现新
  项目，应重新评估（尤其是 d1/d2）。

---

## 六、参考资料

1. ios-sweet：README（同一份 iPhone X 16.7.16 用户态、CPU 渲染、无 GPU）：
   <https://github.com/dipa-studio/ios-sweet/blob/main/README.md>
2. ios-sweet：工作笔记（IOMFB/IOSurface ABI、CA 渲染器、backboardd 的
   WindowServer、性能数据）：<https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md>
3. ios-sweet：`0046-sweet-iosurface-iomfb.patch`：
   <https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0046-sweet-iosurface-iomfb.patch>
4. ios-sweet：`0056-sweet-fb-throttled-present.patch`（呈现限流）：
   <https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0056-sweet-fb-throttled-present.patch>
5. ios-sweet：`0061-sweet-iphone-x-display-size.patch`（1125×2436）：
   <https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0061-sweet-iphone-x-display-size.patch>
6. hackiosx.dev：How it works（软件渲染、freedreno、Metal 翻译的三条路）：
   <https://hackiosx.dev/how-it-works>
7. hackiosx.dev：Open problems（完整 Metal 仍未解决）：
   <https://hackiosx.dev/future>
8. hackiosx.dev：Compatibility（SM8250 属于同代可移植，不是已完成）：
   <https://hackiosx.dev/compatibility>
9. HackiOS：内核仓库 README（Adreno 642L 的 OpenGL ES，无 Metal）：
   <https://github.com/oosexgang/hackios-xnu-kernel/blob/main/README.md>
10. HackiOS：`docs/air.md`（AIR→NIR、AGXMetalA11 的规模）：
    <https://github.com/oosexgang/hackios-xnu-kernel/blob/main/docs/air.md>
11. UniXNU：Adreno740（A740 的 KMD、metal2vulkan、Turnip、GPU 合成桌面）：
    <https://unixnu.com/zh/drivers/adreno740>
12. UniXNU：AdrenoMetal（MTLAddDevice、AIR→SPIR-V→IR3）：
    <https://unixnu.com/zh/drivers/adreno-metal>
13. UniXNU：GPU 与 Metal 适配（其他 GPU 代际需要新的内核后端）：
    <https://unixnu.com/zh/porting/gpu>
14. Mesa：freedreno（A650 gen3、`a650_sqe.fw`）：
    <https://docs.mesa3d.org/drivers/freedreno.html>
15. Mesa：Asahi（AGX 是 Apple GPU）：
    <https://docs.mesa3d.org/drivers/asahi.html>
16. Apple：IOSurface 文档：
    <https://developer.apple.com/documentation/iosurface>
17. Apple：Metal 文档：
    <https://developer.apple.com/documentation/metal>
18. Apple 开源：IOGraphics 的 `IOFramebufferSharedUserClient`：
    <https://github.com/apple-oss-distributions/IOGraphics/blob/main/IOGraphicsFamily/IOFramebufferUserClient.cpp#L408>
19. Linux：MSM/Adreno 驱动（GPL-2.0）：
    <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/msm/adreno/adreno_gpu.c>
20. 本项目：`docs/ROADMAP.md`（P0–P4）与 `0093-kebab-display.patch`：
    [ROADMAP.md](ROADMAP.md)、
    [0093-kebab-display.patch](../patches/xnu-8796-kebab/0093-kebab-display.patch)
