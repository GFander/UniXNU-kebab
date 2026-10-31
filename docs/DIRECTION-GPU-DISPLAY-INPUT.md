> 2026-10-31：已按最新 **0424** 重新审核，并按用户新授权开始图形/输入阶段。请先读[0424审核](GPU-DISPLAY-INPUT-REVIEW-0424.md)和[0425–0427进度](GRAPHICS-PREREQUISITE-20261031.md)。蓝牙延期至可用SpringBoard之后。下文保留0289原文，其中“A740直接照抄”、Peppy匹配和部分ABI说法已被审核修正。
>
> 2026-10-30历史：0347审核后当时未实施；该停止点已被最新授权和构建取代。

# GPU / 显示 / 输入 开发方向指南

按以下已定的倾向编写：架构基于 UniXNU；输入走 HID；GPU 先修好再直接做 DPU 直扫。

本版已按 0289 的状态复核，更正了三处事实与四处判断，并标注了证据来源。
本文只定方向与边界，不含代码。

---

## 一、四条已定决策

### 决策 1　架构基线用 UniXNU

```
驱动放在内核里，编成静态库链进内核（内置 XNU 驱动层）
匹配靠 IOKitPersonalities：IOClass / IOProviderClass / IONameMatch / IOProbeScore
personality 写进 iokit/KernelConfigTables.cpp，按板型条件分组
Apple 私有类一律先做 ABI 审计（abi/<class>.json），再生成链接期绑定
```

代价：ABI 恢复工作量大，且与供体版本绑死（20H392 / iOS 16.7.16）。
收益：原版 libMetal、原版 backboardd、原版 SpringBoard 不需要打补丁。

**ABI 工具链已经存在**：`scripts/driver-standardization/abi/`，22 个审计补丁都走这套。
后续接新类时复用，不要另外造一套。

### 决策 2　输入走 HID，不做用户态注入

```
触摸与按键在内核侧发布成 HID 设备
由 Apple 自己的 IOHIDEventDriver（无代码，纯 personality）翻译成 IOHIDEvent
事件经 IOHIDSystem 到 backboardd，再进 SpringBoard
```

已核实的事实（来自 20H392 供体的 `__PRELINK_INFO`）：

```
com.apple.iokit.IOHIDFamily          有代码，依赖仅 kpi.* + AMFI
com.apple.iokit.IOHIDEventDriver     无代码，4 个 personality，provider 都是 IOHIDInterface：
                                       Peppy Touch Display      ← 触屏，正是我们要的
                                       HID Buttons
                                       HID Keyboard Driver
                                       HID Sensor
com.apple.driver.AppleHIDKeyboard    有代码，29 个 personality，依赖仅 IOHIDFamily + kpi
```

**关键含义：Apple 已经自带触屏 personality。** 我们不需要自己构造 IOHIDEvent，
只要发布一个能被 `Peppy Touch Display` 匹配的 `IOHIDInterface`，翻译由 Apple 完成。

### 决策 3　显示等 GPU，然后一次做到直扫

```
跳过"GPU 渲染到扫描缓冲 + 拷贝"这一层中间形态
直接做：GPU 合成 → DPU 扫描输出 → 硬件 vsync 中断 → 120 Hz
```

代价：直扫的三个条件必须同时满足（见第三节）。
收益：少一次改造，最终形态一次到位。

**保留一个调试出口**：现有 `KebabFramebuffer` 的拷贝路径不删，用于验证
"GPU 渲染结果能不能上屏"，直扫打通后降级为回退。

### 决策 4　SMMU 是 GPU 和直扫的共同前置

```
GPU 的 BO 池满后要用分散页，靠 SMMU 拼连续 IOVA      → 绕不过
直扫 唯一的实现参考是靠显示 SMMU 的 stage-1 翻译取图  → 同样绕不过
```

依据（已核实 HackiOS 的 `lemonade_display.c`）：

```
它自己 kmem_alloc 出页表，按 AArch64 stage-1 LPAE 填表项（physical | 3）
直接写显示 context bank：CBAR(0x20/0x24)、SCTLR(0x10)、TCR(0x30)、MAIR(0x38)
换缓冲写的是 IOVA：mdp + 0x4014 / +0x6014 = DISPLAY_BUFFER_IOVA(n)
TLB 按 ASID 5 失效（cb + 0x610），轮询 TLBSTATUS（cb + 0x7f4）
代码注释明确："Cache only their translations, never an IOSurface's physical pages"
```

也就是说：**HackiOS 的直扫建立在显示 SMMU 之上，不是绕过它。**

唯一不用 SMMU 的办法是保证扫描缓冲物理连续（预留连续池，或给那条 stream 配
bypass）。这条路两个参考项目都没走过，属于我们要自己验证的备选。

排期结论：**SMMU 一次做好，GPU 与直扫都受益**，不把它挂在某一边名下。

---

## 二、GPU

### 目标终态

```
能创建 Metal device
CoreAnimation 能合成，UI 帧率达标
渲染结果可读回校验（先读回，后上屏）
```

### 架构（照 UniXNU 的分层）

```
第 0 层  SMMU          ArmSmmu4K：4 KiB 页表、表池、可见性回调、TLB 失效
第 1 层  供电与固件     GMU 初始化、HFI 协议、OPP/带宽投票
第 2 层  内存与提交     BO 分配、IOVA 映射、ring buffer、fence
第 3 层  用户客户端     实现 Apple 的 IOGPU user-client ABI（selector 表）
第 4 层  Metal 前端     AIR → SPIR-V → NIR → IR3（Turnip）→ PM4
```

对应 UniXNU 的文件划分（Adreno740，10561 行），与 GPU 代际无关，可直接照抄：

```
NativeAdrenoService.cpp / Vm / Submit / Ring / Memory / UserClient / Publish
```

### A650 必须替换的部分

```
寄存器表        a7xx → a6xx（从 Mesa 的 a6xx 描述生成，不要手抄）
固件            a740_gmu/sqe/zap → a650 对应文件（从 linux-firmware 核对确切文件名）
GMU 版本与 HFI   代际相关，按 a6xx 核对
供电与带宽        SM8550 的 OPP 表 → SM8250 的
SMMU stream ID  SM8550 的 → SM8250 的，从设备树取，不能照抄
```

### 可以复用的部分

```
AdrenoMetal 的编译链（AIR → IR3）与代际无关，整体复用
AdrenoPM4 的包编码框架通用，只换寄存器编号
AdrenoFirmware 的 HFI 消息框架
ArmSmmu4K 整个直接用
```

### 内存与 DMA 的新前提（0289 状态）

```
0272/0273 已通过高地址 DMA；0274 验证约 8 GiB XNU 内存、八核
仓库已有 kebab_dma_limits.h 定义现有 DMA 上限模型
```

因此：GPU 的 BO 池与扫描缓冲**按现有 DMA aperture 规则申请**，
不要把"低地址 aperture"当作默认前提，也不要在驱动里另立一套地址上限。

### 性能要点（UniXNU 已验证的规则，照搬）

```
BO 池            板级预留的连续 write-combined 池，池满才退化到分散页
对象查找          每客户端上限 16384，handle 索引
自有内存映射       批量映射，不做别名检查
导入内存映射       同缓存类别且不重叠时走同一快路径
解除映射          不超过 512 页逐页 TLBIVAL，更多按 ASID 失效
提交              不做 TLB 失效（只填无效叶）
可观测            [vm map] / [vm unmap] / [bo pool] 三行计数与耗时
```

### 参考清单

```
UniXNU/Adreno740           内核服务结构、性能规则
UniXNU/AdrenoMetal         前端 ABI、编译链
UniXNU/AdrenoPM4           PM4 编码
UniXNU/AdrenoFirmware      固件与 HFI
UniXNU/ArmSmmu4K           SMMU
UniXNU 文档                /zh/porting/gpu
Mesa freedreno             a6xx 寄存器、IR3、Turnip
Linux msm                  adreno 目录：A650 固件名、GMU 参数
HackiOS RedwoodGPU         另一条路线的接口形状（仅供对照，不采用其自定 ABI）
```

---

## 三、显示

### 目标终态

```
DPU 直接扫描 GPU 合成的缓冲
硬件 vsync 中断驱动换帧
120 Hz，无撕裂
backboardd 打开 IOMobileFramebuffer 成功
```

### 现状（0289 状态，必须写清）

```
KebabFramebuffer 通过 PE_current_console(&video) 取得固件留下的 splash 缓冲
帧缓冲地址来自补丁 0035（early-console-fb-address）与 0036（fb-address-everywhere）
仓库里没有 ADT 显示节点：make-kebab-adt.py 搜不到 dsi/mdss
→ 显示资源目前是硬编码地址，这与 UniXNU "资源来自 provider 的 ADT 属性" 规则不一致
```

短期接受硬编码但登记来源；长期补 ADT 节点。**在补上之前，不要把显示驱动
说成"符合 UniXNU 规范"。**

### 直扫的三个必要条件

```
一  GPU 能合成（CoreAnimation 已能渲染）
二  显示 context bank 有 stage-1 窗口，或扫描缓冲物理连续
三  缓冲生命周期正确：正在被扫描的缓冲不能释放或改写
```

第二条给了两条路：一是显示 SMMU 的 stage-1 窗口（唯一被实现验证过的），
二是物理连续扫描缓冲或 stream bypass（两家都没走过，属于待验证的备选）。

### 分步

```
D1  DPU 只读探测
      读管线源地址、尺寸、格式、取图设置；CTL 状态；tear check；显示 SMMU 流
      全部只读，不改任何寄存器
      管线数量按实际配置确认，不要预设（SM8250/OnePlus 8T 与双 DSI 机型不同）

D2  硬件 vsync
      用 GIC SPI 中断替换定时器模拟
      handler 里做 tear check，thread_call 分发
      提供注册与快照接口（供 framebuffer 类查询 tick/period/count）
      0274 起是八核：中断路由与处理核绑定要与 KebabCPU 的拓扑一致
      否则帧同步抖动会随核迁移变化

D3  直扫
      依赖显示 SMMU 的 stage-1 翻译（见决策 4 的核实结论）
      要点（全部取自 HackiOS 的实测实现）：
        IOVA 空间限制 32 位（iova >> 32 直接拒绝）
        TCR = 0x803519（T0SZ 25 → 39 位输入地址），MAIR = 0xf404ff44
        页大小 4 KiB（表项要求 physical & 4095 == 0）
        双缓冲两个 IOVA，换页写 mdp + 0x4014 与 + 0x6014，再写 0x15018 = 3
        逐帧按需建叶子，不是一次性全映射；每帧后按 ASID 失效并等 TLBSTATUS
      备选（未验证）：预留物理连续扫描池或给该 stream 配 bypass，可绕开 SMMU
        两个参考项目都没走过这条路，需要我们自己验证 DPU 的地址宽度与 bypass 许可

D4  120 Hz
      先稳定 60，再按面板型号确认命令序列
      不把"设了 120"当成"跑在 120"，要测帧间隔
```

### 参考数值（标注来源，不是承诺）

```
HackiOS 在 SM8350 / Adreno 660 上的实测：
  单缓冲 + 拷贝     60.5 fps
  双缓冲后          116-117 fps，P95 帧间隔 16.6 ms → 8.3 ms

我们的硬件是 SM8250 / Adreno 650，面板与驱动都不同，不保证复现
```

### 参考清单

```
HackiOS lemonade_display.c    DPU 寄存器、tear check、vsync 分发、120 Hz 守卫
                              （MDSS 0x0ae00000 / MDP 0x0ae01000，与 SM8250 同基址）
UniXNU GopFramebuffer         NativeLegacyDisplay.cpp、NativeGOPPresenter.cpp、
                              OriginalMobileFramebufferABI.h
UniXNU 文档                   /zh/porting/display
Mesa / Linux dpu1             管线与 timing 的寄存器语义
```

### 备注

直扫在 UniXNU 文档里标注为**实验性、默认关闭**，其主路径是 GOP 拷贝 + GPU 呈现；
在 HackiOS 里直扫是主路径。两者定位不同，我们选择直扫属于比较激进的做法，
因此更需要保留回退。

---

## 四、输入

### 目标终态

```
多点触控：按下、移动、抬起、取消，坐标与压力正确
按键：电源、音量，含按下与释放
事件经 HID 到 backboardd，SpringBoard 有响应
```

### 路径

```
触摸/按键采样（现有 s3908 与 PMIC 采样保持不变）
    ↓
内核发布一个能被 Peppy Touch Display 匹配的 IOHIDInterface
    ↓
Apple 的 IOHIDEventDriver 翻译
    ↓
IOHIDEvent → IOHIDSystem → backboardd
```

### 硬前置：IOHIDFamily 必须进预链接闭包

这不是"依赖很轻"，而是**只有这一条路**：

```
内核拒绝加载非预链接 kext：
  "Refusing to link non-prelinked kext: %s (no kxld support)"
KDK 里的 IOHIDFamily 是 macOS 版（x86_64 + arm64e，平台 MacOSX），不可用
我们的 IOKit 有框架但没有 HID 类
仓库已有 package/HidInputProbe.cpp 这个默认关闭的探针，
  尝试 OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily")，注定失败
→ 必须把 com.apple.iokit.IOHIDFamily 加进 roots，并接受随之增加的闭包
```

注意：仓库里没有 roots 的配置，roots 在构建机侧。加之前先在构建机确认闭包增长与
跨度/同余是否仍然通过。

### 为什么用报告描述符而不是直接构造 IOHIDEvent

```
报告描述符是公开标准，写错了能被工具验证
翻译交给 Apple 的 personality，事件字段由它填
少一处私有 ABI 依赖
```

### 要准备的东西

```
多点触控报告描述符
    逻辑最大值、物理尺寸、报告 ID、同时接触点数
    必须与 Synaptics S3908 的实际能力一致（驱动已能读到 X/Y 范围）

按键描述符
    consumer control 或键盘用途页
    电源键按 iOS 的用法确认，不是普通键盘键

类声明与 ABI
    IOHIDInterface / IOHIDDevice 的类声明与 vtable 槽序
    从 20H392 供体恢复，写入 abi/*.json
```

### 保留的

```
/dev 上的诊断接口（kebab_touch、kebab_keys）保留，用于不依赖 HID 的采样验证
现有去抖逻辑不动
```

### 参考清单

```
UniXNU 文档                   /zh/porting/input
Apple OSS IOHIDFamily         事件服务的组织方式（架构参考，不能当私有 ABI 依据）
HackiOS redwood_input.c       另一条路线（/dev + 注入），仅作对照，不采用
```

---

## 五、依赖顺序

```
SMMU ─→ GPU 服务 ─→ Metal 前端 ─→ GPU 验收
  └──→ 显示 context bank ─→ 直扫 ─→ 120 Hz ─→ 显示验收
                                      │
DPU 只读探测 ─→ 硬件 vsync ────────────┤
                                      │
HID 设备 ─→ IOHIDEventDriver 匹配 ─→ backboardd 收事件 ─→ 输入验收
                                      │
                                      ↓
                               backboardd 完整运行
                                      ↓
                                  SpringBoard
```

硬性依赖：

```
SMMU 同时是 GPU 与直扫的前置（已核实 HackiOS 靠显示 SMMU 取图）
GPU 验收需要一条呈现通路（用现有拷贝路径做调试出口）
直扫需要 GPU 已能合成，且显示 context bank 已配好
backboardd 需要显示能打开、输入有事件源
SpringBoard 需要 backboardd 稳定
```

---

## 六、与 B 阶段的耦合点

指南不是孤立推进的，0289 的状态决定了几个耦合：

```
SystemOS 已只读挂载、DSC 规划与真实映射已通过、三份原版 trustcache 已加载
  → 用户态正在成型，图形与输入会越来越早成为显性阻塞

Data 卷仍未配对
  → 动态信任缓存与部分系统服务可能依赖它，会间接到 backboardd

backboardd 是否属于阻塞启动的 boot task 尚未核实
  → 需要先用 job 清单确认；若它阻塞 launchd，显示与输入就要提前到 GPU 之前
```

**待确认项（不写成结论）**：backboardd 在 launchd 任务表中的位置。

---

## 七、三条红线

```
一  不伪造成功
    安全接口返回 unsupported 而不是假成功；ABI 不符就拒绝构建

二  不碰 secure context
    SMMU 的 secure context 是安全世界在用的，改错直接挂死且无日志

三  保留回退
    直扫失败能退回拷贝呈现；GPU 失败能退回无 GPU 的调试路径
```

---

## 八、待定事项

```
一  NO_KERNEL_HID 是否去掉
      内核目前带这个宏编译，源码树里没有任何 IOHID 源码
      若只发布 HID 设备、不编 IOHID 代码，可能不必动它
      但它同时门控 DISPLAY_WRANGLER_PRESENT（电源域相关）
      先按"不动"推进，遇到依赖再评估

二  Surface 的发布方式
      规则上应写进 KernelConfigTables.cpp 的 personality（现在零 KEBAB 条目）
      但 HackiOS 证明"内核里直接 new + registerService"同样可行
      按 UniXNU 规则优先，若匹配行为不符预期再切换

三  触摸 HID 的用途页选择
      digitizer 还是 vendor-defined，取决于 Peppy Touch Display 的期望
      需要真机验证一次
```

---

## 九、验收指标

```
GPU     Metal device 创建成功；渲染一帧读回像素正确；pipeline 创建与缓存复用分别计时
显示    画面稳定无撕裂；UI 滑动 ≥ 110 fps；P95 帧间隔 < 9 ms；分辨率与面板原生一致
输入    多点按下/移动/抬起/取消全序列正确；按键含释放；SpringBoard 有响应
```
